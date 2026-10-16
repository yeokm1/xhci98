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
    param([string[]]$Arguments, [switch]$WithBinaryGates, [switch]$NoHlprStandIn)
    # The stand-in "driver" is a text file, so the packager's pre-staging host
    # test suite and import gate are skipped for every case except the one
    # that is about them.
    if (-not $WithBinaryGates) { $Arguments = $Arguments + @("-SkipBinaryGates") }
    #
    # The 32-bit INF names xhcihlpr.sys, the x86 helper, since task
    # 36.15 (design record 18), so an x86 stand-in package needs a stand-in
    # helper too, carrying the flavour the run asks for. Every case gets one
    # unless it names its own or is about the helper's absence; the amd64
    # package carries none.
    $isAmd64 = $false
    $flavour = "debug"
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        if ($Arguments[$i] -eq "-Arch" -and $i + 1 -lt $Arguments.Count -and $Arguments[$i + 1] -eq "amd64") { $isAmd64 = $true }
        if ($Arguments[$i] -eq "-Flavor" -and $i + 1 -lt $Arguments.Count) { $flavour = $Arguments[$i + 1] }
    }
    if (-not $NoHlprStandIn -and -not $isAmd64 -and ($Arguments -notcontains "-HlprDriverPath")) {
        $standIn = Join-Path $script:work ("xhcihlpr-" + $flavour + ".sys")
        if (-not (Test-Path -LiteralPath $standIn)) {
            Set-Content -LiteralPath $standIn -Value ("not a real helper XHCI98_FLAVOUR_" + $flavour.ToUpperInvariant()) -Encoding ASCII
        }
        $Arguments = $Arguments + @("-HlprDriverPath", $standIn)
    }
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
    # Whitespace collapsed: Windows PowerShell wraps a native command's stderr
    # ErrorRecord at the console width, so on a long repository path a refusal
    # such as "volume or repository root" arrived split across lines and its
    # phrase match failed (F6 legs, 2026-10-04: a 41-character worktree path).
    return @{ Output = ($out -replace '\s+', ' '); ExitCode = $LASTEXITCODE }
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
    # Whitespace collapsed: Windows PowerShell wraps a native command's stderr
    # ErrorRecord at the console width, so on a long repository path a refusal
    # such as "volume or repository root" arrived split across lines and its
    # phrase match failed (F6 legs, 2026-10-04: a 41-character worktree path).
    return @{ Output = ($out -replace '\s+', ' '); ExitCode = $LASTEXITCODE }
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
    foreach ($f in @("xhci98.inf", "xhci98.sys", "xhcihlpr.sys")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $flatOut $f)) "'$f' is missing from the flat package."
    }

    # --- the bundled nvme98 build (owner, 2026-10-15) -----------------------
    #
    # nvme98 is a separate project, bundled from a pinned release folder.
    # These are synthetic release folders in both layouts - 0.1.x (win9x\,
    # x86\, amd64\, bundled under nvme98\) and 0.2.0.0 on (x86\ and x64\ at
    # the package root), the second not yet cut by nvme98 - with stand-in
    # files and a SHA256SUMS.txt written here, so nothing depends on the
    # nvme98 repository being present.
    Write-Step "nvme98: both layouts bundle unchanged, and every pin is held"
    $nvAmd64Inf = Join-Path $repo "src\xhci98-amd64.inf"
    $nvNoHeader = Join-Path $script:work "no-such-nvme98cm.h"
    $nvHeader1 = Join-Path $script:work "nvme98cm-1.h"
    Set-Content -LiteralPath $nvHeader1 -Value "#define NVME98LS_VERSION            1" -Encoding ASCII
    function New-Nvme98Release {
        # A synthetic release folder: name -> text, then SHA256SUMS.txt over
        # every file. Returns the folder and the SHA-256 of SHA256SUMS.txt.
        param([string]$Name, [hashtable]$Files, [string[]]$Unlisted = @())
        $dir = Join-Path $script:work $Name
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $lines = @()
        foreach ($rel in ($Files.Keys | Sort-Object)) {
            $full = Join-Path $dir $rel
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $full) | Out-Null
            [System.IO.File]::WriteAllText($full, $Files[$rel], (New-Object System.Text.ASCIIEncoding))
            if ($Unlisted -notcontains $rel) {
                $lines += ("{0}  {1}" -f (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant(), $rel.Replace('\', '/'))
            }
        }
        $sums = Join-Path $dir "SHA256SUMS.txt"
        [System.IO.File]::WriteAllText($sums, (($lines -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))
        return @{ Dir = $dir; Sha = (Get-FileHash -LiteralPath $sums -Algorithm SHA256).Hash }
    }
    function New-Nvme98Inf { param([string]$V) return "[Version]`r`nSignature=`"`$CHICAGO`$`"`r`nDriverVer=10/20/2026,$V`r`n" }
    $nvOldFiles = @{
        "win9x\nvme98.inf" = (New-Nvme98Inf "0.1.1.0"); "win9x\nvme98.mpd" = "stand-in mpd"
        "x86\nvme98.inf" = (New-Nvme98Inf "0.1.1.0"); "x86\nvme98.sys" = "stand-in x86 sys"
        "amd64\nvme98.inf" = (New-Nvme98Inf "0.1.1.0"); "amd64\nvme98.sys" = "stand-in amd64 sys"
        "amd64\nvme98-test.cer" = "stand-in cer"
        "LICENSE" = "stand-in licence"; "NOTICE.TXT" = "stand-in notice"; "readme.txt" = "stand-in readme"
    }
    $nvNewFiles = @{
        "x86\nvme98.inf" = (New-Nvme98Inf "0.2.0.0"); "x86\nvme98.mpd" = "stand-in mpd"; "x86\nvme98.sys" = "stand-in x86 sys"
        "x64\nvme98.inf" = (New-Nvme98Inf "0.2.0.0"); "x64\nvme98.sys" = "stand-in x64 sys"; "x64\nvme98-test.cer" = "stand-in cer"
        "LICENSE" = "stand-in licence"; "NOTICE.TXT" = "stand-in notice"; "readme.txt" = "stand-in readme"
    }
    $nvOld = New-Nvme98Release -Name "nv-0.1.1.0" -Files $nvOldFiles
    $nvNew = New-Nvme98Release -Name "nv-0.2.0.0" -Files $nvNewFiles
    function Get-NvArgs {
        param($Rel, [string]$V, [string]$Iface = "none", [string]$Header = $nvNoHeader)
        return @("-Nvme98Version", $V, "-Nvme98Dir", $Rel.Dir, "-Nvme98SumsSha256", $Rel.Sha,
                 "-Nvme98InterfaceVersion", $Iface, "-Nvme98HeaderPath", $Header)
    }
    function Test-NvCopies {
        # Every expected file at its package path, byte for byte the release's.
        param([string]$Pkg, $Rel, [hashtable]$Map, [string]$What)
        foreach ($src in $Map.Keys) {
            $dest = Join-Path $Pkg $Map[$src]
            Assert-True (Test-Path -LiteralPath $dest) "$What has no '$($Map[$src])' (nvme98's $src)."
            if (Test-Path -LiteralPath $dest) {
                Assert-True ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -eq
                             (Get-FileHash -LiteralPath (Join-Path $Rel.Dir $src) -Algorithm SHA256).Hash) `
                    "$What's '$($Map[$src])' is not nvme98's $src unchanged."
            }
        }
    }
    $nvRenamed = @{ "LICENSE" = "nvme98-LICENSE.txt"; "NOTICE.TXT" = "nvme98-NOTICE.txt"; "readme.txt" = "nvme98-readme.txt" }

    # The new layout (0.2.0.0 on), x86: x86\* and the renamed three at the root.
    $nvOut = Join-Path $script:work "pkg-nv-new-x86"
    $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $nvOut) + (Get-NvArgs $nvNew "0.2.0.0"))
    Assert-True ($r.ExitCode -eq 0) ("an x86 package bundling the new-layout nvme98 was rejected:`n" + $r.Output)
    $map = @{ "x86\nvme98.inf" = "nvme98.inf"; "x86\nvme98.mpd" = "nvme98.mpd"; "x86\nvme98.sys" = "nvme98.sys" }
    foreach ($k in $nvRenamed.Keys) { $map[$k] = $nvRenamed[$k] }
    Test-NvCopies -Pkg $nvOut -Rel $nvNew -Map $map -What "the new-layout x86 package"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "nvme98-test.cer"))) "the x86 package carries the x64 build's nvme98-test.cer."
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "SHA256SUMS.txt"))) "a package carries nvme98's SHA256SUMS.txt, which lists files it does not hold."
    Assert-True ([System.IO.File]::ReadAllText((Join-Path $nvOut "xhci98.inf")) -eq [System.IO.File]::ReadAllText($plainInf)) "bundling nvme98 changed xhci98.inf."

    # The new layout, amd64: x64\* at the root, the test certificate with it.
    $nvOut = Join-Path $script:work "pkg-nv-new-amd64"
    $r = Invoke-Packager (@("-Arch", "amd64", "-InfPath", $nvAmd64Inf, "-DriverPath", $driver, "-OutDir", $nvOut) + (Get-NvArgs $nvNew "0.2.0.0"))
    Assert-True ($r.ExitCode -eq 0) ("an amd64 package bundling the new-layout nvme98 was rejected:`n" + $r.Output)
    $map = @{ "x64\nvme98.inf" = "nvme98.inf"; "x64\nvme98.sys" = "nvme98.sys"; "x64\nvme98-test.cer" = "nvme98-test.cer" }
    foreach ($k in $nvRenamed.Keys) { $map[$k] = $nvRenamed[$k] }
    Test-NvCopies -Pkg $nvOut -Rel $nvNew -Map $map -What "the new-layout amd64 package"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "nvme98.mpd"))) "the amd64 package carries the 98/ME nvme98.mpd."

    # The old layout (0.1.x), x86: win9x\ and x86\ unchanged under nvme98\.
    $nvOut = Join-Path $script:work "pkg-nv-old-x86"
    $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $nvOut) + (Get-NvArgs $nvOld "0.1.1.0"))
    Assert-True ($r.ExitCode -eq 0) ("an x86 package bundling the old-layout nvme98 was rejected:`n" + $r.Output)
    $map = @{ "win9x\nvme98.inf" = "nvme98\win9x\nvme98.inf"; "win9x\nvme98.mpd" = "nvme98\win9x\nvme98.mpd"
              "x86\nvme98.inf" = "nvme98\x86\nvme98.inf"; "x86\nvme98.sys" = "nvme98\x86\nvme98.sys" }
    foreach ($k in $nvRenamed.Keys) { $map[$k] = "nvme98\" + $nvRenamed[$k] }
    Test-NvCopies -Pkg $nvOut -Rel $nvOld -Map $map -What "the old-layout x86 package"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "nvme98\amd64"))) "the x86 package carries nvme98's amd64\."
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "nvme98.inf"))) "the old layout put an nvme98 file at the package root."

    # The old layout, amd64: amd64\ (with nvme98-test.cer) under nvme98\.
    $nvOut = Join-Path $script:work "pkg-nv-old-amd64"
    $r = Invoke-Packager (@("-Arch", "amd64", "-InfPath", $nvAmd64Inf, "-DriverPath", $driver, "-OutDir", $nvOut) + (Get-NvArgs $nvOld "0.1.1.0"))
    Assert-True ($r.ExitCode -eq 0) ("an amd64 package bundling the old-layout nvme98 was rejected:`n" + $r.Output)
    $map = @{ "amd64\nvme98.inf" = "nvme98\amd64\nvme98.inf"; "amd64\nvme98.sys" = "nvme98\amd64\nvme98.sys"
              "amd64\nvme98-test.cer" = "nvme98\amd64\nvme98-test.cer" }
    foreach ($k in $nvRenamed.Keys) { $map[$k] = "nvme98\" + $nvRenamed[$k] }
    Test-NvCopies -Pkg $nvOut -Rel $nvOld -Map $map -What "the old-layout amd64 package"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $nvOut "nvme98\win9x"))) "the amd64 package carries nvme98's win9x\."

    # Without the input, no nvme98 at all (the flat package above).
    Assert-True (@(Get-ChildItem -LiteralPath $flatOut -Recurse -Force | Where-Object { $_.Name -like "nvme98*" }).Count -eq 0) `
        "a package built without the nvme98 input carries nvme98 files."

    # The interface version: pinned against the header xhci98 compiles
    # against; a mismatch refused, and bundled only with the override, which
    # is printed.
    $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", (Join-Path $script:work "pkg-nv-if1")) +
                          (Get-NvArgs $nvNew "0.2.0.0" "1" $nvHeader1))
    Assert-True ($r.ExitCode -eq 0) ("interface version 1 against a header declaring 1 was refused:`n" + $r.Output)
    foreach ($bad in @(
        @{ Iface = "1";    Header = $nvNoHeader; Says = "compiles against no nvme98 interface" },
        @{ Iface = "2";    Header = $nvHeader1;  Says = "declares NVME98LS_VERSION 1" },
        @{ Iface = "none"; Header = $nvHeader1;  Says = "no link-state receiver" }
    )) {
        $out = Join-Path $script:work ("pkg-nv-ifbad-" + $bad.Iface)
        $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $out) + (Get-NvArgs $nvNew "0.2.0.0" $bad.Iface $bad.Header))
        Assert-True ($r.ExitCode -ne 0 -and $r.Output -match [regex]::Escape($bad.Says)) `
            ("interface '$($bad.Iface)' against the wrong header was not refused with '$($bad.Says)'. Output:`n" + $r.Output)
        Assert-True (-not (Test-Path -LiteralPath $out)) "a package refused for its nvme98 interface left an output directory."
    }
    $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", (Join-Path $script:work "pkg-nv-ifover"), "-Nvme98AllowInterfaceMismatch") +
                          (Get-NvArgs $nvNew "0.2.0.0" "2" $nvHeader1))
    Assert-True ($r.ExitCode -eq 0 -and $r.Output -match "INTERFACE MISMATCH OVERRIDDEN") `
        ("-Nvme98AllowInterfaceMismatch did not bundle the build with the override printed. Output:`n" + $r.Output)

    # Every other pin, refused before anything is staged.
    $nvTampered = New-Nvme98Release -Name "nv-tampered" -Files $nvNewFiles
    Add-Content -LiteralPath (Join-Path $nvTampered.Dir "x86\nvme98.sys") -Value "changed" -Encoding ASCII
    $nvExtra = New-Nvme98Release -Name "nv-extra" -Files ($nvNewFiles + @{ "x86\nvme98.dll" = "unlisted" }) -Unlisted @("x86\nvme98.dll")
    $nvClash = New-Nvme98Release -Name "nv-clash" -Files ($nvNewFiles + @{ "x86\xhci98.inf" = "would replace ours" })
    $nvTopFile = New-Nvme98Release -Name "nv-top" -Files ($nvNewFiles + @{ "other.txt" = "unknown top-level file" })
    $nvOldMissing = $nvOldFiles.Clone(); $nvOldMissing.Remove("amd64\nvme98-test.cer")
    $nvMissing = New-Nvme98Release -Name "nv-missing" -Files $nvOldMissing
    $nv010 = New-Nvme98Release -Name "nv-0.1.0.0" -Files $nvOldFiles
    $nvNewNoCer = $nvNewFiles.Clone(); $nvNewNoCer.Remove("x64\nvme98-test.cer")
    $nvNoCer = New-Nvme98Release -Name "nv-nocer" -Files $nvNewNoCer
    foreach ($bad in @(
        @{ Args = (Get-NvArgs $nvNew "0.2.0.0") | ForEach-Object { if ($_ -eq $nvNew.Sha) { "0" * 64 } else { $_ } }; Says = "not the pinned" },
        @{ Args = (Get-NvArgs $nvTampered "0.2.0.0"); Says = "Release folders are write-once" },
        @{ Args = (Get-NvArgs $nvExtra "0.2.0.0"); Says = "not in its SHA256SUMS.txt" },
        @{ Args = (Get-NvArgs $nvClash "0.2.0.0"); Says = "could clash" },
        @{ Args = (Get-NvArgs $nvTopFile "0.2.0.0"); Says = "does not know where to put" },
        @{ Args = (Get-NvArgs $nvMissing "0.1.1.0"); Says = "amd64\nvme98-test.cer" },
        @{ Args = (Get-NvArgs $nvNew "0.1.1.0"); Says = "old layout carries" },
        @{ Args = (Get-NvArgs $nvOld "0.2.0.0"); Says = "new layout carries" },
        @{ Args = (Get-NvArgs $nv010 "0.1.0.0"); Says = "is never bundled" },
        @{ Args = (Get-NvArgs $nvNoCer "0.2.0.0"); Says = "x64\nvme98-test.cer" },
        @{ Args = (Get-NvArgs $nvOld "0.1.1.0" "1" $nvHeader1); Says = "has no link-state receiver" },
        @{ Args = (Get-NvArgs $nvNew "0.2"); Says = "not a four-part version" },
        @{ Args = @("-Nvme98Version", "0.2.0.0", "-Nvme98Dir", $nvNew.Dir); Says = "go together" },
        @{ Args = @("-Nvme98AllowInterfaceMismatch"); Says = "no nvme98 input" }
    )) {
        $out = Join-Path $script:work "pkg-nv-bad"
        $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $out) + @($bad.Args))
        Assert-True ($r.ExitCode -ne 0 -and $r.Output -match [regex]::Escape($bad.Says)) `
            ("an nvme98 input was not refused with '$($bad.Says)'. Output:`n" + $r.Output)
        Assert-True (-not (Test-Path -LiteralPath $out)) "a package refused for its nvme98 input left an output directory."
    }

    # Any 0.1.x.y from 0.1.1.0 takes the old layout (a 0.1.1.1 hotfix
    # included), and the substitute header is the self-tests' alone.
    $nvOldFiles111 = @{}
    foreach ($k in $nvOldFiles.Keys) { $nvOldFiles111[$k] = $nvOldFiles[$k].Replace("0.1.1.0", "0.1.1.1") }
    $nvOld111 = New-Nvme98Release -Name "nv-0.1.1.1" -Files $nvOldFiles111
    $nvOut = Join-Path $script:work "pkg-nv-old111"
    $r = Invoke-Packager (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $nvOut) + (Get-NvArgs $nvOld111 "0.1.1.1"))
    Assert-True ($r.ExitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $nvOut "nvme98\x86\nvme98.sys"))) `
        ("an old-layout 0.1.1.1 was not bundled under nvme98\:`n" + $r.Output)
    $r = Invoke-Packager -WithBinaryGates -Arguments (@("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", (Join-Path $script:work "pkg-nv-hdr"),
                          "-NoTargetEvidence") + (Get-NvArgs $nvNew "0.2.0.0" "1" $nvHeader1))
    Assert-True ($r.ExitCode -ne 0 -and $r.Output -match "self-tests only") `
        ("-Nvme98HeaderPath was accepted without -SkipBinaryGates. Output:`n" + $r.Output)

    # The release notes' one declaration, against what a cut bundles.
    $bt = [string][char]96
    foreach ($c in @(
        @{ Text = "x`r`nThis download bundles nvme98 ${bt}0.1.1.0${bt} (dry cut only).`r`ny"; V = "0.1.1.0"; Ok = $true },
        @{ Text = "This download bundles nvme98 ${bt}0.2.0.0${bt}.`r`n"; V = "0.2.0.0"; Ok = $true },
        @{ Text = "This download bundles nvme98 0.2.0.0.`r`n"; V = "0.2.0.0"; Ok = $true },
        @{ Text = "This download bundles no nvme98.`r`n"; V = ""; Ok = $true },
        @{ Text = "This download bundles nvme98 ${bt}(version: filled in at the cut)${bt}.`r`n"; V = "0.2.0.0"; Ok = $false },
        @{ Text = "This download bundles nvme98 ${bt}(version: filled in at the cut)${bt}.`r`n"; V = ""; Ok = $false },
        @{ Text = "This download bundles nvme98 ${bt}0.2.0.0${bt}.`r`n"; V = ""; Ok = $false },
        @{ Text = "This download bundles nvme98 ${bt}0.2.0.0${bt}.`r`n"; V = "0.2.0.1"; Ok = $false },
        @{ Text = "This download bundles no nvme98.`r`n"; V = "0.2.0.0"; Ok = $false },
        @{ Text = "nvme98 ${bt}0.2.0.0${bt} is mentioned, but there is no declaration.`r`n"; V = "0.2.0.0"; Ok = $false },
        @{ Text = "This download bundles no nvme98.`r`nThis download bundles nvme98 0.2.0.0.`r`n"; V = "0.2.0.0"; Ok = $false }
    )) {
        $why = Get-Nvme98NotesFault -Text $c.Text -Version $c.V
        Assert-True (($why -eq "") -eq $c.Ok) ("Get-Nvme98NotesFault on '" + $c.Text.Trim() + "' for '" + $c.V + "' answered '" + $why + "'.")
    }
    $realNotes = [System.IO.File]::ReadAllText((Join-Path $repo "docs\using\release-notes.md"))
    Assert-True (([regex]::Matches($realNotes, '(?m)^This download bundles (.+?)\.[ \t]*\r?$')).Count -eq 1) `
        "docs\using\release-notes.md does not carry exactly one 'This download bundles ...' declaration for the cut to fill in."
    # make-release.ps1: the release legs alone get the pins, the bundled
    # files are accepted beside the declared set by exactly their paths, the
    # upload set copies them from the release folder (releases\ never holds
    # them), and their INFs' date and file times are left as nvme98 cut them.
    $nvRelText = [System.IO.File]::ReadAllText($releaser)
    foreach ($anchor in @(
        @{ Find = 'if ($null -ne $nvme98 -and $f -eq "release") {'; Why = "the debug legs would bundle nvme98 too" },
        @{ Find = '$(if ($legSigned) { $certPublishable } else { @() }) + $legNvme98)'; Why = "the build loop no longer accepts exactly the bundled paths" },
        @{ Find = '@($nvFiles | ForEach-Object { $_.Dest }))'; Why = "the upload assembly no longer accepts exactly the bundled paths" },
        @{ Find = 'Copy-Item -LiteralPath (Join-Path $Nvme98.Dir $nf.Source) -Destination $nvDest'; Why = "the upload set no longer takes nvme98 from the pinned release folder" },
        @{ Find = 'Where-Object { -not $nvUploaded.ContainsKey($_.FullName.ToLowerInvariant()) }'; Why = "nvme98's INFs would fail the one-date rule" },
        @{ Find = 'if ($nvUploaded.ContainsKey($item.FullName.ToLowerInvariant())) { continue }'; Why = "nvme98's file times would be rewritten" },
        @{ Find = '-Publishable $publishable -Staged -Nvme98 $nvme98'; Why = "the ordinary cut's upload set no longer bundles nvme98" },
        @{ Find = '$askNvme98 = Get-Nvme98PinText -Release $nvme98'; Why = "-UploadSetOnly no longer holds a re-assembly to the recorded pins" },
        @{ Find = '" -Nvme98SumsSha256 " + $nv.SumsSha256'; Why = "the repair command no longer carries the nvme98 pins" }
    )) {
        Assert-True ($nvRelText.Contains($anchor.Find)) ("make-release.ps1 no longer contains '" + $anchor.Find + "': " + $anchor.Why + ".")
    }
    $legCopyLine = @($nvRelText -split "`r?`n" | Where-Object { $_ -match '^\s*\$legCopy = ' })
    Assert-True ($legCopyLine.Count -eq 1 -and $legCopyLine[0] -notmatch '(?i)nvme98') `
        "make-release.ps1's copy into releases\<version>\ names nvme98 files; nothing third-party is tracked."

    # --- the x86 helper (task 36.15, design record 18) -------------
    Write-Step "the x86 package carries the helper its INF names, of its own flavour"
    $r = Invoke-Packager -NoHlprStandIn -Arguments @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", (Join-Path $script:work "pkg-no-helper"))
    Assert-True ($r.ExitCode -ne 0) "an x86 package was built with no xhcihlpr.sys for its INF's entry."
    Assert-True ($r.Output -match "xhcihlpr\.sys") ("expected the refusal to name xhcihlpr.sys. Output:`n" + $r.Output)
    $wrongHelper = Join-Path $script:work "xhcihlpr-wrong.sys"
    Set-Content -LiteralPath $wrongHelper -Value "not a real helper XHCI98_FLAVOUR_QEMU" -Encoding ASCII
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-HlprDriverPath", $wrongHelper,
        "-OutDir", (Join-Path $script:work "pkg-wrong-helper"))
    Assert-True ($r.ExitCode -ne 0) "a qemu-flavour helper was staged into a debug package."
    $r = Invoke-Packager @("-Arch", "amd64", "-InfPath", (Join-Path $repo "src\xhci98-amd64.inf"),
        "-DriverPath", $driver, "-HlprDriverPath", $wrongHelper, "-OutDir", (Join-Path $script:work "pkg-amd64-helper"))
    Assert-True ($r.ExitCode -ne 0) "an amd64 package accepted -HlprDriverPath."
    # Record 18 section 15.11: the helper is never packaged on its allowlist
    # alone, and a cut never publishes the retired 2.3.x helper. The binary
    # gates cannot run on stand-ins, so the refusals are held by their text,
    # each inside the block it guards.
    $hlprPkgText = [System.IO.File]::ReadAllText($packager)
    $hlprGateAt = $hlprPkgText.IndexOf('& powershell.exe @hlprGateArgs')
    $hlprRefuseAt = $hlprPkgText.IndexOf('-NoTargetEvidence: the x86 helper is never packaged on the allowlist alone')
    Assert-True ($hlprRefuseAt -gt 0 -and $hlprGateAt -gt $hlprRefuseAt -and ($hlprGateAt - $hlprRefuseAt) -lt 800) `
        "make-package.ps1 must refuse -NoTargetEvidence just before the helper's hlprx86 gate."
    $hlprRelText = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot "make-release.ps1"))
    Assert-True ($hlprRelText.Contains('$retiredHelper = "xhcimsi.sys"') -and
                 $hlprRelText.Contains('if (Test-Path -LiteralPath (Join-Path $destDir $retiredHelper))')) `
        "make-release.ps1 must refuse a leg holding xhcimsi.sys, the retired 2.3.x helper."
    Assert-True ($hlprRelText.Contains('-NoTargetEvidence: an x86 leg carries xhcihlpr.sys')) `
        "make-release.ps1 must refuse -NoTargetEvidence for an x86 leg."
    Assert-True ($hlprRelText.Contains('$legPublishableExtra = @("xhcihlpr.sys")')) `
        "make-release.ps1 must publish xhcihlpr.sys, not xhcimsi.sys, as an x86 leg's third file."

    # --- txtsetup.oem (roadmap task 33.3) -----------------------------------
    #
    # Staged at the package root when named, and gated there against the
    # staged INF: a file whose ids the INF does not bind is refused before
    # the package is published to -OutDir.
    Write-Step "txtsetup.oem is staged at the root and gated against the staged INF"
    $oemOut = Join-Path $script:work "pkg-oem"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OemPath", (Join-Path $repo "src\txtsetup.oem"), "-OutDir", $oemOut)
    Assert-True ($r.ExitCode -eq 0) ("a package with txtsetup.oem was rejected:`n" + $r.Output)
    Assert-True (Test-Path -LiteralPath (Join-Path $oemOut "txtsetup.oem")) "txtsetup.oem is not at the package root."
    $badOem = Join-Path $script:work "bad-txtsetup.oem"
    [System.IO.File]::WriteAllText($badOem,
        [System.IO.File]::ReadAllText((Join-Path $repo "src\txtsetup.oem")).Replace('"USB\XHCI98_ROOT_HUB"', '"USB\ROOT_HUB"'),
        (New-Object System.Text.ASCIIEncoding))
    $badOemOut = Join-Path $script:work "pkg-oem-bad"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OemPath", $badOem, "-OutDir", $badOemOut)
    Assert-True ($r.ExitCode -ne 0) "a txtsetup.oem mapping USB\ROOT_HUB was staged."
    Assert-True ($r.Output -match "check-txtsetup-oem") `
        ("expected the refusal to name the txtsetup.oem gate. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $badOemOut)) "a refused package left an output directory."
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OemPath", (Join-Path $repo "src\txtsetup-amd64.oem"), "-OutDir", (Join-Path $script:work "pkg-oem-arch"))
    Assert-True ($r.ExitCode -ne 0) "the amd64 txtsetup.oem was staged into an x86 package."

    # --- portsw.vbs (roadmap-hcd task 36.42) --------------------------------
    #
    # The port switchover's user script. Since task 37.5 no package carries
    # it or its two batch files: make-release.ps1 publishes the three once,
    # from src\, into portsw\ at the version root (held further down, with
    # fixtures), so the packager has no -PswScriptPath and stages none of
    # them. The script finds the controller's key by DriverDesc, so the
    # string it matches is held to both INFs' XhciDesc, and the text a
    # Windows 98 WSH reads is held to ASCII with CRLF line endings.
    Write-Step "no package carries portsw.vbs; it matches the INFs"
    $pswSrc = Join-Path $repo "src\portsw.vbs"
    foreach ($name in @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $flatOut $name))) `
            "a package carries $name, which since roadmap-hcd task 37.5 is published once in portsw\ and staged into no package."
    }
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-PswScriptPath", $pswSrc, "-OutDir", (Join-Path $script:work "pkg-psw-gone"))
    Assert-True ($r.ExitCode -ne 0) "make-package.ps1 still takes -PswScriptPath, so a package can carry the port switchover's scripts again (roadmap-hcd task 37.5)."
    $packagerSrcText = [System.IO.File]::ReadAllText($packager)
    foreach ($needle in @('$PswScriptPath', '"portsw.vbs"', '"portsw-on.bat"', '"portsw-off.bat"')) {
        Assert-True (-not $packagerSrcText.Contains($needle)) `
            "make-package.ps1 contains $needle again: since roadmap-hcd task 37.5 it stages none of the port switchover's scripts."
    }
    $pswBytes = [System.IO.File]::ReadAllBytes($pswSrc)
    $pswBad = @($pswBytes | Where-Object { $_ -ge 0x80 -or ($_ -lt 0x20 -and $_ -ne 9 -and $_ -ne 10 -and $_ -ne 13) })
    Assert-True ($pswBad.Count -eq 0) "src\portsw.vbs holds a byte outside printable ASCII, TAB, CR and LF."
    $pswText = [System.IO.File]::ReadAllText($pswSrc)
    Assert-True (-not ($pswText -match "(?<!`r)`n")) "src\portsw.vbs has a line not ended by CRLF."
    foreach ($infName in @("src\xhci98.inf", "src\xhci98-amd64.inf")) {
        $infText = [System.IO.File]::ReadAllText((Join-Path $repo $infName))
        $descMatch = [regex]::Match($infText, '(?m)^XhciDesc="([^"]*)"')
        Assert-True $descMatch.Success "$infName has no XhciDesc string this test can read."
        if ($descMatch.Success) {
            Assert-True ($pswText.Contains('Const XHCI_DESC = "' + $descMatch.Groups[1].Value + '"')) `
                "src\portsw.vbs does not look for $infName's XhciDesc, so it would find no controller."
        }
    }
    foreach ($needle in @('"XhciIntelPortSwitch"', '"REG_DWORD"',
                          'HKLM\System\CurrentControlSet\Services\Class\USB\',
                          'HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\',
                          'RESTART WINDOWS')) {
        Assert-True ($pswText.Contains($needle)) "src\portsw.vbs no longer contains $needle."
    }

    # --- portsw-on.bat and portsw-off.bat (roadmap-hcd task 36.56) ----------
    #
    # Published beside portsw.vbs, in portsw\ since task 37.5 and in
    # tools\portsw\ since task 38.9. Each file is
    # run by COMMAND.COM on Windows 98 SE and ME and by cmd.exe elsewhere
    # (design record 16 section 5a), so the split is pinned: COMMAND.COM must
    # reach no line it cannot parse, and the one cmd.exe line must sit where
    # only cmd.exe goes.
    Write-Step "portsw-on.bat and portsw-off.bat are COMMAND.COM-safe"
    $pswBatches = @(
        @{ Name = "portsw-on.bat";  Value = "1"; Others = @("0", "2") },
        @{ Name = "portsw-off.bat"; Value = "0"; Others = @("1", "2") }
    )

    # The files themselves. Every line is held to a short list of the commands
    # both interpreters run, rather than searched for constructs one of them
    # lacks: a list of forbidden words misses the one nobody listed (review
    # round 1 found four such escapes). The only lines outside that list are
    # the split's: the OS test, and the cd /d that only cmd.exe reaches.
    function Get-PswBatchFaults {
        param([string]$Text, [string]$Value)
        $faults = @()
        if ($Text -match '[^\x20-\x7E\r\n]') { $faults += "a byte outside printable ASCII, CR and LF" }
        if ($Text -match "(?<!`r)`n" -or $Text -match "`r(?!`n)") { $faults += "a line not ended by CRLF" }
        if (-not $Text.EndsWith("`r`n")) { $faults += "no CRLF after the last line, so COMMAND.COM may not run it"; return $faults }
        $lines = @($Text.Substring(0, $Text.Length - 2) -split "`r`n")
        if ($lines[0] -cne "@echo off") { $faults += "line 1 is not @echo off" }
        if ($lines[$lines.Count - 1] -cne "pause") { $faults += "the last line is not pause" }
        $code = @()
        for ($i = 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -cnotmatch '^rem( |$)') { $code += $i }
        }
        $split = @('if "%OS%"=="Windows_NT" goto nt', 'goto run', ':nt', 'cd /d "%~dp0"', ':run')
        for ($k = 0; $k -lt $split.Count; $k++) {
            if ($code.Count -le $k -or $lines[$code[$k]] -cne $split[$k]) {
                $faults += ("command " + ($k + 1) + " after @echo off is not '" + $split[$k] + "'")
            }
        }
        $osLine = -1
        $ntLine = -1
        if ($code.Count -gt 0) { $osLine = $code[0] }
        if ($code.Count -gt 3) { $ntLine = $code[3] }
        $allowed = @('^rem( [ -~]*)?$', '^echo( [ -~]*|\.)$', '^:[a-z]+$', '^goto [a-z]+$', '^pause$',
                     '^if not exist portsw\.vbs goto [a-z]+$', '^if errorlevel [0-9] goto [a-z]+$',
                     '^if not "%OS%"=="Windows_NT" goto [a-z]+$', '^cscript //nologo portsw\.vbs (show|[0-9])$')
        $labels = @{}
        $gotos = @()
        for ($i = 1; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            if ($line.Length -gt 127) { $faults += "line $($i + 1) is past COMMAND.COM's 127 characters" }
            if ($i -ne $osLine -and $i -ne $ntLine) {
                $ok = $false
                foreach ($p in $allowed) { if ($line -cmatch $p) { $ok = $true; break } }
                if (-not $ok) { $faults += "line $($i + 1) is not one of the commands both interpreters run: $line" }
            }
            if ($line -cmatch '^(rem|echo)\b' -and $line -match '[<>|&%^]') {
                $faults += "line $($i + 1) has a redirection, pipe or percent character on an echo or rem line: $line"
            }
            if ($line -match '^echo +(on|off) *$') { $faults += "line $($i + 1) is an echo COMMAND.COM reads as a switch: $line" }
            if ($line -cmatch '^:([a-z]+)$') {
                if ($labels.ContainsKey($Matches[1])) { $faults += "label $($Matches[1]) twice" }
                $labels[$Matches[1]] = $i
            }
            if ($line -cmatch '^(if .* )?goto ([a-z]+)$') { $gotos += , @($i, $Matches[2]) }
        }
        foreach ($g in $gotos) {
            if (-not $labels.ContainsKey($g[1])) { $faults += "line $($g[0] + 1) jumps to a label the file does not have: $($g[1])" }
            if ($g[1] -ceq "nt" -and $g[0] -ne $osLine) { $faults += "line $($g[0] + 1) is a goto nt that COMMAND.COM could take" }
        }
        $sets = @($lines | Where-Object { $_ -cmatch '^cscript //nologo portsw\.vbs [0-9]$' })
        if ($sets.Count -ne 1 -or $sets[0] -cne ('cscript //nologo portsw.vbs ' + $Value)) {
            $faults += "it does not set exactly one value, $Value (2 is a hand edit)"
        }
        $last = -1
        foreach ($pin in @('if not exist portsw.vbs goto nofile', 'cscript //nologo portsw.vbs show',
                           'if errorlevel 1 goto end', ('cscript //nologo portsw.vbs ' + $Value),
                           'if errorlevel 3 goto elevate', 'if errorlevel 1 goto refused')) {
            $at = [Array]::IndexOf($lines, $pin)
            if ($at -le $last) { $faults += "'$pin' is missing or out of order" } else { $last = $at }
        }
        foreach ($needle in @('Vista and 7: right-click it', 'choose Run as administrator')) {
            if (-not $Text.Contains($needle)) { $faults += "it no longer says '$needle'" }
        }
        # portsw.vbs says to restart after a set; an echo of its own printed it twice (task 38.6).
        if ($Text.Contains('echo RESTART WINDOWS')) { $faults += "it echoes 'RESTART WINDOWS', which portsw.vbs already prints" }
        return $faults
    }

    $batLineCounts = @()
    foreach ($bat in $pswBatches) {
        $batSrc = Join-Path $repo ("src\" + $bat.Name)
        if (-not (Test-Path -LiteralPath $batSrc)) {
            Assert-True $false "src\$($bat.Name) is missing."
            continue
        }
        # Bytes, then Latin-1: ReadAllText would consume a BOM, or decode a
        # UTF-16 file into clean text (review round 2).
        $batText = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($batSrc))
        $faults = @(Get-PswBatchFaults -Text $batText -Value $bat.Value)
        Assert-True ($faults.Count -eq 0) ("src\$($bat.Name):`n  " + ($faults -join "`n  "))
        $batLineCounts += @($batText -split "`r`n").Count
    }
    if ($batLineCounts.Count -eq 2) {
        Assert-True ($batLineCounts[0] -eq $batLineCounts[1]) "portsw-on.bat and portsw-off.bat no longer have the same number of lines; they are one file but for the value."
    }

    # Controls on the checker itself: each of these must be refused, and the
    # last, harmless echo text that names a cmd.exe word, must not be.
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $onBytes = [System.IO.File]::ReadAllBytes((Join-Path $repo "src\portsw-on.bat"))
    $onText = $latin1.GetString($onBytes)
    $anchor = "`r`necho Now:`r`n"
    Assert-True ($onText.Contains($anchor)) "src\portsw-on.bat has no 'echo Now:' line for the checker's controls to replace."
    foreach ($bad in @('goto nt', '@echo example > stray.txt', ' rem example > stray.txt', 'echo x | more',
                       'for /f %%i in (a.txt) do echo %%i', 'setlocal', 'if /i errorlevel 1 goto end',
                       'cd /d "%~dp0"', 'goto eof', 'cscript //nologo "portsw.vbs" 2', 'cscript //nologo portsw.vbs 2',
                       'echo off', 'echo ON', 'echo Off', 'call :run', ':run', ('echo ' + ('x' * 130)),
                       'echo RESTART WINDOWS for the change to take effect.')) {
        $mutant = $onText.Replace($anchor, "`r`n" + $bad + "`r`n")
        Assert-True (@(Get-PswBatchFaults -Text $mutant -Value "1").Count -gt 0) "the batch-file checker accepted the line: $bad"
    }
    foreach ($mutant in @($onText.Replace("goto run`r`n:nt`r`n", ":nt`r`n"),
                          $onText.Replace($anchor, "`r`necho caf" + [char]0xE9 + "`r`n"),
                          $latin1.GetString([byte[]](@(0xEF, 0xBB, 0xBF) + $onBytes)),
                          $latin1.GetString([byte[]](@(0xFF, 0xFE) + [System.Text.Encoding]::Unicode.GetBytes($onText))),
                          $onText.Replace("`r`n", "`n"),
                          $onText.Substring(0, $onText.Length - 2),
                          $onText.Replace("cscript //nologo portsw.vbs 1", "cscript //nologo portsw.vbs 0"))) {
        Assert-True (@(Get-PswBatchFaults -Text $mutant -Value "1").Count -gt 0) "the batch-file checker accepted a broken whole-file control."
    }
    $harmless = $onText.Replace($anchor, "`r`necho setlocal is not used here.`r`n")
    $harmlessFaults = @(Get-PswBatchFaults -Text $harmless -Value "1")
    Assert-True ($harmlessFaults.Count -eq 0) ("the batch-file checker refused harmless echo text:`n  " + ($harmlessFaults -join "`n  "))

    # --- the silent install's files (roadmap-hcd task 38.2) -----------------
    #
    # SILENT.REG, SILENT.VBS, silent-on.bat and silent-off.bat in
    # tools\silent\ (design record 13 section 6.11.8). The batches are run by
    # COMMAND.COM on Windows 98 SE and ME and by cmd.exe elsewhere, so they
    # are held as portsw-on.bat's are: every line from a short list both
    # interpreters run, and a pinned split whose cmd.exe branch only says it
    # changed nothing. silent-on.bat merges SILENT.REG before it sets 1, and
    # silent-off.bat never merges it. SILENT.REG is held to its six values.
    # The two checkers are package-common.ps1's, which make-release.ps1 also
    # holds a staged or published tools\silent\ to (Assert-SilentInstallContent).
    Write-Step "silent-on.bat and silent-off.bat are COMMAND.COM-safe; SILENT.REG is the six values"
    $silentBatches = @(
        @{ Name = "silent-on.bat";  Value = "1" },
        @{ Name = "silent-off.bat"; Value = "0" }
    )
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    foreach ($bat in $silentBatches) {
        $batSrc = Join-Path $repo ("src\" + $bat.Name)
        if (-not (Test-Path -LiteralPath $batSrc)) { Assert-True $false "src\$($bat.Name) is missing."; continue }
        $batText = $latin1.GetString([System.IO.File]::ReadAllBytes($batSrc))
        $faults = @(Get-SilentBatchFaults -Text $batText -Value $bat.Value)
        Assert-True ($faults.Count -eq 0) ("src\$($bat.Name):`n  " + ($faults -join "`n  "))
    }

    # Controls on the checker: each mutant of silent-on.bat (and one of
    # silent-off.bat) must be refused, and harmless echo text must not be.
    $sOnBytes = [System.IO.File]::ReadAllBytes((Join-Path $repo "src\silent-on.bat"))
    $sOnText = $latin1.GetString($sOnBytes)
    $sAnchor = "`r`necho Now:`r`n"
    Assert-True ($sOnText.Contains($sAnchor)) "src\silent-on.bat has no 'echo Now:' line for the checker's controls to replace."
    foreach ($bad in @('goto nt', '@echo example > stray.txt', 'echo x | more', 'setlocal', 'cd /d "%~dp0"',
                       'goto eof', 'cscript //nologo silent.vbs 2', 'cscript //nologo silent.vbs 0',
                       'regedit /s other.reg', 'regedit /s silent.reg', 'reg add HKLM\x', 'echo off',
                       'call :run', ':run', ('echo ' + ('x' * 130)))) {
        $mutant = $sOnText.Replace($sAnchor, "`r`n" + $bad + "`r`n")
        Assert-True (@(Get-SilentBatchFaults -Text $mutant -Value "1").Count -gt 0) "the silent batch checker accepted the line: $bad"
    }
    foreach ($mutant in @($sOnText.Replace("goto run`r`n:nt`r`n", ":nt`r`n"),
                          $sOnText.Replace("`r`n:nt`r`necho ", "`r`n:nt`r`ncscript //nologo silent.vbs 1`r`necho "),
                          $sOnText.Replace($sAnchor, "`r`necho caf" + [char]0xE9 + "`r`n"),
                          $latin1.GetString([byte[]](@(0xEF, 0xBB, 0xBF) + $sOnBytes)),
                          $sOnText.Replace("`r`n", "`n"),
                          $sOnText.Substring(0, $sOnText.Length - 2),
                          $sOnText.Replace("regedit /s silent.reg`r`n", ""),
                          $sOnText.Replace("if errorlevel 4 goto nolist`r`n", ""),
                          $sOnText.Replace("cscript //nologo silent.vbs 1", "cscript //nologo silent.vbs 0"))) {
        Assert-True (@(Get-SilentBatchFaults -Text $mutant -Value "1").Count -gt 0) "the silent batch checker accepted a broken whole-file control of silent-on.bat."
    }
    # The merge moved after the set: the order pin must refuse it.
    $sMoved = $sOnText.Replace("regedit /s silent.reg`r`n", "").Replace("cscript //nologo silent.vbs 1`r`n", "cscript //nologo silent.vbs 1`r`nregedit /s silent.reg`r`n")
    Assert-True (@(Get-SilentBatchFaults -Text $sMoved -Value "1").Count -gt 0) "the silent batch checker accepted silent-on.bat merging SILENT.REG after it sets the switch."
    $sOffText = $latin1.GetString([System.IO.File]::ReadAllBytes((Join-Path $repo "src\silent-off.bat")))
    foreach ($mutant in @($sOffText.Replace("cscript //nologo silent.vbs 0`r`n", "regedit /s silent.reg`r`ncscript //nologo silent.vbs 0`r`n"),
                          $sOffText.Replace("SilentClasses key", "key"),
                          $sOffText.Replace("cscript //nologo silent.vbs 0", "cscript //nologo silent.vbs 1"))) {
        Assert-True (@(Get-SilentBatchFaults -Text $mutant -Value "0").Count -gt 0) "the silent batch checker accepted a broken whole-file control of silent-off.bat."
    }
    $sHarmless = $sOnText.Replace($sAnchor, "`r`necho setlocal and regedit are words here.`r`n")
    $sHarmlessFaults = @(Get-SilentBatchFaults -Text $sHarmless -Value "1")
    Assert-True ($sHarmlessFaults.Count -eq 0) ("the silent batch checker refused harmless echo text:`n  " + ($sHarmlessFaults -join "`n  "))

    # SILENT.REG: held to package-common.ps1's Get-SilentRegFaults.
    $regSrc = Join-Path $repo "src\silent.reg"
    $regText = $latin1.GetString([System.IO.File]::ReadAllBytes($regSrc))
    $regFaults = @(Get-SilentRegFaults -Text $regText)
    Assert-True ($regFaults.Count -eq 0) ("src\silent.reg:`n  " + ($regFaults -join "`n  "))
    foreach ($mutant in @($regText.Replace("hex:c3,08,06,50", "hex:c3,08,06,62"),
                          $regText.Replace("hex:80,09,00,00", "hex:00,09,00,00"),
                          $regText.Replace("`"5`"=hex:83,03,01,02`r`n", ""),
                          $regText.Replace("`"5`"=hex:83,03,01,02`r`n", "`"5`"=hex:83,03,01,02`r`n`"6`"=hex:c3,08,06,62`r`n"),
                          $regText.Replace("`"0`"=", "`"9`"="),
                          $regText.Replace("REGEDIT4", "Windows Registry Editor Version 5.00"),
                          $regText.Replace("`r`n", "`n"))) {
        Assert-True (@(Get-SilentRegFaults -Text $mutant).Count -gt 0) "the SILENT.REG checker accepted a changed list."
    }

    # --- the field reports' batch files (roadmap-hcd task 38.1) -------------
    #
    # SNAPON.BAT, SNAPGET.BAT and SNAPOFF.BAT beside XHCISNAP.EXE, run by
    # COMMAND.COM on Windows 98 SE and ME and by cmd.exe from 2000 to 7, so
    # they carry portsw-on.bat's pinned split; QUALREP.BAT beside
    # XHCIQUAL.EXE, run by MS-DOS's COMMAND.COM alone, so it carries none.
    # Each line is held to a short list of what both interpreters run, as
    # portsw-on.bat's are. Each batch runs its tool once, by name, with the
    # one command line the task fixes - SNAPON.BAT's and SNAPGET.BAT's end in
    # %1, passed on as it came for the tool to default and check (owner,
    # 2026-10-13: an optional level, an optional name; Codex review of 38.1,
    # round 3: no `if` on it); no batch names a drive path or reads a
    # percent variable other than that one %1 (COMMAND.COM on 98 SE and ME
    # has no %DATE% or %TIME%, and
    # cmd.exe's follow the locale, which is why the tools build the names);
    # and SNAPGET.BAT never turns the channel off. The specs and
    # Get-ReportBatchFaults are package-common.ps1's, the one definition
    # make-release.ps1 also holds staged and published batches to.
    Write-Step "SNAPON, SNAPGET, SNAPOFF and QUALREP are COMMAND.COM-safe"
    $reportBatchSpecs = Get-ReportBatchSpecs

    foreach ($spec in $reportBatchSpecs) {
        $batSrc = Join-Path $repo ($spec.Dir + "\" + $spec.Name)
        if (-not (Test-Path -LiteralPath $batSrc -PathType Leaf)) {
            Assert-True $false "$($spec.Dir)\$($spec.Name) is missing (roadmap-hcd task 38.1)."
            continue
        }
        # Bytes, then Latin-1, for the reason portsw-on.bat's are read so.
        $batText = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($batSrc))
        $faults = @(Get-ReportBatchFaults -Text $batText -Spec $spec)
        Assert-True ($faults.Count -eq 0) ("$($spec.Dir)\$($spec.Name):`n  " + ($faults -join "`n  "))
    }

    # Controls on the checker, on fixtures written to the task's terms here
    # rather than on the files, so they hold whatever the files' wording: each
    # good fixture must pass, every mutant be refused, and the harmless echo
    # text pass.
    function New-ReportBatchFixture {
        param([hashtable]$Spec)
        $l = @("@echo off", ("rem " + $Spec.Name.ToUpperInvariant() + " - a fixture of test-package.ps1."))
        if ($Spec.Win) { $l += @('if "%OS%"=="Windows_NT" goto nt', 'goto run', ':nt', 'cd /d "%~dp0"', ':run') }
        if ($Spec.ContainsKey("ToolLast")) {
            # The tool runs last and prints the outcome, its last line what
            # to attach (owner, 2026-10-13); with ArgPass, Args ends in %1,
            # passed on as it came for the tool to default and check.
            if ($Spec.Win) { $l += "echo On Vista and 7: right-click it and choose Run as administrator." }
            if ($Spec.Names.Count -gt 0) { $l += ("echo Attach the " + ($Spec.Names -join " and ") + " files XHCISNAP names.") }
            $l += @(("if exist " + $Spec.Tool + ".EXE goto tool"), ("echo " + $Spec.Tool + ".EXE was not found in this directory."),
                    "goto end", ":tool", ($Spec.Tool + " " + $Spec.Args), ":end", "pause")
            return (($l -join "`r`n") + "`r`n")
        }
        $l += ("if not exist " + $Spec.Tool + ".EXE goto nofile")
        $l += ($Spec.Tool + " " + $Spec.Args)
        $l += "if errorlevel 4 goto failed"
        if ($Spec.Win) { $l += "echo On Vista and 7: right-click it and choose Run as administrator." }
        if ($Spec.Names.Count -gt 0) { $l += ("echo Attach the " + ($Spec.Names -join " and ") + " files named above.") }
        $l += @("goto end", ":failed", "echo It did not finish. Read the lines above.", "goto end",
                ":nofile", ("echo " + $Spec.Tool + ".EXE was not found in this directory."), ":end", "echo.", "pause")
        return (($l -join "`r`n") + "`r`n")
    }
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    foreach ($spec in $reportBatchSpecs) {
        $fx = New-ReportBatchFixture -Spec $spec
        $fxFaults = @(Get-ReportBatchFaults -Text $fx -Spec $spec)
        Assert-True ($fxFaults.Count -eq 0) ("the report-batch checker refused its own good $($spec.Name) fixture:`n  " + ($fxFaults -join "`n  "))
        $runLine = $spec.Tool + " " + $spec.Args
        $at = "`r`n" + $runLine + "`r`n"
        $mutants = @(
            @{ Why = "a UTF-8 BOM";      Text = $latin1.GetString([byte[]](@(0xEF, 0xBB, 0xBF) + $latin1.GetBytes($fx))) },
            @{ Why = "UTF-16";           Text = $latin1.GetString([byte[]](@(0xFF, 0xFE) + [System.Text.Encoding]::Unicode.GetBytes($fx))) },
            @{ Why = "LF endings";       Text = $fx.Replace("`r`n", "`n") },
            @{ Why = "no last CRLF";     Text = $fx.Substring(0, $fx.Length - 2) },
            @{ Why = "truncated";        Text = $fx.Substring(0, $fx.IndexOf(":end`r`n")) },
            @{ Why = "no pause";         Text = $fx.Replace("`r`npause`r`n", "`r`n") },
            @{ Why = "no @echo off";     Text = $fx.Substring("@echo off`r`n".Length) },
            @{ Why = "a %DATE% line";    Text = $fx.Replace($at, "`r`nset STAMP=%DATE%`r`n" + $runLine + "`r`n") },
            @{ Why = "%TIME% in its run"; Text = $fx.Replace($at, "`r`n" + $runLine + " %TIME%`r`n") },
            @{ Why = "another %1 in its run"; Text = $fx.Replace($at, "`r`n" + $runLine + " %1`r`n") },
            @{ Why = "a redirect";       Text = $fx.Replace($at, "`r`n" + $runLine + " > OUT.TXT`r`n") },
            @{ Why = "a C:\ write";      Text = $fx.Replace($at, "`r`n" + $spec.Tool + " -o C:\DUMP`r`n") },
            @{ Why = "a cd to the root"; Text = $fx.Replace($at, "`r`ncd \`r`n" + $runLine + "`r`n") },
            @{ Why = "run twice";        Text = $fx.Replace($at, $at + $runLine + "`r`n") },
            @{ Why = "never run";        Text = $fx.Replace($at, "`r`n") },
            @{ Why = "run by path";      Text = $fx.Replace($at, "`r`n.\" + $runLine + "`r`n") },
            @{ Why = "echo on";          Text = $fx.Replace($at, $at + "echo on`r`n") },
            @{ Why = "a pipe on echo";   Text = $fx.Replace($at, $at + "echo x | more`r`n") },
            @{ Why = "a missing label";  Text = $fx.Replace($at, $at + "if errorlevel 1 goto nowhere`r`n") },
            @{ Why = "a label twice";    Text = $fx.Replace(":end`r`n", ":end`r`n:end`r`n") },
            @{ Why = "a long line";      Text = $fx.Replace($at, $at + "echo " + ("x" * 130) + "`r`n") },
            @{ Why = "setlocal";         Text = $fx.Replace($at, "`r`nsetlocal`r`n" + $runLine + "`r`n") },
            @{ Why = "call";             Text = $fx.Replace($at, "`r`ncall :end`r`n" + $runLine + "`r`n") }
        )
        if ($spec.Win) {
            $mutants += @{ Why = "no split"; Text = $fx.Replace("goto run`r`n:nt`r`n", ":nt`r`n") }
            $mutants += @{ Why = "a stray goto nt"; Text = $fx.Replace($at, $at + "goto nt`r`n") }
        } else {
            $mutants += @{ Why = "a cmd.exe cd /d"; Text = $fx.Replace($at, "`r`ncd /d `"%~dp0`"`r`n" + $runLine + "`r`n") }
        }
        if ($spec.Name -eq "snapget.bat") {
            $mutants += @{ Why = "-disable for -report"; Text = $fx.Replace($at, "`r`nXHCISNAP -disable`r`n") }
            $mutants += @{ Why = "-disable after -report"; Text = $fx.Replace($at, $at + "XHCISNAP -disable`r`n") }
            $mutants += @{ Why = "-verbosity 0 after -report"; Text = $fx.Replace($at, $at + "XHCISNAP -verbosity 0`r`n") }
            $mutants += @{ Why = "no .DEV named"; Text = $fx.Replace("Attach the .DEV and .TXT", "Attach the .TXT") }
        }
        if ($spec.Name -eq "QUALREP.BAT") {
            $mutants += @{ Why = "--log for --log-stamp"; Text = $fx.Replace($at, "`r`nXHCIQUAL --probe-only --no-page --log PROBE.LOG`r`n") }
            $mutants += @{ Why = "an active run"; Text = $fx.Replace($at, "`r`nXHCIQUAL --no-page --log-stamp`r`n") }
            $mutants += @{ Why = "another done flag"; Text = $fx.Replace($at, "`r`n" + $runLine + " --done-flag OTHER.FLG`r`n") }
            $mutants += @{ Why = "a log deleted"; Text = $fx.Replace($at, $at + "del *.LOG`r`n") }
            $mutants += @{ Why = "another file deleted"; Text = $fx.Replace($at, $at + "if exist OTHER.FLG del OTHER.FLG`r`n") }
        }
        if ($spec.Name -eq "snapon.bat") {
            $mutants += @{ Why = "verbosity 3"; Text = $fx.Replace($at, "`r`nXHCISNAP -verbosity 3`r`n") }
            $mutants += @{ Why = "-verbosity %1 for -on %1"; Text = $fx.Replace($at, "`r`nXHCISNAP -verbosity %1`r`n") }
            $mutants += @{ Why = "a second -on line"; Text = $fx.Replace($at, $at + "XHCISNAP -on 2`r`n") }
            $mutants += @{ Why = "a level test routed around"; Text = $fx.Replace($at, "`r`nif `"%1`"==`"0`" goto end`r`n" + $runLine + "`r`n") }
        }
        if ($spec.Name -eq "snapget.bat") {
            $mutants += @{ Why = "-report -o %1 for -report %1"; Text = $fx.Replace($at, "`r`nXHCISNAP -report -o %1`r`n") }
            $mutants += @{ Why = "a second -report line"; Text = $fx.Replace($at, "`r`nif errorlevel 2 goto end`r`n" + $runLine + "`r`nXHCISNAP -report`r`n") }
        }
        if ($spec.ContainsKey("ArgPass")) {
            # %1 passes on the one run line alone, as it came, and nothing
            # tests it; %2, %0, %*, %~1, %DATE% and %1 anywhere else are
            # refused (Codex review of 38.1, round 3).
            $mutants += @{ Why = "an if on %1";          Text = $fx.Replace($at, "`r`nif `"%1`"==`"`" goto end`r`n" + $runLine + "`r`n") }
            $mutants += @{ Why = "an if not on %1";      Text = $fx.Replace($at, $at + "if not `"%1`"==`"`" goto end`r`n") }
            $mutants += @{ Why = "an if on %1 unquoted"; Text = $fx.Replace($at, "`r`nif %1.==. goto end`r`n" + $runLine + "`r`n") }
            $mutants += @{ Why = "%2 for %1";            Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "%2") + "`r`n") }
            $mutants += @{ Why = "%1 and %2";            Text = $fx.Replace($at, "`r`n" + $runLine + " %2`r`n") }
            $mutants += @{ Why = "%0 for %1";            Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "%0") + "`r`n") }
            $mutants += @{ Why = "%* for %1";            Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "%*") + "`r`n") }
            $mutants += @{ Why = "%~1 for %1";           Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "%~1") + "`r`n") }
            $mutants += @{ Why = "%DATE% for %1";        Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "%DATE%") + "`r`n") }
            $mutants += @{ Why = "a %2 test";            Text = $fx.Replace($at, "`r`nif `"%2`"==`"`" goto end`r`n" + $runLine + "`r`n") }
            $mutants += @{ Why = "%1 on an echo";        Text = $fx.Replace($at, $at + "echo saved as %1`r`n") }
            $mutants += @{ Why = "set from %1";          Text = $fx.Replace($at, "`r`nset N=%1`r`n" + $runLine + "`r`n") }
            $mutants += @{ Why = "shift";                Text = $fx.Replace($at, "`r`nshift`r`n" + $runLine + "`r`n") }
            $mutants += @{ Why = "no %1 passed";         Text = $fx.Replace($at, "`r`n" + $runLine.Replace(" %1", "") + "`r`n") }
            $mutants += @{ Why = "%1 quoted in its run"; Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "`"%1`"") + "`r`n") }
            $mutants += @{ Why = "%1 by path";           Text = $fx.Replace($at, "`r`n" + $runLine.Replace("%1", "C:\%1") + "`r`n") }
            $mutants += @{ Why = "%1 not last";          Text = $fx.Replace($at, "`r`n" + $runLine + " -c 0`r`n") }
        } else {
            $mutants += @{ Why = "a %1 test";            Text = $fx.Replace($at, "`r`nif `"%1`"==`"`" goto end`r`n" + $runLine + "`r`n") }
        }
        if ($spec.ContainsKey("ToolLast")) {
            # Nothing reads the exit code and nothing follows the tool line
            # but labels and pause (owner, 2026-10-13: XHCISNAP's last line
            # names what to attach); nothing but rem and echo before the
            # 'if exist' test, so no jump routes around the tool.
            $guardLine = "if exist " + $spec.Tool + ".EXE goto tool"
            $mutants += @{ Why = "an if errorlevel after the tool"; Text = $fx.Replace($at, $at + "if errorlevel 1 goto end`r`n") }
            $mutants += @{ Why = "an echo after the tool";          Text = $fx.Replace($at, $at + "echo Done.`r`n") }
            $mutants += @{ Why = "an echo before pause";            Text = $fx.Replace(":end`r`npause`r`n", ":end`r`necho.`r`npause`r`n") }
            $mutants += @{ Why = "a goto after the tool";           Text = $fx.Replace($at, $at + "goto end`r`n") }
            $mutants += @{ Why = "an exit-code ladder";             Text = $fx.Replace($at, $at + "if errorlevel 2 goto bad`r`ngoto end`r`n:bad`r`necho Refused.`r`n") }
            $mutants += @{ Why = "an if errorlevel before the tool"; Text = $fx.Replace("`r`n:tool`r`n", "`r`nif errorlevel 1 goto end`r`n:tool`r`n") }
            $mutants += @{ Why = "a goto around the guard";         Text = $fx.Replace("`r`n" + $guardLine + "`r`n", "`r`ngoto end`r`n" + $guardLine + "`r`n") }
            $mutants += @{ Why = "no guard";                        Text = $fx.Replace("`r`n" + $guardLine + "`r`n", "`r`n") }
            $mutants += @{ Why = "the guard to another label";      Text = $fx.Replace("`r`n" + $guardLine + "`r`n", "`r`nif exist " + $spec.Tool + ".EXE goto end`r`n") }
            $mutants += @{ Why = "if not exist for the guard";      Text = $fx.Replace("`r`n" + $guardLine + "`r`n", "`r`nif not exist " + $spec.Tool + ".EXE goto tool`r`n") }
            $mutants += @{ Why = "a second way to the tool";        Text = $fx.Replace("`r`ngoto end`r`n:tool`r`n", "`r`ngoto tool`r`n:tool`r`n") }
            $mutants += @{ Why = "the tool not below its label";    Text = $fx.Replace(":tool`r`n" + $runLine + "`r`n", ":tool`r`necho Running.`r`n" + $runLine + "`r`n") }
            $harmless = $fx.Replace("`r`n" + $guardLine + "`r`n", "`r`necho setlocal is not used here, and the date comes from XHCISNAP.`r`n" + $guardLine + "`r`n")
        } else {
            $harmless = $fx.Replace($at, $at + "echo setlocal is not used here, and the date comes from XHCISNAP.`r`n")
        }
        # After every control is built, the ToolLast ones included (Codex
        # review of 38.1, round 4: they were appended after this loop).
        foreach ($m in $mutants) {
            Assert-True ($m.Text -cne $fx) "the $($spec.Name) control '$($m.Why)' did not change the fixture, so it controls nothing."
            Assert-True (@(Get-ReportBatchFaults -Text $m.Text -Spec $spec).Count -gt 0) "the report-batch checker accepted $($spec.Name) with $($m.Why)."
        }
        $harmlessFaults = @(Get-ReportBatchFaults -Text $harmless -Spec $spec)
        Assert-True ($harmlessFaults.Count -eq 0) ("the report-batch checker refused harmless echo text in $($spec.Name):`n  " + ($harmlessFaults -join "`n  "))
        if ($spec.ContainsKey("Flag")) {
            $withFlag = $fx.Replace($at, ("`r`nif exist {0} del {0}`r`nif exist {0} goto failed`r`n{1} --done-flag {0}`r`nif not exist {0} goto failed`r`ndel {0}`r`n" -f $spec.Flag, $runLine))
            $flagFaults = @(Get-ReportBatchFaults -Text $withFlag -Spec $spec)
            Assert-True ($flagFaults.Count -eq 0) ("the report-batch checker refused $($spec.Name)'s own completion flag:`n  " + ($flagFaults -join "`n  "))
        }
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

    # --- the HCD's build scaffold must never become install media (25.8) ---
    # A correct flavour marker must not let it through.
    Write-Step "the HCD build scaffold is refused"
    $scaffoldDriver = Join-Path $srcDir "xhci98-scaffold.sys"
    Set-Content -LiteralPath $scaffoldDriver -Value `
        "stand-in XHCI98_SCAFFOLD_DO_NOT_STAGE XHCI98_FLAVOUR_DEBUG driver" `
        -Encoding ASCII
    $scaffoldOut = Join-Path $script:work "pkg-scaffold"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $scaffoldDriver,
        "-OutDir", $scaffoldOut)
    Assert-True ($r.ExitCode -ne 0) "the HCD build scaffold was accepted for packaging."
    Assert-True ($r.Output -match "build scaffold") `
        ("expected the rejection to identify the HCD scaffold. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $scaffoldOut)) `
        "the output directory was created for the rejected HCD scaffold."

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
    $hlprStandInDebug = Join-Path $script:work "xhcihlpr-relpkg.sys"
    Set-Content -LiteralPath $hlprStandInDebug -Value "not a real helper XHCI98_FLAVOUR_DEBUG" -Encoding ASCII
    $script = @(
        "Set-Location -LiteralPath '$cwd'",
        "& '$packager' -InfPath '$plainInf' -DriverPath '$driver' -HlprDriverPath '$hlprStandInDebug' -OutDir 'relpkg' -SkipBinaryGates | Out-Null",
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
            # The x86 media's third file of this project's own since task
            # 36.15 (design record 18), published beside xhci98.sys.
            Set-Content -LiteralPath (Join-Path $pubFlavour "xhcihlpr.sys") -Encoding ASCII `
                        -Value "stand-in xhcihlpr.sys, $fl flavour, $relVersion"

            # The gated package: the same two files byte for byte, and since
            # 1.0.0.1 nothing else.
            $pkgFlavour = Join-Path $pkgRoot ("pkg-" + $fl)
            Ensure-Directory $pkgFlavour
            [System.IO.File]::WriteAllBytes((Join-Path $pkgFlavour "xhci98.sys"), $flavourBytes[$fl])
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pkgFlavour "xhci98.inf") -Force
            Set-Content -LiteralPath (Join-Path $pkgFlavour "xhcihlpr.sys") -Encoding ASCII `
                        -Value "stand-in xhcihlpr.sys, $fl flavour, $relVersion"
        }
        Set-Content -LiteralPath (Join-Path $pubRoot "readme.txt") -Encoding ASCII `
                    -Value "stand-in per-version readme"
        Copy-Item -LiteralPath (Join-Path $repo "LICENSE") `
                  -Destination (Join-Path $pubRoot "LICENSE") -Force
        # The tools under tools\, as every cut publishes them since roadmap-hcd
        # task 38.9: the tree is the current version's, which is 2.4.0.0 or
        # later from the cut on, and the upload assembly refuses a tree of that
        # version without tools\ (Assert-ToolsLayout). A tree that has tools\
        # is held to it at any version, so this fixture passes before the
        # version is stamped too.
        Ensure-Directory (Join-Path $pubRoot "tools\xhciqual")
        Set-Content -LiteralPath (Join-Path $pubRoot "tools\xhciqual\XHCIQUAL.EXE") `
                    -Encoding ASCII -Value "stand-in qualifier"
        # And its report batch beside it (roadmap-hcd task 38.1): the upload
        # assembly refuses a tool directory without its batches
        # (Assert-ReportBatchLayout), and one whose text the shared checker
        # refuses (Assert-ReportBatchContent), so it is the repository's own.
        Copy-Item -LiteralPath (Join-Path $repo "xhciqual\QUALREP.BAT") `
                  -Destination (Join-Path $pubRoot "tools\xhciqual\QUALREP.BAT") -Force
        # The port switchover's scripts in tools\portsw\, as every cut publishes
        # them since roadmap-hcd tasks 37.5 and 38.9: the upload assembly
        # refuses a tree of this version without them (Assert-PortSwitchLayout).
        Ensure-Directory (Join-Path $pubRoot "tools\portsw")
        foreach ($name in @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")) {
            Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $pubRoot "tools\portsw\$name") -Force
        }
        # The silent install's four files in tools\silent\ (task 38.2).
        Ensure-Directory (Join-Path $pubRoot "tools\silent")
        foreach ($pair in @(@("silent.reg", "SILENT.REG"), @("silent.vbs", "SILENT.VBS"), @("silent-on.bat", "silent-on.bat"), @("silent-off.bat", "silent-off.bat"))) {
            Copy-Item -LiteralPath (Join-Path $repo ("src\" + $pair[0])) -Destination (Join-Path $pubRoot ("tools\silent\" + $pair[1])) -Force
        }

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
            foreach ($name in @("xhci98.sys", "xhci98.inf", "xhcihlpr.sys")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from the upload set's $fl\ directory; a user pointing Windows at it gets an incomplete install."
            }
        }
        # And the published tree's own files are at the top level, not one
        # directory further down. A nested <version>\ directory here is the
        # exact defect: every file present, and none of them together.
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $uploadDir $relVersion))) `
            "the upload set nests the published tree as '$relVersion\'; the release root's contents belong at the top level."
        foreach ($name in @("readme.txt", "LICENSE", "tools\xhciqual\XHCIQUAL.EXE", "tools\portsw\portsw.vbs")) {
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

        # --- a malformed report batch in the published tree (task 38.1) -----
        #
        # The layout checks see names and places only, so a QUALREP.BAT in
        # the right place that the shared checker refuses (here: LF endings,
        # which COMMAND.COM misreads) must stop the assembly before anything
        # is archived (Codex review of 38.1, round 1).
        Write-Step "the upload set refuses a malformed report batch in the published tree"
        $qrPub = Join-Path $pubRoot "tools\xhciqual\QUALREP.BAT"
        $qrGood = [System.IO.File]::ReadAllBytes($qrPub)
        $qrBad = [System.Text.Encoding]::ASCII.GetString($qrGood).Replace("`r`n", "`n")
        [System.IO.File]::WriteAllBytes($qrPub, [System.Text.Encoding]::ASCII.GetBytes($qrBad))
        Remove-Item -LiteralPath $uploadDir -Recurse -Force
        Remove-Item -LiteralPath $uploadZip -Force
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a published QUALREP.BAT with LF endings was assembled into the asset."
        Assert-True ($r.Output -match "QUALREP\.BAT" -and $r.Output -match "CRLF") `
            ("expected the refusal to name QUALREP.BAT and its line endings. Output:`n" + $r.Output)
        Assert-True (-not (Test-Path -LiteralPath $uploadZip)) "an upload archive was written around a malformed report batch."
        [System.IO.File]::WriteAllBytes($qrPub, $qrGood)
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not re-assembled once QUALREP.BAT was restored:`n" + $r.Output)

        # --- the UAS class driver's pair (roadmap task 31-A.2) ---------------
        #
        # A flavour directory carrying xhciuas.inf is gated by that INF's own
        # gate in place: the pair is carried into the asset when it passes,
        # and a directory whose UAS INF would bind Bulk-Only devices is
        # refused. Where the pair is absent (every version before 2.0.0.0, and
        # the cases above) nothing is required of it in this mode.
        Write-Step "the upload set gates the UAS class driver's pair by its own INF"
        $uasInfSrc = Join-Path $repo "src\uas\xhciuas.inf"
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $uasInfSrc -Destination (Join-Path $pubRoot "$fl\xhciuas.inf") -Force
            [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "$fl\xhciuas.sys"),
                [System.Text.Encoding]::ASCII.GetBytes("stand-in xhciuas.sys, $fl flavour"))
        }
        Remove-Item -LiteralPath $uploadDir -Recurse -Force
        Remove-Item -LiteralPath $uploadZip -Force
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not assembled with the UAS pair present:`n" + $r.Output)
        Assert-True ($r.Output -match "carries the UAS class driver") `
            ("expected the UAS pair to be gated by its own INF. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhciuas.sys", "xhciuas.inf")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from the upload set's $fl\ directory though the published one carries it."
            }
        }
        $uasBad = [System.IO.File]::ReadAllText($uasInfSrc).Replace("Prot_62", "Prot_50")
        [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "release\xhciuas.inf"),
            [System.Text.Encoding]::ASCII.GetBytes($uasBad))
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a published UAS INF binding a Bulk-Only id was assembled into the asset."
        Assert-True ($r.Output -match "check-uas-inf") `
            ("expected the refusal to name the UAS INF gate. Output:`n" + $r.Output)
        # Back to the fixture the cases below were written against.
        foreach ($fl in @("release", "debug")) {
            Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\xhciuas.inf") -Force
            Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\xhciuas.sys") -Force
        }
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set did not assemble again once the UAS pair was removed:`n" + $r.Output)

        # --- the connection manager's four files (roadmap-usb4.md 39-A.1) ---
        #
        # Gated in place by check-cm-inf.ps1 where present, carried into the
        # asset, and all four or none: a directory missing any one of them
        # is refused. Not required in this mode (no version before 3.0.0.0
        # has them).
        Write-Step "the upload set gates the connection manager's four files, all or none"
        $cmFiles = @{
            "xhcicm.inf"  = (Join-Path $repo "src\cm\xhcicm.inf")
            "nt\xhcicmf.inf" = (Join-Path $repo "src\cm\xhcicmf.inf")
        }
        $putCm = {
            foreach ($fl in @("release", "debug")) {
                Ensure-Directory (Join-Path $pubRoot "$fl\nt")
                foreach ($n in $cmFiles.Keys) { Copy-Item -LiteralPath $cmFiles[$n] -Destination (Join-Path $pubRoot "$fl\$n") -Force }
                [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "$fl\xhcicm.sys"),
                    [System.Text.Encoding]::ASCII.GetBytes("stand-in xhcicm.sys, $fl flavour"))
                # The filter INF's own copy beside it, the same bytes.
                [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "$fl\nt\xhcicm.sys"),
                    [System.Text.Encoding]::ASCII.GetBytes("stand-in xhcicm.sys, $fl flavour"))
            }
        }
        & $putCm
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not assembled with the connection manager present:`n" + $r.Output)
        Assert-True ($r.Output -match "carries the connection manager's nt\\xhcicmf\.inf") `
            ("expected the connection manager's INFs to be gated by their own gate. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhcicm.sys", "xhcicm.inf", "nt\xhcicmf.inf", "nt\xhcicm.sys")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from the upload set's $fl\ directory though the published one carries it."
            }
        }
        foreach ($drop in @("xhcicm.inf", "nt\xhcicmf.inf", "xhcicm.sys", "nt\xhcicm.sys")) {
            Remove-Item -LiteralPath (Join-Path $pubRoot "release\$drop") -Force
            if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
            if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
            $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                                   "-PackageRoot", $noOut, "-UploadDir", $upRoot)
            Assert-True ($r.ExitCode -ne 0) "a published directory missing $drop of the connection manager was assembled into the asset."
            Assert-True (-not (Test-Path -LiteralPath $uploadZip)) "an upload archive was written around an incomplete connection manager ($drop missing)."
            & $putCm
        }
        # The filter INF's copy of the binary differing from the root's is
        # refused: Vista and 7 would install the nt\ bytes from the store.
        [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "release\nt\xhcicm.sys"),
            [System.Text.Encoding]::ASCII.GetBytes("stand-in xhcicm.sys, another build"))
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a published nt\xhcicm.sys differing from xhcicm.sys was assembled into the asset."
        Assert-True (-not (Test-Path -LiteralPath $uploadZip)) "an upload archive was written around two different xhcicm.sys copies."
        & $putCm
        $cmBad = [System.IO.File]::ReadAllText($cmFiles["nt\xhcicmf.inf"]).Replace('Signature="$Windows NT$"', 'Signature="$CHICAGO$"')
        [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "release\nt\xhcicmf.inf"), [System.Text.Encoding]::ASCII.GetBytes($cmBad))
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a published filter INF Windows 98 would read was assembled into the asset."
        Assert-True ($r.Output -match "check-cm-inf") ("expected the refusal to name the connection manager's INF gate. Output:`n" + $r.Output)
        & $putCm
        # The filter INF beside xhcicm.inf, where a 98 wizard is pointed, is
        # refused even with the right one in nt\ (CM-NO9XBRIDGE, 39-0.4).
        Copy-Item -LiteralPath $cmFiles["nt\xhcicmf.inf"] -Destination (Join-Path $pubRoot "release\xhcicmf.inf") -Force
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a filter INF beside xhcicm.inf, where a 98 wizard is pointed, was assembled into the asset."
        Assert-True ($r.Output -match "CM-NO9XBRIDGE") ("expected the refusal to name CM-NO9XBRIDGE. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhcicm.sys", "xhcicm.inf", "nt\xhcicmf.inf")) {
                Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\$name") -Force
            }
            Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\nt") -Recurse -Force
            if (Test-Path -LiteralPath (Join-Path $pubRoot "$fl\xhcicmf.inf")) { Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\xhcicmf.inf") -Force }
        }
        # A filter INF alone, beside where xhcicm.inf would be, with none of
        # the trio: refused all the same.
        Copy-Item -LiteralPath $cmFiles["nt\xhcicmf.inf"] -Destination (Join-Path $pubRoot "release\xhcicmf.inf") -Force
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a lone filter INF outside nt\ was assembled into the asset."
        Assert-True ($r.Output -match "CM-NO9XBRIDGE") ("expected the refusal to name CM-NO9XBRIDGE. Output:`n" + $r.Output)
        Assert-True (-not (Test-Path -LiteralPath $uploadZip)) "an upload archive was written around a lone filter INF outside nt\."
        Remove-Item -LiteralPath (Join-Path $pubRoot "release\xhcicmf.inf") -Force
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set did not assemble again once the connection manager was removed:`n" + $r.Output)

        # --- txtsetup.oem (roadmap task 33.3) --------------------------------
        #
        # Gated in place against the INF beside it where present, carried into
        # the asset, and not required in this mode (no version before 2.1.0.0
        # has one).
        Write-Step "the upload set gates txtsetup.oem by its own gate"
        $oemSrc = Join-Path $repo "src\txtsetup.oem"
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $oemSrc -Destination (Join-Path $pubRoot "$fl\txtsetup.oem") -Force
        }
        Remove-Item -LiteralPath $uploadDir -Recurse -Force
        Remove-Item -LiteralPath $uploadZip -Force
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not assembled with txtsetup.oem present:`n" + $r.Output)
        Assert-True ($r.Output -match "carries the text-mode Setup description") `
            ("expected txtsetup.oem to be gated. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\txtsetup.oem")) `
                "txtsetup.oem is missing from the upload set's $fl\ directory though the published one carries it."
        }
        [System.IO.File]::WriteAllBytes((Join-Path $pubRoot "release\txtsetup.oem"),
            [System.Text.Encoding]::ASCII.GetBytes([System.IO.File]::ReadAllText($oemSrc).Replace("PCI\CC_0C0330", "PCI\CC_0C0320")))
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "a published txtsetup.oem binding an EHCI class id was assembled into the asset."
        Assert-True ($r.Output -match "check-txtsetup-oem") `
            ("expected the refusal to name the txtsetup.oem gate. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            Remove-Item -LiteralPath (Join-Path $pubRoot "$fl\txtsetup.oem") -Force
        }
        if (Test-Path -LiteralPath $uploadDir) { Remove-Item -LiteralPath $uploadDir -Recurse -Force }
        if (Test-Path -LiteralPath $uploadZip) { Remove-Item -LiteralPath $uploadZip -Force }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set did not assemble again once txtsetup.oem was removed:`n" + $r.Output)

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

        # --- the architecture -UploadSetOnly re-assembles for ----------------
        #
        # -Arch defaults to both architectures since roadmap task 21.5, which is
        # right for a cut and would be wrong here: every already-published
        # version is x86-only, and the case above re-assembles one of exactly
        # that shape under the default. So the default is derived from the
        # published tree. The other half is what this asserts - an -Arch the
        # caller PASSED is held to exactly, because then a missing directory is
        # a real mistake rather than an old cut's shape.
        #
        Write-Step "-UploadSetOnly derives the architecture, but obeys an explicit -Arch"
        $r = Invoke-Releaser ($relArgs + @("-Arch", "x64"))
        Assert-True ($r.ExitCode -ne 0) `
            "-UploadSetOnly accepted -Arch x64 against a published tree that has no 64-bit directory."
        Assert-True ($r.Output -match "release-x64") `
            ("expected the refusal to name the directory it wanted. Output:`n" + $r.Output)

        # --- and a published tree that DOES hold the 64-bit pair --------------
        #
        # Every case above assembles an x86-only tree, which is what the four
        # already-published versions are - so the whole assembly ran under the
        # INF gate's default x86 profile and nothing noticed that it was never
        # told otherwise. Hand it a `release-x64\` holding the amd64 INF and the
        # profile stops being a detail: that file fails seven of the gate's x86
        # checks (PATH-MFGDEC three times, BOTH-XREF twice, OS-DEFAULT twice), so before
        # 2026-09-16 this exited non-zero on a tree that is perfectly good.
        #
        # It is worth driving for real rather than asserting a source anchor
        # because of *where* the failure lands: in an ordinary cut this call is
        # the tail of the run, after `releases\<version>\` has been moved into
        # place - the published-with-no-asset state make-release.ps1's own
        # comments exist to prevent. -UploadSetOnly is the only way to reach the
        # same code without two builds.
        #
        Write-Step "-UploadSetOnly assembles a four-directory tree, each INF under its own architecture"
        # The tracked 64-bit INF, read from src\ rather than mutated, because
        # what is under test is the gate profile it is parsed under.
        $amd64InfSrc = Join-Path $repo "src\xhci98-amd64.inf"
        Assert-True (Test-Path -LiteralPath $amd64InfSrc) `
            "src\xhci98-amd64.inf is missing; the 64-bit half of the upload set cannot be driven."
        $relRoot4 = Join-Path $script:work "releases-4dir"
        $pubRoot4 = Join-Path $relRoot4 $relVersion
        foreach ($leg in @(
            @{ Dir = "release-x86"; Inf = $plainInf },
            @{ Dir = "debug-x86";   Inf = $plainInf },
            @{ Dir = "release-x64"; Inf = $amd64InfSrc },
            @{ Dir = "debug-x64";   Inf = $amd64InfSrc }
        )) {
            $d = Join-Path $pubRoot4 $leg.Dir
            Ensure-Directory $d
            # Distinct bytes per directory for the reason the pair above uses
            # them: a run that crossed two legs should fail rather than pass.
            [System.IO.File]::WriteAllBytes((Join-Path $d "xhci98.sys"),
                [System.Text.Encoding]::ASCII.GetBytes("stand-in xhci98.sys, $($leg.Dir), $relVersion"))
            # Both architectures' media carry the file as xhci98.inf; which one
            # it is is said by the directory, which is the whole reason the
            # architecture has to be carried alongside the name.
            Copy-Item -LiteralPath $leg.Inf -Destination (Join-Path $d "xhci98.inf") -Force
            # The x86 media's helper (design record 18); the x64 media has none.
            if ($leg.Dir -like "*-x86") {
                Set-Content -LiteralPath (Join-Path $d "xhcihlpr.sys") -Encoding ASCII `
                            -Value "stand-in xhcihlpr.sys, $($leg.Dir), $relVersion"
            }
        }
        Set-Content -LiteralPath (Join-Path $pubRoot4 "readme.txt") -Encoding ASCII `
                    -Value "stand-in per-version readme"
        # The port switchover's scripts in tools\portsw\, as every cut publishes
        # them since roadmap-hcd tasks 37.5 and 38.9: the upload assembly
        # refuses a tree of this version without them (Assert-PortSwitchLayout,
        # Assert-ToolsLayout).
        Ensure-Directory (Join-Path $pubRoot4 "tools\portsw")
        foreach ($name in @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")) {
            Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $pubRoot4 "tools\portsw\$name") -Force
        }
        # The silent install's four files in tools\silent\ (task 38.2).
        Ensure-Directory (Join-Path $pubRoot4 "tools\silent")
        foreach ($pair in @(@("silent.reg", "SILENT.REG"), @("silent.vbs", "SILENT.VBS"), @("silent-on.bat", "silent-on.bat"), @("silent-off.bat", "silent-off.bat"))) {
            Copy-Item -LiteralPath (Join-Path $repo ("src\" + $pair[0])) -Destination (Join-Path $pubRoot4 ("tools\silent\" + $pair[1])) -Force
        }

        $up4 = Join-Path $script:work "upload-4dir"
        Ensure-Directory $up4
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot4,
                               "-PackageRoot", $noOut, "-UploadDir", $up4)
        Assert-True ($r.ExitCode -eq 0) `
            ("-UploadSetOnly refused a published tree holding both architectures - the likeliest cause is the INF gate being run on the amd64 file under its default x86 profile:`n" + $r.Output)
        $uploadDir4 = Join-Path $up4 ("upload-" + $relVersion)
        foreach ($d in @("release-x86", "debug-x86", "release-x64", "debug-x64")) {
            foreach ($name in @("xhci98.sys", "xhci98.inf")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir4 "$d\$name")) `
                    "'$name' is missing from the upload set's $d\ directory."
            }
        }
        # The 64-bit directories really do carry the 64-bit INF, so the run
        # above gated what it claims to have gated rather than four copies of
        # one file.
        foreach ($d in @("release-x64", "debug-x64")) {
            $t = [System.IO.File]::ReadAllText((Join-Path $uploadDir4 "$d\xhci98.inf"))
            Assert-True ($t -match [regex]::Escape("%Mfg%=XhciModels,NTamd64")) `
                "the upload set's $d\xhci98.inf is not the amd64 file, so the case that needs the gate's amd64 profile was never run."
        }
        Assert-True (Test-Path -LiteralPath (Join-Path $up4 ("xhci98-" + $relVersion + ".zip"))) `
            "no upload archive was written for the four-directory tree."

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

        # --- a TEST build (-TestLabel, 36.final's 2.2.0.3) ------------------
        #
        # Refused before anything is built, so these run on the stand-ins: a
        # test build is never cut into the repository's own releases\ (the
        # default -ReleasesDir), a label must fit the readme's banner, and
        # -UploadSetOnly renders no readme to label.
        Write-Step "-TestLabel refuses releases\, an unprintable or long label, and -UploadSetOnly"
        foreach ($bad in @(
            @{ Args = @("-TestLabel", "bench pending");                            Says = "never published" },
            @{ Args = @("-TestLabel", ("x" * 51), "-ReleasesDir", $relRoot);       Says = "printable ASCII" },
            @{ Args = @("-TestLabel", "bench`tpending", "-ReleasesDir", $relRoot); Says = "printable ASCII" },
            @{ Args = ($relArgs + @("-TestLabel", "bench pending"));               Says = "renders none" }
        )) {
            $r = Invoke-Releaser $bad.Args
            Assert-True ($r.ExitCode -ne 0) "make-release.ps1 accepted $($bad.Args -join ' ')."
            Assert-True ($r.Output -match [regex]::Escape($bad.Says)) `
                ("expected '$($bad.Says)' in the -TestLabel refusal. Output:`n" + $r.Output)
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
            Where-Object { $_.Name -notin @("release", "debug", "tools", "xhciqual", "portsw", "silent") })
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
        # The anchor moved with roadmap task 21.3: the media-root assertion is
        # now inside a per-architecture loop, because a cut may hold two INFs,
        # so the layout it is handed is $layoutA rather than one $declaredLayout.
        # What this list checks is the ORDER of the steps, and that is unchanged
        # - the assertion still runs before anything is built or published.
        @{ Name = "the media-root assertion";  Find = 'Assert-PublishableAtMediaRoot -Layout $layoutA' },
        @{ Name = "the containment assertion"; Find = 'Assert-UploadSetOutsideRelease -UploadRoot $early.Root' },
        @{ Name = "the make-package call";     Find = '& powershell.exe @pkgArgs' },
        @{ Name = "the declared-media check";  Find = 'Assert-PackageMatchesDeclaredMedia -PkgDir $pkgDir -Expected $declaredExpected' },
        # The upload assembly comes BEFORE the swap since the 2026-09-17
        # audit's D5: it carries the per-directory INF gate and the zip, and
        # either failing after the swap left a written-once release with no
        # asset - the exact shape this list exists to prevent, one step later
        # than it was looking.
        @{ Name = "the upload assembly";       Find = '$set = New-UploadSet -PublishedRoot $destRoot' },
        @{ Name = "the publish swap";          Find = 'Move-Item -LiteralPath $destRoot -Destination $finalRoot' },
        # ...and the set built before the swap reaches its final names only
        # after it (Codex review round 2): built straight onto them, a swap
        # that failed and rolled the release back left the OLD release beside
        # the NEW release's zip.
        @{ Name = "the upload publish";        Find = '$final = Publish-UploadSet -Set $stagedUpload' }
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

    # --- the aside's removal after a completed swap may not fail the cut -----
    #
    # The 2026-09-17 audit's D4, structural for the same reason as the list
    # above. After both renames the replaced release is disk space, and on a
    # OneDrive-hosted tree its removal fails routinely on a sync handle; under
    # Stop that reported a published cut as failed and left the aside for the
    # next run to refuse on as "interrupted". make-package.ps1 has the same
    # rule for its retired package, and this holds make-release.ps1 to it.
    Write-Step "the replaced release's removal is relaxed, and a leftover beside a published tree is not 'interrupted'"
    Assert-True ($releaserText.Contains('Remove-Item -LiteralPath $asideRoot -Recurse -Force -ErrorAction SilentlyContinue')) `
        "make-release.ps1 removes the replaced release under Stop after the swap, so a held handle in it fails a cut whose publish already completed."
    Assert-True ($releaserText.Contains("'`$asideCheck' exists and '`$finalRoot' does not")) `
        "make-release.ps1's leftover-aside check no longer asks whether the published tree is beside it before calling the previous run interrupted."

    # --- a failed swap leaves the previous asset in place ---------------------
    #
    # Codex review round 2 on the D5 reorder. The upload set is assembled
    # before the swap, so it must be assembled under names that are not the
    # previous asset's: the ordinary cut passes -Staged, Get-UploadSetPaths
    # -Staged yields the `.staging` pair, New-UploadSet takes its paths from
    # that call (so its delete-existing steps hit the staged names only), the
    # final names are written by Publish-UploadSet after the swap (the ordering
    # list above), and the outer catch removes the staged pair so a rollback
    # leaves neither a stale staged set nor a touched previous asset.
    # -UploadSetOnly publishes nothing and writes the final names directly.
    Write-Step "a failed publish swap leaves the previous upload set and zip untouched"
    Assert-True ($releaserText.Contains('-Publishable $publishable -Staged')) `
        "make-release.ps1's ordinary cut no longer assembles the upload set with -Staged, so a swap that fails leaves the old release beside the new release's zip."
    Assert-True ($releaserText.Contains('Get-UploadSetPaths -UploadDir $UploadDir -Version $Version -Staged:$Staged')) `
        "New-UploadSet no longer takes its paths from Get-UploadSetPaths -Staged, so its delete-existing and zip steps would hit the final names before the swap."
    Assert-True ($releaserText.Contains('("upload-" + $Version + $suffix)') -and $releaserText.Contains('+ ".zip" + $suffix)') -and $releaserText.Contains('".staging"')) `
        "Get-UploadSetPaths -Staged no longer names the set under a .staging suffix, so a staged assembly would overwrite the previous asset."
    $catchAt = $releaserText.LastIndexOf('} catch {')
    $catchText = $releaserText.Substring($catchAt)
    Assert-True ($catchText.Contains('Remove-StagedUploadSet -Set $stagedUpload')) `
        "make-release.ps1's outer catch no longer removes the staged upload set, so a rolled-back cut leaves a .staging pair behind."
    $usoStart = $releaserText.IndexOf('if ($UploadSetOnly) {')
    $usoEnd = $releaserText.IndexOf('exit 0', $usoStart)
    Assert-True ($usoStart -ge 0 -and $usoEnd -gt $usoStart -and -not $releaserText.Substring($usoStart, $usoEnd - $usoStart).Contains('-Staged')) `
        "-UploadSetOnly assembles with -Staged, but nothing publishes a staged set in that mode, so it would leave .staging names and no asset."

    # --- once the swap has succeeded, the cut is published, whatever out\ does
    #
    # Codex review round 3. Publish-UploadSet runs after the release swap, so
    # a failure in it must not reach the outer catch as a failed cut (the
    # release cannot be unpublished, and the "previous asset untouched" line
    # would be false): it is caught locally, the staged pair is kept, the
    # state of out\ is said, and -UploadSetOnly is named as the recovery. And
    # the previous zip is renamed aside, not deleted, so no step leaves out\
    # with no zip at all; the aside goes only after the staged zip is on.
    Write-Step "a failed upload publish after the swap is a warning with a recovery, not a failed cut"
    $pubAt = $releaserText.IndexOf('$final = Publish-UploadSet -Set $stagedUpload')
    $pubCatchAt = $releaserText.IndexOf('} catch {', $pubAt)
    $pubBlock = $releaserText.Substring($pubAt, $releaserText.IndexOf('# --- summary', $pubAt) - $pubAt)
    Assert-True ($pubAt -ge 0 -and $pubCatchAt -gt $pubAt -and $pubCatchAt -lt ($pubAt + $pubBlock.Length)) `
        "make-release.ps1 no longer catches a Publish-UploadSet failure locally, so a rename that fails in out\ reports a published cut as failed."
    Assert-True ($pubBlock.Contains('(Get-UploadSetRepairCommand)') -and $pubBlock.Contains('The staged set has been left in place')) `
        "make-release.ps1's upload-publish catch no longer keeps the staged set and names -UploadSetOnly as the way to rebuild the asset."
    Assert-True (-not $pubBlock.Contains('Remove-StagedUploadSet')) `
        "make-release.ps1's upload-publish catch discards the staged set, which after the swap is the only copy of the new asset."
    $fnAt = $releaserText.IndexOf('function Publish-UploadSet')
    $fnText = $releaserText.Substring($fnAt, $releaserText.IndexOf('function Remove-StagedUploadSet') - $fnAt)
    $asideMove = $fnText.IndexOf('Move-Item -LiteralPath $Set.Final.Zip -Destination $zipAside')
    $stagedMove = $fnText.IndexOf('Move-Item -LiteralPath $Set.Zip -Destination $Set.Final.Zip')
    $asideDrop = $fnText.IndexOf('Remove-Item -LiteralPath $zipAside -Force -ErrorAction SilentlyContinue')
    Assert-True ($asideMove -ge 0 -and $stagedMove -gt $asideMove -and $asideDrop -gt $stagedMove) `
        "Publish-UploadSet no longer renames the previous zip aside, moves the staged zip on, and only then drops the aside - in that order - so a failure between the steps can leave out\ with no zip."
    Assert-True (-not $fnText.Contains('Remove-Item -LiteralPath $Set.Final.Zip')) `
        "Publish-UploadSet deletes the previous zip outright instead of renaming it aside."
    Assert-True ($fnText.Contains('Move-Item -LiteralPath $zipAside -Destination $Set.Final.Zip')) `
        "Publish-UploadSet no longer restores the aside when the final zip is missing after a failure."
    $outerCatch = $releaserText.Substring($releaserText.LastIndexOf('} catch {'))
    Assert-True ($outerCatch.Contains('-and -not $swapDone') -and $outerCatch.Contains('is untouched')) `
        "make-release.ps1's outer catch prints 'the previous asset is untouched' without checking that the failure came before the swap."
    Assert-True ($releaserText.IndexOf('$swapDone = $true') -gt $releaserText.IndexOf('Move-Item -LiteralPath $destRoot -Destination $finalRoot')) `
        "make-release.ps1 no longer marks the swap done right after the release rename, so the outer catch cannot tell a pre-swap failure from a post-swap one."

    # --- a stale .previous is the last good asset when the final zip is gone
    #
    # Codex review round 4. An interruption between the aside rename and the
    # staged move leaves `.previous` present and the final zip absent; a retry
    # that deleted the stale aside first, then failed its own staged move,
    # would lose the only previous archive. So the stale aside is judged by
    # what is beside it - restored when the final zip is absent, dropped only
    # when the final zip is present - and nothing deletes it before that test.
    Write-Step "a stale .previous is restored, not deleted, when the final zip is absent"
    $staleTest = $fnText.IndexOf('if (-not (Test-Path -LiteralPath $Set.Final.Zip)) {')
    $staleRestore = $fnText.IndexOf('Move-Item -LiteralPath $zipAside -Destination $Set.Final.Zip')
    $firstAsideDrop = $fnText.IndexOf('Remove-Item -LiteralPath $zipAside')
    Assert-True ($staleTest -ge 0 -and $staleRestore -gt $staleTest) `
        "Publish-UploadSet no longer tests for the final zip's absence and restores a stale .previous onto it."
    Assert-True ($firstAsideDrop -gt $staleTest) `
        "Publish-UploadSet removes a stale .previous before asking whether the final zip is there, so a retry can delete the only previous archive."
    Assert-True ($fnText.IndexOf('Remove-Item -LiteralPath $zipAside -Force') -gt $staleTest) `
        "Publish-UploadSet's unconditional removal of .previous precedes the final-zip test."

    # --- the repair command names the cut that actually happened -------------
    Write-Step "the -UploadSetOnly repair hint is rendered by one helper from the bound arguments"
    $helperAt = $releaserText.IndexOf('function Get-UploadSetRepairCommand')
    Assert-True ($helperAt -ge 0) "make-release.ps1 no longer has Get-UploadSetRepairCommand, so each repair hint renders its own command and can drop -ReleasesDir, -UploadDir or -Flavor."
    $helperText = $releaserText.Substring($helperAt, $releaserText.IndexOf('function Remove-StagedUploadSet') - $helperAt)
    foreach ($p in @('"Flavor"', '"Arch"', '"ReleasesDir"', '"UploadDir"')) {
        Assert-True ($helperText.Contains('ContainsKey(' + $p + ')')) "Get-UploadSetRepairCommand does not carry -$($p.Trim('"')) through when it was passed."
    }
    $calls = ([regex]::Matches($releaserText, [regex]::Escape('(Get-UploadSetRepairCommand)'))).Count
    Assert-True ($calls -ge 3) ("only " + $calls + " site(s) render the repair hint through Get-UploadSetRepairCommand; the local catch, the summary hint and the outer catch must all use it.")
    Assert-True ($pubBlock.Contains('(Get-UploadSetRepairCommand)') -and $outerCatch.Contains('(Get-UploadSetRepairCommand)')) `
        "the upload-publish catch or the outer catch renders its own -UploadSetOnly command instead of calling Get-UploadSetRepairCommand."
    Assert-True (-not ($releaserText -match '-UploadSetOnly -Version \{')) `
        "a repair hint still formats '-UploadSetOnly -Version {n}' by hand, bypassing Get-UploadSetRepairCommand."

    # --- ...and the rendered line parses, apostrophes and all -----------------
    #
    # Codex review round 5: the paths were single-quoted with no escaping, so
    # a directory such as D:\O'Brien\ rendered a line no shell could parse.
    # This case EXECUTES the helper rather than grepping it: its body is
    # loaded from make-release.ps1's text, driven with bound arguments naming
    # such a path, and the line it renders is parsed with PowerShell's own
    # parser and read back. The fixture is built by concatenation, because a
    # doubled apostrophe inside a here-string is literal (AGENTS.md).
    Write-Step "the repair command survives an apostrophe in a directory path"
    $helperEnd = $helperText.IndexOf("`n}", $helperText.IndexOf('function Get-UploadSetRepairCommand'))
    $helperBody = $helperText.Substring(0, $helperEnd + 2)
    Invoke-Expression $helperBody
    $apos = [string][char]39
    $trickyDir = "D:\O" + $apos + "Brien\releases"
    $Version = "9.9.9.9"
    $Flavor = @("release")
    $ReleasesDir = $trickyDir
    $UploadDir = "D:\out"
    $script:boundArgs = @{ ReleasesDir = $trickyDir; Flavor = $Flavor }
    $rendered = Get-UploadSetRepairCommand
    $tokens = $null; $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput($rendered, [ref]$tokens, [ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) ("the rendered repair command does not parse: " + $rendered)
    $cmdAst = $ast.Find({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true)
    $readBack = $null
    if ($null -ne $cmdAst) {
        for ($i = 0; $i -lt $cmdAst.CommandElements.Count - 1; $i++) {
            $el = $cmdAst.CommandElements[$i]
            if ($el -is [System.Management.Automation.Language.CommandParameterAst] -and $el.ParameterName -eq "ReleasesDir") {
                $readBack = $cmdAst.CommandElements[$i + 1].Value
            }
        }
    }
    Assert-True ($readBack -eq $trickyDir) ("-ReleasesDir did not round-trip through the rendered repair command: got '" + $readBack + "' from " + $rendered)
    Assert-True (-not $rendered.Contains("-UploadDir")) "the repair command carried -UploadDir although it was not among the bound arguments."
    Assert-True ($rendered.Contains("-Flavor release")) "the repair command dropped the bound -Flavor."
    Remove-Variable -Name Version, Flavor, ReleasesDir, UploadDir -ErrorAction SilentlyContinue
    Remove-Variable -Name boundArgs -Scope Script -ErrorAction SilentlyContinue

    # --- a draft is refused: no TODO(...) or TBD reaches a user -------------
    #
    # Roadmap task 32.3. The 2.0.0.0 release notes, readme template and
    # history entry were drafted ahead of their readings, each owed reading
    # tagged TODO(...) or TBD, and make-release.ps1 refuses a cut while any is
    # left in the history entry, the release notes or the rendered readme.
    # Like the repair-command case above, the matcher is EXECUTED: its body is
    # loaded from make-release.ps1's text and driven with fixtures. Then the
    # two call sites are held to the order that makes the refusal useful -
    # the source check before the build, the readme check before the readme
    # is written, both long before the publish swap.
    Write-Step "a draft marker in the history entry, release notes or readme refuses the cut"
    $fnAt = $releaserText.IndexOf('function Find-DraftMarkers')
    Assert-True ($fnAt -ge 0) "make-release.ps1 no longer has Find-DraftMarkers, so a cut can publish a TODO(...) line."
    if ($fnAt -ge 0) {
        $fnEnd = $releaserText.IndexOf("`n}", $fnAt)
        Invoke-Expression $releaserText.Substring($fnAt, $fnEnd - $fnAt + 2)
        $draftFixture = @(
            "A finished sentence.",
            "Audio at a root port: TODO(bench).",
            "## 9.9.9.9 - TBD",
            "lower-case todo( and TBDx and xTBD are prose, not markers",
            "Another finished sentence."
        )
        $found = @(Find-DraftMarkers -Lines $draftFixture -Label "fixture")
        Assert-True ($found.Count -eq 2) ("Find-DraftMarkers found " + $found.Count + " marker(s) in the fixture, not 2: " + ($found -join " | "))
        Assert-True ($found.Count -ge 1 -and $found[0] -eq "fixture line 2: Audio at a root port: TODO(bench).") ("the first hit is not reported with its label and line: " + ($found -join " | "))
        Assert-True ($found.Count -ge 2 -and $found[1] -eq "fixture line 3: ## 9.9.9.9 - TBD") ("a TBD heading is not reported: " + ($found -join " | "))
        $clean = @(Find-DraftMarkers -Lines @("Validated.", "Nothing owed here.") -Label "clean")
        Assert-True ($clean.Count -eq 0) ("Find-DraftMarkers reported a clean text as a draft: " + ($clean -join " | "))
        $empty = @(Find-DraftMarkers -Lines @() -Label "empty")
        Assert-True ($empty.Count -eq 0) "Find-DraftMarkers reported markers in an empty text."
    }
    $draftMarks = @(
        @{ Name = "the history and release-notes draft check"; Find = '$draftHits = @(Find-DraftMarkers -Lines $historyLines' },
        @{ Name = "the make-package call";                     Find = '& powershell.exe @pkgArgs' },
        @{ Name = "the rendered-readme draft check";           Find = '$readmeDraft = @(Find-DraftMarkers -Lines ($readme' },
        @{ Name = "the readme write";                          Find = 'Write-GeneratedText -Path (Join-Path $destRoot "readme.txt")' },
        @{ Name = "the publish swap";                          Find = 'Move-Item -LiteralPath $destRoot -Destination $finalRoot' }
    )
    $draftAt = @()
    foreach ($m in $draftMarks) {
        $i = $releaserText.IndexOf($m.Find)
        Assert-True ($i -ge 0) ("make-release.ps1 no longer contains " + $m.Name + " as this test recognises it.")
        $draftAt += $i
    }
    if (@($draftAt | Where-Object { $_ -lt 0 }).Count -eq 0) {
        for ($i = 1; $i -lt $draftMarks.Count; $i++) {
            Assert-True ($draftAt[$i - 1] -lt $draftAt[$i]) `
                ("$($draftMarks[$i - 1].Name) must come before $($draftMarks[$i].Name) in make-release.ps1.")
        }
    }
    Assert-True ($releaserText.Contains('Join-Path $repo "docs\using\release-notes.md"')) `
        "make-release.ps1's draft check no longer reads docs\using\release-notes.md."

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
    #
    # **Every placeholder in the readme template must be one the renderer
    # substitutes.** The template is a literal here-string and the substitution
    # is a fixed chain of .Replace() calls, so a placeholder added to one and
    # not the other renders as itself - a user-facing file telling someone to
    # install from the "{RELEASEDIR}\\" directory. Nothing else would notice:
    # the readme is generated at the very end of a cut, after every gate.
    #
    # This became worth checking with roadmap task 21.3, which turned the
    # hardcoded RELEASE\ into a placeholder because the directory is now named
    # for its architecture as well as its flavour.
    #
    Write-Step "every readme placeholder is one the renderer substitutes"
    $tplStart = $releaserText.IndexOf('$template = @' + [char]39)
    Assert-True ($tplStart -ge 0) "make-release.ps1 no longer has a readme template this test can find."
    if ($tplStart -ge 0) {
        $tplEnd = $releaserText.IndexOf([char]39 + "@", $tplStart)
        Assert-True ($tplEnd -gt $tplStart) "make-release.ps1's readme template has no closing delimiter."
        $tplText = $releaserText.Substring($tplStart, $tplEnd - $tplStart)
        # The chain, read from the renderer rather than restated here, so a
        # substitution removed from it fails this instead of quietly passing.
        $substituted = @([regex]::Matches($releaserText, 'Replace\("(\{[A-Z]+\})"') |
                         ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        Assert-True ($substituted.Count -ge 5) ("only " + $substituted.Count + " Replace(...) substitutions were found in make-release.ps1; this test cannot be reading the renderer correctly.")
        foreach ($ph in @([regex]::Matches($tplText, '\{[A-Z]+\}') | ForEach-Object { $_.Value } | Sort-Object -Unique)) {
            Assert-True ($substituted -contains $ph) (
                "the readme template uses $ph but make-release.ps1 never substitutes it, so it would render literally into the file a user reads.")
        }
    }

    # The rendered readme's own leftover-placeholder check must be case
    # sensitive. The x64 test-signing text carries bcdedit's {current}, and
    # the 2.3.0.0 cut first failed on it ("readme template left an
    # unsubstituted placeholder: {current}") while the check was -match, which
    # ignores case. The check is read from the renderer, then applied to a
    # lower-case brace (which must pass) and an upper-case one (which must not).
    Write-Step "the leftover-placeholder check passes lower-case braces such as {current}"
    $leftoverCheck = [regex]::Match($releaserText, "if \(\`$readme -(c?match) '([^']+)'\) \{\s*throw `"readme template left an unsubstituted placeholder")
    Assert-True $leftoverCheck.Success "make-release.ps1 no longer has a leftover-placeholder check on the rendered readme that this test can find."
    if ($leftoverCheck.Success) {
        Assert-True ($leftoverCheck.Groups[1].Value -ceq "cmatch") (
            "make-release.ps1 checks the rendered readme with -" + $leftoverCheck.Groups[1].Value +
            "; it must be -cmatch, or bcdedit's {current} is refused as a placeholder.")
        $leftoverPattern = $leftoverCheck.Groups[2].Value
        $lowerSample = "Run: bcdedit /set {current} testsigning on, then restart."
        $upperSample = "Install from the {RELEASEDIR}\ directory."
        Assert-True (-not ($lowerSample -cmatch $leftoverPattern)) (
            "the leftover-placeholder check '" + $leftoverPattern + "' refuses a readme carrying {current}.")
        Assert-True ($upperSample -cmatch $leftoverPattern) (
            "the leftover-placeholder check '" + $leftoverPattern + "' no longer catches {RELEASEDIR}.")
    }
    if ($tplStart -ge 0 -and $tplEnd -gt $tplStart) {
        Assert-True ($tplText.Contains("{current}") -or $releaserText.Contains("{current}")) `
            "make-release.ps1 no longer carries bcdedit's {current}; this test's lower-case case has lost its subject, so revisit it."
    }

    # **The cut's version-literal guard, run here on the real template.** The
    # guard (make-release.ps1, $allowedTemplateVersions) runs only at a cut, so
    # a release named by hand in the template got as far as the 3.0.0.0 dry
    # cut before anything refused it ("(new in 3.0.0.0)"), the same failure
    # class as the 2.4.0.0 cut. The pattern and the allow-list are read from
    # the guard itself, so the two cannot drift apart.
    Write-Step "the readme template names no release by hand (the cut's version-literal guard)"
    $verGuard = [regex]::Match($releaserText, "\`$templateVersions = @\(\[regex\]::Matches\(\`$template, '([^']+)'\)")
    $verAllow = [regex]::Match($releaserText, "\`$allowedTemplateVersions = @\{([^}]*)\}")
    Assert-True ($verGuard.Success -and $verAllow.Success) `
        "make-release.ps1 no longer has the template version-literal guard as this test reads it; the template is unchecked until the anchor is fixed."
    if ($verGuard.Success -and $verAllow.Success -and $tplStart -ge 0 -and $tplEnd -gt $tplStart) {
        Assert-True ("THE EASY WAY: INSTALL.BAT (new in 3.0.0.0)." -match $verGuard.Groups[1].Value) `
            ("the guard's pattern '" + $verGuard.Groups[1].Value + "' no longer matches a four-part version.")
        $verAllowed = @([regex]::Matches($verAllow.Groups[1].Value, '"(\d+\.\d+\.\d+\.\d+)"') | ForEach-Object { $_.Groups[1].Value })
        $verBody = $tplText.Substring($tplText.IndexOf("@'") + 2)
        $verNamed = @([regex]::Matches($verBody, $verGuard.Groups[1].Value) | ForEach-Object { $_.Value } | Sort-Object -Unique |
                      Where-Object { $verAllowed -notcontains $_ })
        Assert-True ($verNamed.Count -eq 0) (
            "the readme template names version(s) " + ($verNamed -join ', ') + " by hand, and the cut would refuse it: write it with {VERSION}, or put the sentence in history.md.")
    }

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
    # The wrong sentence must stay absent.
    Assert-True ($releaserText -notmatch "validated on, including on real hardware") `
        "make-release.ps1 tells the reader Windows 2000 SP4 is validated on real hardware; AGENTS.md says every Windows 2000 observation in this project is a virtual-machine one."
    # The positive half ("Only Windows 98 SE has been validated on ...") was
    # dropped on 2026-10-04: the owner removed that paragraph from the 2.0.0.0
    # readme, which now makes no real-hardware claim for any target.

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

    # The UAS class driver's stamp (task 31-A.2): its own file name, its own
    # binary, and its source set from src\uas plus the src\ headers it
    # includes - the same two answers for the same two cases.
    $uasStampWork = Join-Path $script:work "stamp-uas"
    New-Item -ItemType Directory -Path $uasStampWork | Out-Null
    $uasStampSys = Join-Path $uasStampWork "xhciuas.sys"
    [System.IO.File]::WriteAllBytes($uasStampSys, [byte[]](5, 6, 7, 8))
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File $stampScript -Driver xhciuas -Write $uasStampWork 2>&1
        $uasWrite = $LASTEXITCODE
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File $stampScript -Driver xhciuas -Check $uasStampWork 2>&1
        $uasCheck = $LASTEXITCODE
        [System.IO.File]::WriteAllBytes($uasStampSys, [byte[]](5, 6, 7, 9))
        $null = & powershell -NoProfile -ExecutionPolicy Bypass -File $stampScript -Driver xhciuas -Check $uasStampWork 2>&1
        $uasSwap = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    $uasStampText = [System.IO.File]::ReadAllText((Join-Path $uasStampWork "xhciuas.srcstamp"))
    Assert-True ($uasWrite -eq 0 -and $uasCheck -eq 0) "source-stamp.ps1 -Driver xhciuas did not write and pass a fresh stamp."
    Assert-True ($uasSwap -eq 1) "a UAS binary swapped under an unchanged stamp must be refused."
    Assert-True ($uasStampText -cmatch '(?m)^BINARY [0-9A-F]{64} xhciuas\.sys\r?$' -and
                 $uasStampText -cmatch '(?m)^[0-9A-F]{64} uas_xport\.c\r?$' -and
                 $uasStampText -cmatch '(?m)^[0-9A-F]{64} \.\.\\xhci98_streams\.h\r?$') `
        "the UAS stamp does not cover its binary, its own sources and the src\ headers it includes."

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


    # --- the second architecture (roadmap task 21.3) -------------------------
    #
    # -Arch moves three things at once and they must not be separable: which
    # obj subdirectory the binary comes from, which of the two INFs is staged,
    # and which architecture the INF and import gates are run under. The
    # failure this coverage is about is not a crash - it is a package that
    # stages an amd64 xhci98.sys around the 32-bit INF, installs on a 32-bit
    # machine because that INF offers it a driver, and then fails its load with
    # a yellow bang no log explains.
    #
    # **-UploadSetOnly against a tree cut with the NEW directory names.** The
    # block above stages `release\` and `debug\` - the shape the four
    # already-cut versions have - and so covers the legacy fallback. This
    # covers the primary path, which is what every cut from now on produces,
    # and the two together are why that fallback exists at all: the rename
    # does not reach back through the write-once rule, so both shapes have to
    # assemble.
    #
    Write-Step "-UploadSetOnly assembles a tree cut with the -x86 directory names"
    {
        # **The current version, not an invented one.** -UploadSetOnly refuses
        # any version other than the one src\xhci98.inf declares - it rebuilds
        # the asset of the current cut and nothing else - so a made-up number
        # is refused for that reason and this case would be testing the wrong
        # refusal.
        $newInfText = [System.IO.File]::ReadAllText($prodInf)
        Assert-True ($newInfText -match '(?m)^DriverVer\s*=\s*[^,\r\n]+,\s*([0-9.]+)\s*$') `
            "src\xhci98.inf has no readable DriverVer version."
        $newVersion = $Matches[1].Trim()
        $newRelRoot = Join-Path $script:work "releases-newnames"
        $newPubRoot = Join-Path $newRelRoot $newVersion
        $newUpRoot = Join-Path $script:work "upload-newnames"
        Ensure-Directory $newUpRoot

        $newInf = $prodInf
        foreach ($fl in @("release-x86", "debug-x86")) {
            $d = Join-Path $newPubRoot $fl
            Ensure-Directory $d
            [System.IO.File]::WriteAllBytes((Join-Path $d "xhci98.sys"),
                [System.Text.Encoding]::ASCII.GetBytes("stand-in xhci98.sys, $fl, $newVersion"))
            Copy-Item -LiteralPath $newInf -Destination (Join-Path $d "xhci98.inf") -Force
            Set-Content -LiteralPath (Join-Path $d "xhcihlpr.sys") -Encoding ASCII `
                        -Value "stand-in xhcihlpr.sys, $fl, $newVersion"
        }
        Set-Content -LiteralPath (Join-Path $newPubRoot "readme.txt") -Encoding ASCII `
                    -Value "stand-in per-version readme"
        Copy-Item -LiteralPath (Join-Path $repo "LICENSE") `
                  -Destination (Join-Path $newPubRoot "LICENSE") -Force
        # The port switchover's scripts in tools\portsw\, as every cut publishes
        # them since roadmap-hcd tasks 37.5 and 38.9: the upload assembly
        # refuses a tree of this version without them (Assert-PortSwitchLayout,
        # Assert-ToolsLayout).
        Ensure-Directory (Join-Path $newPubRoot "tools\portsw")
        foreach ($name in @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")) {
            Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $newPubRoot "tools\portsw\$name") -Force
        }
        # The silent install's four files in tools\silent\ (task 38.2).
        Ensure-Directory (Join-Path $newPubRoot "tools\silent")
        foreach ($pair in @(@("silent.reg", "SILENT.REG"), @("silent.vbs", "SILENT.VBS"), @("silent-on.bat", "silent-on.bat"), @("silent-off.bat", "silent-off.bat"))) {
            Copy-Item -LiteralPath (Join-Path $repo ("src\" + $pair[0])) -Destination (Join-Path $newPubRoot ("tools\silent\" + $pair[1])) -Force
        }

        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $newVersion,
                               "-ReleasesDir", $newRelRoot, "-UploadDir", $newUpRoot)
        Assert-True ($r.ExitCode -eq 0) (
            "-UploadSetOnly refused a published tree using the release-x86/debug-x86 names, which is what every cut from now on produces:`n" + $r.Output)
        $newAssembled = Join-Path $newUpRoot ("upload-" + $newVersion)
        foreach ($fl in @("release-x86", "debug-x86")) {
            Assert-True (Test-Path -LiteralPath (Join-Path $newAssembled (Join-Path $fl "xhci98.sys"))) (
                "the assembled upload set has no $fl\xhci98.sys. Output:`n" + $r.Output)
            Assert-True (Test-Path -LiteralPath (Join-Path $newAssembled (Join-Path $fl "xhci98.inf"))) (
                "the assembled upload set has no $fl\xhci98.inf. Output:`n" + $r.Output)
        }
    }.Invoke() | Out-Null

    Write-Step "the second architecture: -Arch moves the INF with the binary"

    #
    # A real run, with the amd64 INF and a stand-in binary. What it asserts is
    # the staged INF's identity, read out of the staged file rather than out of
    # the arguments: the media has to carry the 64-bit file's decorations.
    #
    $amd64Inf = Join-Path $repo "src\xhci98-amd64.inf"
    Assert-True (Test-Path -LiteralPath $amd64Inf) "src\xhci98-amd64.inf is missing; the 64-bit package has no INF."
    $arch64Out = Join-Path $script:work "pkg-amd64"
    $r = Invoke-Packager @("-Arch", "amd64", "-InfPath", $amd64Inf, "-DriverPath", $driver, "-OutDir", $arch64Out)
    Assert-True ($r.ExitCode -eq 0) ("the amd64 package was rejected:`n" + $r.Output)
    $stagedInf64 = Join-Path $arch64Out "xhci98.inf"
    Assert-True (Test-Path -LiteralPath $stagedInf64) "the amd64 package has no xhci98.inf."
    if (Test-Path -LiteralPath $stagedInf64) {
        $staged64Text = [System.IO.File]::ReadAllText($stagedInf64)
        Assert-True ($staged64Text -match [regex]::Escape("%Mfg%=XhciModels,NTamd64")) `
            "the amd64 package staged an INF with no NTamd64 models decoration, so a 64-bit engine would find no driver in it."
        Assert-True ($staged64Text -notmatch [regex]::Escape("DevLoader")) `
            "the amd64 package staged an INF carrying the Windows 98 loader values, so it is the 32-bit file under the 64-bit package's name."
    }
    # The staged file keeps the media name whatever the source file is called -
    # both architectures' media carry xhci98.inf and xhci98.sys, which is why
    # they need directories of their own rather than names of their own.
    Assert-True (Test-Path -LiteralPath (Join-Path $arch64Out "xhci98.sys")) "the amd64 package has no xhci98.sys."

    #
    # **The wrong INF under the 64-bit architecture must be refused**, and this
    # is the case that matters: the packager is asked for amd64 and handed
    # src\xhci98.inf, which is what a caller who edited only the -DriverPath
    # would produce. Without -Arch reaching the INF gate this passed, and the
    # media it produced installs on a 32-bit machine.
    #
    $mismatchOut = Join-Path $script:work "pkg-arch-mismatch"
    $r = Invoke-Packager @("-Arch", "amd64", "-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $mismatchOut)
    Assert-True ($r.ExitCode -ne 0) ("the packager staged the 32-bit INF as the amd64 package:`n" + $r.Output)
    Assert-True ($r.Output -match [regex]::Escape("[PATH-MFGDEC]")) `
        ("expected the INF gate's amd64 profile to refuse src\xhci98.inf. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $mismatchOut)) `
        "the refused amd64 package left an output directory behind; a rejected INF must stop the run before staging."

    # And the mirror, which is the likelier mistake: the 64-bit INF staged as
    # the 32-bit package.
    $mismatchOut2 = Join-Path $script:work "pkg-arch-mismatch2"
    $r = Invoke-Packager @("-InfPath", $amd64Inf, "-DriverPath", $driver, "-OutDir", $mismatchOut2)
    Assert-True ($r.ExitCode -ne 0) ("the packager staged the 64-bit INF as the x86 package:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $mismatchOut2)) `
        "the refused x86 package left an output directory behind."

    #
    # **The default paths carry the architecture, x86 included.** A structural
    # check on make-package.ps1 rather than a run, because a run would need the
    # real obj trees; what it locks is that neither default can be architecture-
    # blind again. An untagged out\pkg-release would mean "x86" without saying
    # so, and the file inside it is called xhci98.sys either way - so what a
    # stale directory holds could not be read off its name.
    #
    $packagerText = [System.IO.File]::ReadAllText($packager)
    foreach ($anchor in @(
        @{ Find = '$DriverPath = Join-Path $repo "src\$objDir\$archDir\xhci98.sys"';
           Why  = "the binary path no longer takes the architecture from -Arch, so an amd64 cut would stage the i386 build" },
        @{ Find = '$archInf = if ($Arch -eq "amd64") { "src\xhci98-amd64.inf" } else { "src\xhci98.inf" }';
           Why  = "the INF is no longer selected by -Arch, so one file would be staged for both architectures" },
        @{ Find = '$OutDir = Join-Path $repo "out\pkg-$Flavor-$Arch"';
           Why  = "the default output directory no longer carries the architecture, so two architectures' packages would overwrite each other" }
    )) {
        Assert-True ($packagerText.Contains($anchor.Find)) `
            ("make-package.ps1 no longer contains '" + $anchor.Find + "': " + $anchor.Why + ".")
    }
    # -Arch has to reach BOTH gates. The import gate needs it because MSVC 6.0's
    # dumpbin reads an amd64 image, exits 0 and prints no import section at all,
    # so without the architecture the gate enforces the allowlist against an
    # empty set and passes (design record 11 section 8).
    Assert-True ($packagerText -match [regex]::Escape('$importGate, "-Image", $DriverPath, "-Flavor", $Flavor,')) `
        "make-package.ps1 no longer passes the image and flavour to the import gate as this test recognises it."
    Assert-True ($packagerText -match [regex]::Escape('"-Arch", $Arch)')) `
        "make-package.ps1 no longer passes -Arch to the import gate, so an amd64 image would be gated against an empty import set."
    Assert-True (([regex]::Matches($packagerText, [regex]::Escape('-InfPath $InfPath -Arch $Arch')).Count +
                  [regex]::Matches($packagerText, [regex]::Escape('-InfPath $rootInf -Arch $Arch')).Count) -eq 2) `
        "make-package.ps1 no longer passes -Arch to both of its INF gate runs - the pre-staging one that derives the media layout, and the post-staging one that checks the package."

    #
    # **And the publisher's four directories.** Structural for the same reason:
    # a real cut needs two builds and the git-ignored tools\ staging. What is
    # locked is the naming rule and the two refusals that depend on it.
    #
    Write-Step "the publisher's per-architecture payload"
    foreach ($anchor in @(
        @{ Find = 'Id      = "$f-$a"';
           Why  = "the published directory name no longer joins the flavour and the architecture, so a second architecture would overwrite the first" },
        @{ Find = '$destDir = Join-Path $destRoot $leg.Dir';
           Why  = "the publish destination is no longer the leg's own directory" },
        @{ Find = '$makePackage, "-Flavor", $f, "-Arch", $leg.PkgArch,';
           Why  = "the publisher no longer tells the packager which architecture to build, so every leg would stage the x86 package" },
        @{ Find = 'throw "the release and debug $a binaries are identical - one build was packaged twice."';
           Why  = "the identical-binary refusal no longer compares within an architecture, where it is the only place a collision could happen" },
        @{ Find = '$legCopy = $declaredPublishable[$leg.Arch] + $uasPublishable + $cmPublishable + $oemPublishable';
           Why  = "a flavour directory no longer carries exactly the INF's files, the UAS pair, the connection manager's four files and txtsetup.oem" },
        @{ Find = '$cmPublishable = @("xhcicm.inf", "nt\xhcicmf.inf", "xhcicm.sys", "nt\xhcicm.sys")';
           Why  = "the connection manager's NHI INF, filter INF, binary and the filter INF's own copy of it in nt\ (roadmap-usb4.md task 39-A.1; Vista's and 7's driver store refuses a source outside the INF's directory) are no longer what a flavour directory publishes for it" },
        @{ Find = 'Assert-CmPublished -Sys $cmSys -Leg $leg -Flavor $f -Version $Version';
           Why  = "the published xhcicm.sys is no longer held to the refusals xhciuas.sys is" },
        @{ Find = 'if ($legSigned) { $legCopy += $certPublishable }';
           Why  = "a signed x64 leg no longer publishes xhci98-test.cer beside the binaries it signed (design record 19)" },
        @{ Find = '$pswPublishable = @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")';
           Why  = "the port switchover's user script and its two batch files (roadmap-hcd tasks 36.42 and 36.56) are no longer the files portsw\ carries" },
        @{ Find = '$pswStaged = Publish-PortSwitchScripts -SourceDir (Join-Path $repo "src") -DestRoot $destRoot';
           Why  = "a cut no longer publishes the port switchover's scripts once, from src\, into portsw\ at the version root (roadmap-hcd task 37.5)" },
        @{ Find = 'Assert-PortSwitchLayout -Root $destRoot -Names $pswPublishable -DirName (Get-ToolDirName -Name "portsw")';
           Why  = "a cut no longer holds its staged tree to one tools\portsw\ and no copy in a flavour directory before the publish swap (tasks 37.5 and 38.9)" },
        @{ Find = '-What "the upload set ''$uploadRoot''" -PublishedVersion $pswVersion';
           Why  = "the upload assembly no longer holds the tree to the port switchover layout its version was published with (tasks 37.5 and 38.9)" },
        @{ Find = '$pswVersion = if (Test-Path -LiteralPath (Join-Path $uploadRoot $toolsDirName)) { "" } else { $Version }';
           Why  = "the upload assembly no longer holds a tree with tools\ to the cut's port switchover layout, so a legacy layout beside tools\ would pass (task 38.9)" },
        # The tools under tools\ (roadmap-hcd task 38.9): each staged through
        # Get-ToolDirName, and the layout held at the cut and at the upload.
        @{ Find = '$pswStaged = Publish-PortSwitchScripts -SourceDir (Join-Path $repo "src") -DestRoot $destRoot -Names $pswPublishable -DirName (Get-ToolDirName -Name "portsw")';
           Why  = "a cut no longer publishes the port switchover's scripts into tools\portsw\ (task 38.9)" },
        @{ Find = '$qualDir = Join-Path $destRoot (Get-ToolDirName -Name "xhciqual")';
           Why  = "a cut no longer stages the DOS qualifier into tools\xhciqual\ (task 38.9)" },
        @{ Find = '$snapDir = Join-Path $destRoot (Get-ToolDirName -Name "xhcisnap")';
           Why  = "a cut no longer stages XHCISNAP into tools\xhcisnap\ (task 38.9)" },
        @{ Find = '$toolsLayoutVersion = [version]"2.4.0.0"';
           Why  = "the first version published with tools\ is no longer 2.4.0.0, so a published release could be read at the wrong layout (task 38.9)" },
        @{ Find = '$toolNames = @("xhciqual", "xhcisnap", "portsw", "silent", "xhcicln", "xhciinst", "xhcitr")';
           Why  = "tools\ no longer holds exactly the qualifier, XHCISNAP, the port switchover, the silent install, the cleanup tool, the install tool and the USB tree tool (tasks 38.9, 38.2, 38.8, 40.6 and 41-A.6)" },
        @{ Find = '$cleanDir = Join-Path $destRoot (Get-ToolDirName -Name "xhcicln")';
           Why  = "a cut no longer stages XHCICLN.EXE and XHCICL64.EXE into tools\xhcicln\ (task 38.8)" },
        @{ Find = '$cleanExes = @("XHCICLN.EXE", "XHCICL64.EXE")';
           Why  = "a cut no longer stages both builds of the cleanup tool, the 64-bit one a native build by design (task 38.8, decision D7)" },
        @{ Find = 'Assert-TakesSharedVersion -Path $cleanSource -Macro "XHCICLN_VERSION"';
           Why  = "a cut no longer holds the cleanup tool's version to the shared macro (task 38.8)" },
        @{ Find = '$cleanNewer = @($cleanSources | Where-Object { $_.LastWriteTimeUtc -gt $cleanTime })';
           Why  = "a cut no longer refuses a cleanup tool older than its sources (task 38.8)" },
        @{ Find = 'Write-GeneratedText -Path (Join-Path $cleantoolStaged "NOTICE.TXT")';
           Why  = "a cut no longer writes the NOTICE.TXT recording the cleanup tool's statically linked runtimes (legal-provenance.md section 2a)" },        # The install tool (roadmap-usb4.md task 40.6, design record 23).
        @{ Find = '$instDir = Join-Path $destRoot (Get-ToolDirName -Name "xhciinst")';
           Why  = "a cut no longer stages XHCIINST.EXE and XHCIIN64.EXE into tools\xhciinst\ (task 40.6)" },
        @{ Find = '$instExes = @("XHCIINST.EXE", "XHCIIN64.EXE")';
           Why  = "a cut no longer stages both builds of the install tool, the 64-bit one a native build by design (task 40.6)" },
        @{ Find = 'Copy-Item -LiteralPath (Join-Path $InstallToolDir "INSTALL.BAT") -Destination (Join-Path $destRoot "INSTALL.BAT") -Force';
           Why  = "a cut no longer puts INSTALL.BAT at the version root (task 40.6, design record 23 section 2)" },
        @{ Find = 'Assert-TakesSharedVersion -Path (Join-Path $InstallToolDir "xhciinst.c") -Macro "XHCIINST_VERSION"';
           Why  = "a cut no longer holds the install tool's version to the shared macro (task 40.6)" },
        @{ Find = '$instNewer = @($instSources | Where-Object { $_.LastWriteTimeUtc -gt $instTime })';
           Why  = "a cut no longer refuses an install tool older than its sources (task 40.6)" },
        @{ Find = 'throw "xhciinst\INSTALL.BAT has an LF line ending without CR: MS-DOS 7.1 loses goto labels in such a file."';
           Why  = "a cut no longer refuses an INSTALL.BAT with LF endings (task 40.6)" },
        @{ Find = 'Write-GeneratedText -Path (Join-Path $insttoolStaged "NOTICE.TXT")';
           Why  = "a cut no longer writes the NOTICE.TXT recording the install tool's statically linked runtimes (legal-provenance.md section 2a)" },
        # The USB tree tool (roadmap-usb4.md task 41-A.6, design record 25
        # sections 8 and 9): XHCIINST copies it from tools\xhcitr\.
        @{ Find = '$trayDir = Join-Path $destRoot (Get-ToolDirName -Name "xhcitr")';
           Why  = "a cut no longer stages XHCITR.EXE and XHCITR64.EXE into tools\xhcitr\, where XHCIINST looks for them (task 41-A.6)" },
        @{ Find = '$trayExes = @("XHCITR.EXE", "XHCITR64.EXE")';
           Why  = "a cut no longer stages both builds of the USB tree tool (task 41-A.6)" },
        @{ Find = 'Assert-TakesSharedVersion -Path (Join-Path $TrayToolDir "xhcitr.c") -Macro "XHCITR_VERSION"';
           Why  = "a cut no longer holds the USB tree tool's version to the shared macro, on which XHCIINST's replace-only-when-older rule depends (task 41-A.6)" },
        @{ Find = 'Assert-ToolFileVersion -Path (Join-Path $TrayToolDir $name) -Version $Version';
           Why  = "a cut no longer holds each USB tree tool EXE's numeric file version to the release's, on which XHCIINST's replace-only-when-older rule depends (task 41-A.6)" },
        @{ Find = '$trayNewer = @($traySources | Where-Object { $_.LastWriteTimeUtc -gt $trayTime })';
           Why  = "a cut no longer refuses a USB tree tool older than its sources (task 41-A.6)" },
        @{ Find = '@(Get-Item -LiteralPath (Join-Path $InstallToolDir "inst_core.c")) +';
           Why  = "the USB tree tool's freshness no longer counts the pure half it links from xhciinst\ (task 41-A.6)" },
        @{ Find = 'Write-GeneratedText -Path (Join-Path $traytoolStaged "readme.txt") -Lines $trayLines';
           Why  = "a cut no longer writes the USB tree tool's readme.txt (task 41-A.6)" },
        @{ Find = 'Write-GeneratedText -Path (Join-Path $traytoolStaged "NOTICE.TXT")';
           Why  = "a cut no longer writes the NOTICE.TXT recording the USB tree tool's statically linked runtimes (legal-provenance.md section 2a)" },
        @{ Find = 'if ($null -eq $traytoolStaged) { $missingTools += "TOOLS\XHCITR\ (the xHCI98 USB Tree of section 4)" }';
           Why  = "a cut without the USB tree tool no longer says so in its readme (task 41-A.6)" },
        @{ Find = 'INSTALL /notray installs it without starting it at';
           Why  = "the readme's section 4 no longer describes the xHCI98 USB Tree INSTALL.BAT installs, or /notray (task 41-A.6)" },
        @{ Find = '$silentStaged = Publish-SilentInstallScripts -SourceDir (Join-Path $repo "src") -DestRoot $destRoot -Map $silentPublishable -DirName (Get-ToolDirName -Name "silent")';
           Why  = "a cut no longer publishes the silent install's list and scripts once, from src\, into tools\silent\ (task 38.2)" },
        @{ Find = 'Assert-SilentInstallLayout -Root $destRoot -Map $silentPublishable -What "the staged release ''$destRoot''"';
           Why  = "a cut no longer holds its staged tree to tools\silent\ alone before the publish swap (task 38.2)" },
        @{ Find = 'Assert-SilentInstallLayout -Root $uploadRoot -Map $silentPublishable -What "the upload set ''$uploadRoot''" -PublishedVersion $Version';
           Why  = "the upload assembly no longer holds the tree to the silent install's layout (task 38.2)" },
        @{ Find = 'Assert-SilentInstallContent -Root $destRoot -Map $silentPublishable -What "the staged release ''$destRoot''" -Reference $repo';
           Why  = "a cut no longer holds the silent install's staged files to the checkers and the repository's own bytes (task 38.2)" },
        @{ Find = 'Assert-SilentInstallContent -Root $uploadRoot -Map $silentPublishable -What "the upload set ''$uploadRoot''"';
           Why  = "the upload assembly no longer holds the silent install's files it archives to the checkers (task 38.2)" },
        @{ Find = 'Assert-ToolsLayout -Root $uploadRoot -What "the upload set ''$uploadRoot''" -PublishedVersion $Version';
           Why  = "the upload assembly no longer holds the tree to tools\ at the layout its version was published with (task 38.9)" },
        @{ Find = '-DirName (Get-ToolDirName -Name "portsw" -Root $uploadRoot)';
           Why  = "the upload assembly no longer looks for the port switchover's scripts where the tree's own layout puts them (task 38.9)" }
    )) {
        Assert-True ($releaserText.Contains($anchor.Find)) `
            ("make-release.ps1 no longer contains '" + $anchor.Find + "': " + $anchor.Why + ".")
    }
    # The USB tree tool's file version check, EXECUTED (task 41-A.6): a
    # system DLL's own version is accepted, any other is refused, and so is
    # a file with no version resource at all.
    Write-Step "a staged USB tree tool carries the release's file version"
    $tfvAt = $releaserText.IndexOf("function Assert-ToolFileVersion")
    Assert-True ($tfvAt -ge 0) "make-release.ps1 no longer has Assert-ToolFileVersion (task 41-A.6)."
    if ($tfvAt -ge 0) {
        Invoke-Expression $releaserText.Substring($tfvAt, $releaserText.IndexOf("`n}", $tfvAt) - $tfvAt + 2)
        $tfvDll = Join-Path $env:SystemRoot "System32\kernel32.dll"
        $tfvInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($tfvDll)
        $tfvOwn = "{0}.{1}.{2}.{3}" -f $tfvInfo.FileMajorPart, $tfvInfo.FileMinorPart, $tfvInfo.FileBuildPart, $tfvInfo.FilePrivatePart
        $refused = $null
        try { Assert-ToolFileVersion -Path $tfvDll -Version $tfvOwn } catch { $refused = $_.Exception.Message }
        Assert-True ($null -eq $refused) "Assert-ToolFileVersion refused a file at its own version $($tfvOwn): $refused"
        $refused = $null
        try { Assert-ToolFileVersion -Path $tfvDll -Version "3.0.0.0" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -ne $refused -and $refused -match "carries the file version") "Assert-ToolFileVersion accepted a file whose version is not the release's."
        $tfvNone = Join-Path $script:work "tfv-none.exe"
        [System.IO.File]::WriteAllText($tfvNone, "no version resource")
        $refused = $null
        try { Assert-ToolFileVersion -Path $tfvNone -Version "3.0.0.0" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -ne $refused) "Assert-ToolFileVersion accepted a file with no version resource."
    }

    # And none of the scripts rides in a flavour directory or a package again.
    Assert-True (-not ($releaserText -match '\$pswPublishable\s*\+|\+\s*\$pswPublishable')) `
        "make-release.ps1 adds the port switchover's scripts to a flavour directory's or a package's file set again; since roadmap-hcd task 37.5 they are published in portsw\ alone."
    # No tool is staged at the version root by a literal name again (task
    # 38.9): every staging goes through Get-ToolDirName.
    foreach ($bad in @('Join-Path $destRoot "xhciqual"', 'Join-Path $destRoot "xhcisnap"', 'Join-Path $destRoot "portsw"', '$pswDirName')) {
        Assert-True (-not $releaserText.Contains($bad)) `
            "make-release.ps1 contains '$bad' again, which stages a tool at the version root; since roadmap-hcd task 38.9 the tools are published under tools\."
    }
    # And the tools' layout is held at the cut before the publish swap, beside
    # the port switchover's check, and on every cut rather than in the
    # -SkipUploadSet branch (task 38.9).
    $tlAt = $releaserText.IndexOf('Assert-ToolsLayout -Root $destRoot')
    $tlPswAt = $releaserText.IndexOf('Assert-PortSwitchLayout -Root $destRoot')
    $tlSwapAt = $releaserText.IndexOf('Move-Item -LiteralPath $destRoot -Destination $finalRoot')
    $tlAsideAt = $releaserText.IndexOf('Move-Item -LiteralPath $finalRoot -Destination $asideRoot')
    Assert-True ($tlAt -ge 0) "make-release.ps1 no longer holds the staged release to the tools\ layout (roadmap-hcd task 38.9)."
    if ($tlAt -ge 0) {
        Assert-True ($tlSwapAt -gt $tlAt -and $tlAsideAt -gt $tlAt) `
            "make-release.ps1 holds the staged release to the tools\ layout after the -Force aside or the publish swap, so a refusal would leave releases\ changed (roadmap-hcd task 38.9)."
        Assert-True ($tlPswAt -gt $tlAt -and ($tlPswAt - $tlAt) -lt 400) `
            "make-release.ps1's tools\ layout check is no longer beside the port switchover's at the publish step (roadmap-hcd task 38.9)."
        $tlLine = ($releaserText.Substring(0, $tlAt) -split "`n")[-1]
        Assert-True ($tlLine.TrimEnd("`r") -eq '    ') "make-release.ps1's tools\ layout check at the cut is not at the publish step's own level."
    }

    #
    # **The portsw\ layout itself, EXECUTED** (roadmap-hcd task 37.5): the
    # two functions loaded from make-release.ps1's text, as the test-client
    # check below is, and driven with fixture trees. Publish copies the three
    # byte for byte and refuses a source missing one; the layout check takes
    # a tree with portsw\ holding the three, and refuses a copy in a flavour
    # directory, a missing script, a stranger in portsw\, and no portsw\ at
    # all - which -PublishedVersion accepts only in the layout that version
    # was published with: none before 2.3.0.0, one set per flavour directory
    # in 2.3.0.0, never after it. The cut fixture is a current cut's, with the
    # tools under tools\ (roadmap-hcd task 38.9), so it names the directory
    # "tools\portsw" as Get-ToolDirName does at the cut; the published layouts
    # name "portsw" at the version root, where every version before 2.4.0.0
    # has it.
    Write-Step "the port switchover's scripts are published in portsw\ alone"
    $pswFns = @()
    foreach ($fnName in @("Publish-PortSwitchScripts", "Assert-PortSwitchLayout")) {
        $fnAt = $releaserText.IndexOf("function $fnName")
        Assert-True ($fnAt -ge 0) "make-release.ps1 no longer has $fnName (roadmap-hcd task 37.5)."
        if ($fnAt -ge 0) { $pswFns += $releaserText.Substring($fnAt, $releaserText.IndexOf("`n}", $fnAt) - $fnAt + 2) }
    }
    if ($pswFns.Count -eq 2) {
        foreach ($fnText in $pswFns) { Invoke-Expression $fnText }
        $pswNames = @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")
        $pswRoot = Join-Path $script:work "pswlayout"
        $cutRoot = Join-Path $pswRoot "cut"
        foreach ($d in @("release-x86", "debug-x86", "release-x64", "debug-x64", "tools\xhcisnap")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $cutRoot $d) | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $cutRoot "$d\xhci98.inf"), "; stand-in")
        }
        $pubDir = Publish-PortSwitchScripts -SourceDir (Join-Path $repo "src") -DestRoot $cutRoot -Names $pswNames -DirName "tools\portsw"
        Assert-True ($pubDir -eq (Join-Path $cutRoot "tools\portsw")) "Publish-PortSwitchScripts did not return the portsw\ directory it wrote: $pubDir"
        foreach ($name in $pswNames) {
            $pubFile = Join-Path $cutRoot "tools\portsw\$name"
            Assert-True ((Test-Path -LiteralPath $pubFile) -and
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes($pubFile))) -eq
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes((Join-Path $repo "src\$name"))))) `
                "Publish-PortSwitchScripts did not publish src\$name byte for byte into portsw\."
        }
        $refused = $null
        try { Assert-PortSwitchLayout -Root $cutRoot -Names $pswNames -DirName "tools\portsw" -What "fixture cut" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -eq $refused) "Assert-PortSwitchLayout refused a tree with the three in portsw\ alone: $refused"

        # A source missing one: refused, naming it, and nothing written.
        $loneSrc = Join-Path $pswRoot "lone-src"
        New-Item -ItemType Directory -Force -Path $loneSrc | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo "src\portsw.vbs") -Destination $loneSrc
        Copy-Item -LiteralPath (Join-Path $repo "src\portsw-on.bat") -Destination $loneSrc
        $loneDest = Join-Path $pswRoot "lone-dest"
        New-Item -ItemType Directory -Force -Path $loneDest | Out-Null
        $refused = $null
        try { $null = Publish-PortSwitchScripts -SourceDir $loneSrc -DestRoot $loneDest -Names $pswNames -DirName "tools\portsw" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -ne $refused -and $refused -match "portsw-off\.bat") "Publish-PortSwitchScripts did not refuse a source with no portsw-off.bat, naming it: $refused"
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $loneDest "tools\portsw"))) "Publish-PortSwitchScripts wrote portsw\ before refusing a source missing a script."

        # The faults, each on a copy of the good tree.
        $faultCases = @(
            @{ Name = "flavour-copy"; Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\portsw\portsw.vbs") -Destination (Join-Path $t "release-x64\portsw.vbs") }; Says = "release-x64" },
            @{ Name = "deep-copy";    Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\portsw\portsw-on.bat") -Destination (Join-Path $t "tools\xhcisnap\PORTSW-ON.BAT") }; Says = "xhcisnap" },
            @{ Name = "missing";      Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\portsw\portsw-off.bat") }; Says = "portsw-off\.bat is missing" },
            @{ Name = "stranger";     Do = { param($t) [System.IO.File]::WriteAllText((Join-Path $t "tools\portsw\readme.txt"), "x") }; Says = "readme\.txt" },
            @{ Name = "no-dir";       Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\portsw") -Recurse }; Says = "no tools\\portsw" }
        )
        foreach ($c in $faultCases) {
            $t = Join-Path $pswRoot ("fault-" + $c.Name)
            Copy-Item -LiteralPath $cutRoot -Destination $t -Recurse
            & $c.Do $t
            $refused = $null
            try { Assert-PortSwitchLayout -Root $t -Names $pswNames -DirName "tools\portsw" -What "fixture $($c.Name)" } catch { $refused = $_.Exception.Message }
            Assert-True ($null -ne $refused -and $refused -match $c.Says) ("Assert-PortSwitchLayout on the '{0}' fixture: expected a refusal matching '{1}', got: {2}" -f $c.Name, $c.Says, $refused)
        }
        # Given a published version, a tree WITH portsw\ is still held to the
        # rule, whatever the version.
        foreach ($v in @("2.2.0.0", "2.3.0.0", "2.3.0.1")) {
            $refused = $null
            try { Assert-PortSwitchLayout -Root (Join-Path $pswRoot "fault-flavour-copy") -Names $pswNames -DirName "tools\portsw" -What "fixture" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
            Assert-True ($null -ne $refused) "Assert-PortSwitchLayout -PublishedVersion $v accepted a tree with portsw\ and a copy in a flavour directory."
            $refused = $null
            try { Assert-PortSwitchLayout -Root $cutRoot -Names $pswNames -DirName "tools\portsw" -What "fixture" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-PortSwitchLayout -PublishedVersion $v refused a tree with the three in portsw\ alone: $refused"
        }

        # The layouts already published, each accepted for its own version
        # only (Codex review round 1): none before 2.3.0.0, all three at the
        # root of every flavour directory in 2.3.0.0, and no other layout
        # after it; and a damaged one of either refused.
        $legacyNone = Join-Path $pswRoot "legacy-none"
        $legacyFlavour = Join-Path $pswRoot "legacy-2300"
        foreach ($d in @("release-x86", "debug-x86", "xhciqual")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $legacyNone $d) | Out-Null
            New-Item -ItemType Directory -Force -Path (Join-Path $legacyFlavour $d) | Out-Null
            if ($d -eq "xhciqual") { continue }
            [System.IO.File]::WriteAllText((Join-Path $legacyNone "$d\xhci98.inf"), "; stand-in")
            [System.IO.File]::WriteAllText((Join-Path $legacyFlavour "$d\xhci98.inf"), "; stand-in")
            foreach ($name in $pswNames) {
                Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $legacyFlavour "$d\$name")
            }
        }
        $legacyCases = @(
            @{ Tree = $legacyNone;    Version = "2.2.0.0"; Want = $true },
            @{ Tree = $legacyNone;    Version = "2.3.0.0"; Want = $false },
            @{ Tree = $legacyNone;    Version = "2.3.0.1"; Want = $false },
            @{ Tree = $legacyFlavour; Version = "2.3.0.0"; Want = $true },
            @{ Tree = $legacyFlavour; Version = "2.2.0.0"; Want = $false },
            @{ Tree = $legacyFlavour; Version = "2.4.0.0"; Want = $false }
        )
        foreach ($c in $legacyCases) {
            $refused = $null
            try { Assert-PortSwitchLayout -Root $c.Tree -Names $pswNames -DirName "portsw" -What "fixture" -PublishedVersion $c.Version } catch { $refused = $_.Exception.Message }
            Assert-True (($null -eq $refused) -eq $c.Want) ("Assert-PortSwitchLayout -PublishedVersion {0} on '{1}': accepted={2}, expected {3}. {4}" -f $c.Version, (Split-Path -Leaf $c.Tree), ($null -eq $refused), $c.Want, $refused)
        }
        $damaged = @(
            @{ Name = "2300-missing";  Base = $legacyFlavour; Version = "2.3.0.0"; Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "debug-x86\portsw-off.bat") } },
            @{ Name = "2300-stray";    Base = $legacyFlavour; Version = "2.3.0.0"; Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "debug-x86\portsw.vbs") -Destination (Join-Path $t "xhciqual\portsw.vbs") } },
            @{ Name = "none-stray";    Base = $legacyNone;    Version = "2.2.0.0"; Do = { param($t) Copy-Item -LiteralPath (Join-Path $repo "src\portsw.vbs") -Destination (Join-Path $t "release-x86\portsw.vbs") } },
            @{ Name = "none-portsw-file"; Base = $legacyNone; Version = "2.2.0.0"; Do = { param($t) [System.IO.File]::WriteAllText((Join-Path $t "portsw"), "x") } }
        )
        foreach ($c in $damaged) {
            $t = Join-Path $pswRoot ("damaged-" + $c.Name)
            Copy-Item -LiteralPath $c.Base -Destination $t -Recurse
            & $c.Do $t
            $refused = $null
            try { Assert-PortSwitchLayout -Root $t -Names $pswNames -DirName "portsw" -What "fixture" -PublishedVersion $c.Version } catch { $refused = $_.Exception.Message }
            Assert-True ($null -ne $refused) "Assert-PortSwitchLayout -PublishedVersion $($c.Version) accepted the damaged '$($c.Name)' layout."
        }
    }

    #
    # **The tools\ layout itself, EXECUTED** (roadmap-hcd task 38.9): the
    # two functions and the three values they read, loaded from
    # make-release.ps1's text as the port switchover's are above, and driven
    # with fixture trees. Get-ToolDirName answers tools\<name> for the cut and
    # for a tree with tools\, and <name> for one without. Assert-ToolsLayout
    # takes tools\ holding only the three tools' directories, and refuses a
    # tool's directory at the root beside it, a stranger in it, and tools as a
    # file - at any version, since a tree with tools\ is held to it whatever
    # its version says. A tree without tools\ it refuses for the cut and from
    # 2.4.0.0, and accepts before 2.4.0.0, where every published release has
    # its tools at the version root and is never rewritten.
    Write-Step "the release's tools are published under tools\"
    $tlTexts = @()
    foreach ($varName in @("toolsDirName", "toolsLayoutVersion", "toolNames")) {
        $m = [regex]::Match($releaserText, '(?m)^\$' + $varName + ' = [^\r\n]+')
        Assert-True $m.Success "make-release.ps1 no longer sets `$$varName at its top level (roadmap-hcd task 38.9)."
        if ($m.Success) { $tlTexts += $m.Value }
    }
    foreach ($fnName in @("Get-ToolDirName", "Assert-ToolsLayout")) {
        $fnAt = $releaserText.IndexOf("function $fnName")
        Assert-True ($fnAt -ge 0) "make-release.ps1 no longer has $fnName (roadmap-hcd task 38.9)."
        if ($fnAt -ge 0) { $tlTexts += $releaserText.Substring($fnAt, $releaserText.IndexOf("`n}", $fnAt) - $fnAt + 2) }
    }
    if ($tlTexts.Count -eq 5) {
        foreach ($tlText in $tlTexts) { Invoke-Expression $tlText }
        $tlRoot = Join-Path $script:work "toolslayout"
        $tlCut = Join-Path $tlRoot "cut"
        foreach ($d in @("release-x86", "debug-x86", "release-x64", "debug-x64")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $tlCut $d) | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $tlCut "$d\xhci98.inf"), "; stand-in")
        }
        foreach ($name in @("xhciqual", "xhcisnap", "portsw", "silent", "xhcicln", "xhciinst", "xhcitr")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $tlCut "tools\$name") | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $tlCut "tools\$name\readme.txt"), "stand-in")
        }
        [System.IO.File]::WriteAllText((Join-Path $tlCut "readme.txt"), "stand-in")
        [System.IO.File]::WriteAllText((Join-Path $tlCut "LICENSE"), "stand-in")
        # The root layout every version before 2.4.0.0 was published with.
        $tlLegacy = Join-Path $tlRoot "legacy"
        foreach ($d in @("release-x86", "debug-x86", "xhciqual", "xhcisnap", "portsw")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $tlLegacy $d) | Out-Null
        }

        # Get-ToolDirName: the cut, a tree with tools\, a tree without.
        foreach ($c in @(
            @{ Root = "";        Want = "tools\portsw" },
            @{ Root = $tlCut;    Want = "tools\portsw" },
            @{ Root = $tlLegacy; Want = "portsw" }
        )) {
            $got = Get-ToolDirName -Name "portsw" -Root $c.Root
            Assert-True ($got -eq $c.Want) ("Get-ToolDirName -Name portsw -Root '{0}' returned '{1}', not '{2}'." -f $c.Root, $got, $c.Want)
        }
        Assert-True ((Get-ToolDirName -Name "xhciqual") -eq "tools\xhciqual") "Get-ToolDirName does not put the qualifier under tools\ at the cut."
        Assert-True ((Get-ToolDirName -Name "xhcisnap") -eq "tools\xhcisnap") "Get-ToolDirName does not put XHCISNAP under tools\ at the cut."
        Assert-True ((Get-ToolDirName -Name "xhcicln") -eq "tools\xhcicln") "Get-ToolDirName does not put the cleanup tool under tools\ at the cut."
        Assert-True ((Get-ToolDirName -Name "xhcitr") -eq "tools\xhcitr") "Get-ToolDirName does not put the USB tree tool under tools\ at the cut."

        # The accepted layout, for the cut and for every published version.
        foreach ($v in @("", "2.3.1.0", "2.4.0.0", "2.4.0.1")) {
            $refused = $null
            try { Assert-ToolsLayout -Root $tlCut -What "fixture cut" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-ToolsLayout -PublishedVersion '$v' refused the three tools under tools\ alone: $refused"
        }

        # The faults, each on a copy of the good tree, refused at the cut and
        # at a version before 2.4.0.0 alike: tools\ is strict wherever it is.
        $tlFaults = @(
            @{ Name = "root-tool";    Do = { param($t) New-Item -ItemType Directory -Path (Join-Path $t "xhcisnap") | Out-Null }; Says = "xhcisnap\\ is at the version root" },
            @{ Name = "root-portsw";  Do = { param($t) Move-Item -LiteralPath (Join-Path $t "tools\portsw") -Destination (Join-Path $t "portsw") }; Says = "portsw\\ is at the version root" },
            @{ Name = "root-xhcicln"; Do = { param($t) Move-Item -LiteralPath (Join-Path $t "tools\xhcicln") -Destination (Join-Path $t "xhcicln") }; Says = "xhcicln\\ is at the version root" },
            @{ Name = "root-xhcitr";  Do = { param($t) Move-Item -LiteralPath (Join-Path $t "tools\xhcitr") -Destination (Join-Path $t "xhcitr") }; Says = "xhcitr\\ is at the version root" },
            @{ Name = "stranger-file"; Do = { param($t) [System.IO.File]::WriteAllText((Join-Path $t "tools\readme.txt"), "x") }; Says = "tools\\readme\.txt is not one of" },
            @{ Name = "stranger-dir"; Do = { param($t) New-Item -ItemType Directory -Path (Join-Path $t "tools\release-x86") | Out-Null }; Says = "tools\\release-x86 is not one of" },
            @{ Name = "tool-as-file"; Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\xhciqual") -Recurse; [System.IO.File]::WriteAllText((Join-Path $t "tools\xhciqual"), "x") }; Says = "tools\\xhciqual is not one of" },
            @{ Name = "tools-file";   Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools") -Recurse; [System.IO.File]::WriteAllText((Join-Path $t "tools"), "x") }; Says = "tools is a file" }
        )
        foreach ($c in $tlFaults) {
            $t = Join-Path $tlRoot ("fault-" + $c.Name)
            Copy-Item -LiteralPath $tlCut -Destination $t -Recurse
            & $c.Do $t
            foreach ($v in @("", "2.3.1.0", "2.4.0.0")) {
                $refused = $null
                try { Assert-ToolsLayout -Root $t -What "fixture $($c.Name)" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
                Assert-True ($null -ne $refused -and $refused -match $c.Says) ("Assert-ToolsLayout -PublishedVersion '{0}' on the '{1}' fixture: expected a refusal matching '{2}', got: {3}" -f $v, $c.Name, $c.Says, $refused)
            }
        }

        # No tools\ at all: refused for the cut and from 2.4.0.0, accepted
        # before it, where it is how every release was published.
        foreach ($c in @(
            @{ Version = "";        Want = $false },
            @{ Version = "2.4.0.0"; Want = $false },
            @{ Version = "2.4.1.0"; Want = $false },
            @{ Version = "2.3.1.0"; Want = $true },
            @{ Version = "2.2.0.0"; Want = $true }
        )) {
            $refused = $null
            try { Assert-ToolsLayout -Root $tlLegacy -What "fixture legacy" -PublishedVersion $c.Version } catch { $refused = $_.Exception.Message }
            Assert-True (($null -eq $refused) -eq $c.Want) ("Assert-ToolsLayout -PublishedVersion '{0}' on a tree with no tools\: accepted={1}, expected {2}. {3}" -f $c.Version, ($null -eq $refused), $c.Want, $refused)
            if (-not $c.Want -and $null -ne $refused) {
                Assert-True ($refused -match "has no tools\\ directory") "Assert-ToolsLayout refused a tree with no tools\ on other terms: $refused"
            }
        }

        # **A tree with tools\ is held to the cut's port switchover layout**,
        # whatever its version (Codex review of task 38.9): the upload
        # assembly's own $pswVersion line, taken from make-release.ps1's text
        # and evaluated here, then Assert-PortSwitchLayout called as the
        # assembly calls it. Without it, a tree with tools\ but no
        # tools\portsw\ falls through to the legacy layouts at 2.3.0.0 and
        # before, and a copy in every flavour directory or none at all would
        # pass. The trees without tools\ keep their legacy layouts.
        $pvMatch = [regex]::Match($releaserText, '(?m)^ *(\$pswVersion = [^\r\n]+)')
        Assert-True $pvMatch.Success "make-release.ps1's upload assembly no longer derives `$pswVersion from the tree's tools\ (roadmap-hcd task 38.9)."
        if ($pvMatch.Success -and $pswFns.Count -eq 2) {
            $pswNames = @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")
            $tlMixed = Join-Path $tlRoot "tools-legacy-2300"
            $tlMixedNone = Join-Path $tlRoot "tools-legacy-none"
            foreach ($tree in @($tlMixed, $tlMixedNone)) {
                New-Item -ItemType Directory -Force -Path (Join-Path $tree "tools\xhciqual") | Out-Null
                foreach ($d in @("release-x86", "debug-x86")) {
                    New-Item -ItemType Directory -Force -Path (Join-Path $tree $d) | Out-Null
                    [System.IO.File]::WriteAllText((Join-Path $tree "$d\xhci98.inf"), "; stand-in")
                    if ($tree -ne $tlMixed) { continue }
                    foreach ($name in $pswNames) {
                        Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $tree "$d\$name")
                    }
                }
            }
            # And a tree in task 38.9's layout, accepted at any version.
            $tlGood = Join-Path $tlRoot "tools-good"
            Copy-Item -LiteralPath $tlCut -Destination $tlGood -Recurse
            Remove-Item -LiteralPath (Join-Path $tlGood "tools\portsw\readme.txt")
            foreach ($name in $pswNames) {
                Copy-Item -LiteralPath (Join-Path $repo "src\$name") -Destination (Join-Path $tlGood "tools\portsw\$name")
            }
            foreach ($c in @(
                @{ Tree = $tlMixed;     Version = "2.3.0.0"; Want = $false },
                @{ Tree = $tlMixedNone; Version = "2.2.0.0"; Want = $false },
                @{ Tree = $tlGood;      Version = "2.3.0.0"; Want = $true },
                @{ Tree = $tlGood;      Version = "2.4.0.0"; Want = $true },
                @{ Tree = (Join-Path $pswRoot "legacy-2300"); Version = "2.3.0.0"; Want = $true },
                @{ Tree = (Join-Path $pswRoot "legacy-none"); Version = "2.2.0.0"; Want = $true }
            )) {
                # In a child scope, so the assembly's own names stay its own.
                $refused = & {
                    $uploadRoot = $c.Tree
                    $Version = $c.Version
                    Invoke-Expression $pvMatch.Groups[1].Value
                    try {
                        Assert-ToolsLayout -Root $uploadRoot -What "fixture" -PublishedVersion $Version
                        Assert-PortSwitchLayout -Root $uploadRoot -Names $pswNames -DirName (Get-ToolDirName -Name "portsw" -Root $uploadRoot) `
                                                -What "fixture" -PublishedVersion $pswVersion
                        $null
                    } catch { $_.Exception.Message }
                }
                Assert-True (($null -eq $refused) -eq $c.Want) ("the upload assembly's layout checks at {0} on '{1}': accepted={2}, expected {3}. {4}" -f $c.Version, (Split-Path -Leaf $c.Tree), ($null -eq $refused), $c.Want, $refused)
            }
        }
    }
    #
    # **The silent install's layout, EXECUTED** (roadmap-hcd task 38.2): the
    # map and the two functions loaded from make-release.ps1's text and
    # driven with fixture trees. Publish copies the four under their
    # published names, byte for byte, and refuses a source missing one; the
    # layout check takes tools\silent\ holding the four alone, and refuses a
    # missing one, a stranger, a lower-case SILENT.REG, a copy elsewhere and
    # a tree with tools\ but no tools\silent\, at any version; a tree without
    # tools\ it accepts only before 2.4.0.0 and only with none of them.
    Write-Step "the silent install's list and scripts are published in tools\silent\ alone"
    $siTexts = @()
    foreach ($varName in @("toolsDirName", "toolsLayoutVersion")) {
        $m = [regex]::Match($releaserText, '(?m)^\$' + $varName + ' = [^\r\n]+')
        if ($m.Success) { $siTexts += $m.Value }
    }
    $siMap = [regex]::Match($releaserText, '(?ms)^\$silentPublishable = @\{.*?^\}')
    Assert-True $siMap.Success "make-release.ps1 no longer sets `$silentPublishable at its top level (roadmap-hcd task 38.2)."
    if ($siMap.Success) { $siTexts += $siMap.Value }
    foreach ($fnName in @("Publish-SilentInstallScripts", "Assert-SilentInstallLayout", "Assert-SilentInstallContent")) {
        $fnAt = $releaserText.IndexOf("function $fnName")
        Assert-True ($fnAt -ge 0) "make-release.ps1 no longer has $fnName (roadmap-hcd task 38.2)."
        if ($fnAt -ge 0) { $siTexts += $releaserText.Substring($fnAt, $releaserText.IndexOf("`n}", $fnAt) - $fnAt + 2) }
    }
    if ($siTexts.Count -eq 6) {
        foreach ($siText in $siTexts) { Invoke-Expression $siText }
        $siWant = @{ "silent.reg" = "SILENT.REG"; "silent.vbs" = "SILENT.VBS"; "silent-on.bat" = "silent-on.bat"; "silent-off.bat" = "silent-off.bat" }
        Assert-True ($silentPublishable.Count -eq 4) "make-release.ps1's `$silentPublishable no longer names four files (design record 13 section 6.11.8)."
        foreach ($k in $siWant.Keys) {
            Assert-True ($silentPublishable.ContainsKey($k) -and $silentPublishable[$k] -ceq $siWant[$k]) `
                "make-release.ps1 no longer publishes src\$k as $($siWant[$k]) (design record 13 section 6.11.8)."
        }
        $siRoot = Join-Path $script:work "silentlayout"
        $siCut = Join-Path $siRoot "cut"
        foreach ($d in @("release-x86", "debug-x86", "tools\xhcisnap", "tools\portsw")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $siCut $d) | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $siCut "$d\readme.txt"), "stand-in")
        }
        $siDir = Publish-SilentInstallScripts -SourceDir (Join-Path $repo "src") -DestRoot $siCut -Map $silentPublishable -DirName "tools\silent"
        Assert-True ($siDir -eq (Join-Path $siCut "tools\silent")) "Publish-SilentInstallScripts did not return the tools\silent\ directory it wrote: $siDir"
        $siGot = @(Get-ChildItem -LiteralPath $siDir -File | ForEach-Object { $_.Name } | Sort-Object)
        Assert-True (($siGot -join ",") -ceq ((@($siWant.Values) | Sort-Object) -join ",")) "Publish-SilentInstallScripts put '$($siGot -join ', ')' in tools\silent\."
        foreach ($k in $siWant.Keys) {
            $pubFile = Join-Path $siDir $siWant[$k]
            Assert-True ((Test-Path -LiteralPath $pubFile) -and
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes($pubFile))) -eq
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes((Join-Path $repo "src\$k"))))) `
                "Publish-SilentInstallScripts did not publish src\$k byte for byte."
        }
        foreach ($v in @("", "2.4.0.0", "2.4.1.0")) {
            $refused = $null
            try { Assert-SilentInstallLayout -Root $siCut -Map $silentPublishable -What "fixture cut" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-SilentInstallLayout -PublishedVersion '$v' refused the four in tools\silent\ alone: $refused"
        }
        # **What they say, EXECUTED** (Codex review of 38.2, round 1): the
        # layout check passes four files by name alone, so a zero-byte
        # SILENT.REG passed it. Each malformed one is refused in both modes,
        # naming its file; a stale SILENT.VBS (no checker of its own) by the
        # cut's byte comparison alone. The reference is the repository.
        Write-Step "a staged or published tools\silent\ is held to the checkers"
        foreach ($ref in @("", $repo)) {
            $refused = $null
            try { Assert-SilentInstallContent -Root $siCut -Map $silentPublishable -What "fixture cut" -Reference $ref } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-SilentInstallContent -Reference '$ref' refused the published four: $refused"
        }
        $siContent = @(
            @{ Name = "empty-reg"; File = "SILENT.REG";     Edit = { param($s) "" };                                                   Stale = $false },
            @{ Name = "lf-reg";    File = "SILENT.REG";     Edit = { param($s) $s.Replace("`r`n", "`n") };                             Stale = $false },
            @{ Name = "lf-on";     File = "silent-on.bat";  Edit = { param($s) $s.Replace("`r`n", "`n") };                             Stale = $false },
            @{ Name = "swap-off";  File = "silent-off.bat"; Edit = { param($s) $s.Replace("silent.vbs 0", "silent.vbs 1") };           Stale = $false },
            @{ Name = "stale-vbs"; File = "SILENT.VBS";     Edit = { param($s) $s + "' stale`r`n" };                                   Stale = $true }
        )
        $siLatin1 = [System.Text.Encoding]::GetEncoding(28591)
        foreach ($c in $siContent) {
            $t = Join-Path $siRoot ("content-" + $c.Name)
            Copy-Item -LiteralPath $siCut -Destination $t -Recurse
            $f = Join-Path $t ("tools\silent\" + $c.File)
            [System.IO.File]::WriteAllBytes($f, $siLatin1.GetBytes((& $c.Edit $siLatin1.GetString([System.IO.File]::ReadAllBytes($f)))))
            foreach ($ref in @("", $repo)) {
                $refused = $null
                try { Assert-SilentInstallContent -Root $t -Map $silentPublishable -What "fixture $($c.Name)" -Reference $ref } catch { $refused = $_.Exception.Message }
                $want = ($ref -ne "") -or (-not $c.Stale)
                Assert-True (($null -ne $refused) -eq $want -and ($null -eq $refused -or $refused -match [regex]::Escape($c.File))) `
                    ("Assert-SilentInstallContent -Reference '{0}' on the '{1}' fixture: refused={2}, expected {3}, naming {4}. {5}" -f $ref, $c.Name, ($null -ne $refused), $want, $c.File, $refused)
            }
        }
        # Together or not at all.
        $siLone = Join-Path $siRoot "lone-src"
        $siLoneDest = Join-Path $siRoot "lone-dest"
        New-Item -ItemType Directory -Force -Path $siLone | Out-Null
        foreach ($k in @("silent.reg", "silent.vbs", "silent-on.bat")) { Copy-Item -LiteralPath (Join-Path $repo "src\$k") -Destination (Join-Path $siLone $k) }
        $refused = $null
        try { $null = Publish-SilentInstallScripts -SourceDir $siLone -DestRoot $siLoneDest -Map $silentPublishable -DirName "tools\silent" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -ne $refused -and $refused -match "silent-off\.bat") "Publish-SilentInstallScripts did not refuse a source with no silent-off.bat, naming it: $refused"
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $siLoneDest "tools\silent"))) "Publish-SilentInstallScripts wrote tools\silent\ before refusing a source missing a file."
        # The faults, each on a copy of the good tree, at the cut and at any version.
        $siFaults = @(
            @{ Name = "missing";   Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\silent\silent-off.bat") }; Says = "silent-off\.bat is missing" },
            @{ Name = "stranger";  Do = { param($t) [System.IO.File]::WriteAllText((Join-Path $t "tools\silent\readme.txt"), "x") }; Says = "readme\.txt is not one of" },
            @{ Name = "lowercase"; Do = { param($t) Rename-Item -LiteralPath (Join-Path $t "tools\silent\SILENT.REG") -NewName "x.tmp"; Rename-Item -LiteralPath (Join-Path $t "tools\silent\x.tmp") -NewName "silent.reg" }; Says = "SILENT\.REG is missing or not so named" },
            @{ Name = "copy";      Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\silent\SILENT.REG") -Destination (Join-Path $t "release-x86\SILENT.REG") }; Says = "release-x86\\SILENT\.REG is a copy" },
            @{ Name = "no-dir";    Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\silent") -Recurse }; Says = "tools\\silent\\ is missing" }
        )
        foreach ($c in $siFaults) {
            $t = Join-Path $siRoot ("fault-" + $c.Name)
            Copy-Item -LiteralPath $siCut -Destination $t -Recurse
            & $c.Do $t
            foreach ($v in @("", "2.3.1.0", "2.4.0.0")) {
                $refused = $null
                try { Assert-SilentInstallLayout -Root $t -Map $silentPublishable -What "fixture $($c.Name)" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
                Assert-True ($null -ne $refused -and $refused -match $c.Says) ("Assert-SilentInstallLayout -PublishedVersion '{0}' on the '{1}' fixture: expected a refusal matching '{2}', got: {3}" -f $v, $c.Name, $c.Says, $refused)
            }
        }
        # No tools\: accepted only before 2.4.0.0, and only with none of the four.
        $siLegacy = Join-Path $siRoot "legacy"
        $siLegacyCopy = Join-Path $siRoot "legacy-copy"
        foreach ($tree in @($siLegacy, $siLegacyCopy)) {
            foreach ($d in @("release-x86", "portsw")) { New-Item -ItemType Directory -Force -Path (Join-Path $tree $d) | Out-Null }
        }
        Copy-Item -LiteralPath (Join-Path $repo "src\silent-on.bat") -Destination (Join-Path $siLegacyCopy "release-x86\silent-on.bat")
        foreach ($c in @(
            @{ Tree = $siLegacy;     Version = "";        Want = $false },
            @{ Tree = $siLegacy;     Version = "2.4.0.0"; Want = $false },
            @{ Tree = $siLegacy;     Version = "2.3.1.0"; Want = $true },
            @{ Tree = $siLegacy;     Version = "2.2.0.0"; Want = $true },
            @{ Tree = $siLegacyCopy; Version = "2.3.1.0"; Want = $false }
        )) {
            $refused = $null
            try { Assert-SilentInstallLayout -Root $c.Tree -Map $silentPublishable -What "fixture" -PublishedVersion $c.Version } catch { $refused = $_.Exception.Message }
            Assert-True (($null -eq $refused) -eq $c.Want) ("Assert-SilentInstallLayout -PublishedVersion '{0}' on '{1}': accepted={2}, expected {3}. {4}" -f $c.Version, (Split-Path -Leaf $c.Tree), ($null -eq $refused), $c.Want, $refused)
        }
    }
    #
    # **The report batches' layout, EXECUTED** (roadmap-hcd task 38.1): the
    # map and the two functions loaded from make-release.ps1's text, as the
    # tools\ layout's are above, and driven with fixture trees. Publish
    # copies a tool's batches under their upper-case 8.3 names and refuses a
    # source missing one; the layout check takes each batch beside its own
    # tool alone, and refuses a copy anywhere else, a missing one, and any
    # in a tree published before 2.4.0.0.
    Write-Step "the report batches are published beside their tools alone"
    foreach ($anchor in @(
        @{ Find = 'Publish-ReportBatches -SourceDir $QualtoolDir -DestDir $qualDir -Map $reportBatches["xhciqual"]';
           Why  = "a cut no longer publishes QUALREP.BAT beside XHCIQUAL.EXE" },
        @{ Find = 'Publish-ReportBatches -SourceDir $SnapToolDir -DestDir $snapDir -Map $reportBatches["xhcisnap"]';
           Why  = "a cut no longer publishes SNAPON.BAT, SNAPGET.BAT and SNAPOFF.BAT beside XHCISNAP.EXE" },
        @{ Find = 'Assert-ReportBatchLayout -Root $destRoot -Batches $reportBatches -What "the staged release ''$destRoot''"';
           Why  = "a cut no longer holds its staged tree to the report batches' layout" },
        @{ Find = 'Assert-ReportBatchLayout -Root $uploadRoot -Batches $reportBatches -What "the upload set ''$uploadRoot''" -PublishedVersion $Version';
           Why  = "the upload assembly no longer holds the tree to the report batches' layout its version was published with" },
        @{ Find = 'Assert-ReportBatchContent -Root $destRoot -Batches $reportBatches -What "the staged release ''$destRoot''" -Reference $repo';
           Why  = "a cut no longer holds the batches it staged to the shared checker and the repository's own bytes" },
        @{ Find = 'Assert-ReportBatchContent -Root $uploadRoot -Batches $reportBatches -What "the upload set ''$uploadRoot''"';
           Why  = "the upload assembly no longer holds the batches it archives to the shared checker" }
    )) {
        Assert-True ($releaserText.Contains($anchor.Find)) `
            ("make-release.ps1 no longer contains '" + $anchor.Find + "': " + $anchor.Why + " (roadmap-hcd task 38.1).")
    }
    $rbAt = $releaserText.IndexOf('Assert-ReportBatchLayout -Root $destRoot')
    $rbSwapAt = $releaserText.IndexOf('Move-Item -LiteralPath $destRoot -Destination $finalRoot')
    $rbAsideAt = $releaserText.IndexOf('Move-Item -LiteralPath $finalRoot -Destination $asideRoot')
    Assert-True ($rbAt -ge 0 -and $rbSwapAt -gt $rbAt -and $rbAsideAt -gt $rbAt) `
        "make-release.ps1 holds the staged release to the report batches' layout after the -Force aside or the publish swap, or not at all (roadmap-hcd task 38.1)."
    $rbcAt = $releaserText.IndexOf('Assert-ReportBatchContent -Root $destRoot')
    Assert-True ($rbcAt -ge 0 -and $rbSwapAt -gt $rbcAt -and $rbAsideAt -gt $rbcAt) `
        "make-release.ps1 holds the staged report batches' content after the -Force aside or the publish swap, or not at all (roadmap-hcd task 38.1)."
    $rbcUpAt = $releaserText.IndexOf('Assert-ReportBatchContent -Root $uploadRoot')
    $rbcZipAt = $releaserText.IndexOf('[System.IO.Compression.ZipFile]', [Math]::Max($rbcUpAt, 0))
    Assert-True ($rbcUpAt -ge 0 -and $rbcZipAt -gt $rbcUpAt) `
        "make-release.ps1 checks the upload set's report batches after archiving them, or not at all (roadmap-hcd task 38.1)."
    $rbTexts = @()
    $rbMap = [regex]::Match($releaserText, '(?ms)^\$reportBatches = @\{.*?^\}')
    Assert-True $rbMap.Success "make-release.ps1 no longer sets `$reportBatches at its top level (roadmap-hcd task 38.1)."
    if ($rbMap.Success) { $rbTexts += $rbMap.Value }
    foreach ($fnName in @("Publish-ReportBatches", "Assert-ReportBatchLayout", "Assert-ReportBatchContent")) {
        $fnAt = $releaserText.IndexOf("function $fnName")
        Assert-True ($fnAt -ge 0) "make-release.ps1 no longer has $fnName (roadmap-hcd task 38.1)."
        if ($fnAt -ge 0) { $rbTexts += $releaserText.Substring($fnAt, $releaserText.IndexOf("`n}", $fnAt) - $fnAt + 2) }
    }
    if ($rbTexts.Count -eq 4) {
        foreach ($rbText in $rbTexts) { Invoke-Expression $rbText }
        $wantMap = @{ "xhcisnap" = @("SNAPGET.BAT", "SNAPOFF.BAT", "SNAPON.BAT"); "xhciqual" = @("QUALREP.BAT") }
        Assert-True ((@($reportBatches.Keys | Sort-Object) -join ",") -eq "xhciqual,xhcisnap") "make-release.ps1's `$reportBatches no longer names exactly the qualifier and XHCISNAP."
        foreach ($tool in $wantMap.Keys) {
            if (-not $reportBatches.ContainsKey($tool)) { continue }
            Assert-True ((@($reportBatches[$tool].Values | Sort-Object) -join ",") -ceq ($wantMap[$tool] -join ",")) `
                "make-release.ps1 no longer publishes exactly $($wantMap[$tool] -join ', ') beside the $tool tool, in upper case."
        }
        foreach ($spec in $reportBatchSpecs) {
            Assert-True ($reportBatches[$spec.Dir].ContainsKey($spec.Name)) "make-release.ps1 does not publish $($spec.Dir)\$($spec.Name), the file the batch gate reads."
        }

        $rbRoot = Join-Path $script:work "reportbatches"
        $rbSrc = Join-Path $rbRoot "src"
        New-Item -ItemType Directory -Force -Path $rbSrc | Out-Null
        foreach ($spec in $reportBatchSpecs) {
            [System.IO.File]::WriteAllText((Join-Path $rbSrc $spec.Name), (New-ReportBatchFixture -Spec $spec), (New-Object System.Text.ASCIIEncoding))
        }
        $rbCut = Join-Path $rbRoot "cut"
        foreach ($d in @("release-x86", "debug-x86", "release-x64", "debug-x64")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $rbCut $d) | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $rbCut "$d\xhci98.inf"), "; stand-in")
        }
        foreach ($name in @("xhciqual", "xhcisnap", "portsw")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $rbCut "tools\$name") | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $rbCut "tools\$name\readme.txt"), "stand-in")
        }
        foreach ($tool in @("xhciqual", "xhcisnap")) {
            Publish-ReportBatches -SourceDir $rbSrc -DestDir (Join-Path $rbCut "tools\$tool") -Map $reportBatches[$tool]
        }
        foreach ($tool in $wantMap.Keys) {
            $got = @(Get-ChildItem -LiteralPath (Join-Path $rbCut "tools\$tool") -Filter "*.BAT" | ForEach-Object { $_.Name } | Sort-Object)
            Assert-True (($got -join ",") -ceq ($wantMap[$tool] -join ",")) "Publish-ReportBatches put '$($got -join ', ')' in tools\$tool, not '$($wantMap[$tool] -join ', ')'."
        }
        foreach ($spec in $reportBatchSpecs) {
            $pub = Join-Path $rbCut ("tools\" + $spec.Dir + "\" + $reportBatches[$spec.Dir][$spec.Name])
            Assert-True ((Test-Path -LiteralPath $pub) -and
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes($pub))) -eq
                         (Get-StandInSha ([System.IO.File]::ReadAllBytes((Join-Path $rbSrc $spec.Name))))) `
                "Publish-ReportBatches did not publish $($spec.Name) byte for byte."
        }
        foreach ($v in @("", "2.3.1.0", "2.4.0.0", "2.4.0.1")) {
            $refused = $null
            try { Assert-ReportBatchLayout -Root $rbCut -Batches $reportBatches -What "fixture cut" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-ReportBatchLayout -PublishedVersion '$v' refused the batches beside their tools alone: $refused"
        }

        # A source missing one: refused, naming it, and nothing written.
        $rbLone = Join-Path $rbRoot "lone-src"
        New-Item -ItemType Directory -Force -Path $rbLone | Out-Null
        Copy-Item -LiteralPath (Join-Path $rbSrc "snapon.bat") -Destination $rbLone
        Copy-Item -LiteralPath (Join-Path $rbSrc "snapoff.bat") -Destination $rbLone
        $rbLoneDest = Join-Path $rbRoot "lone-dest"
        $refused = $null
        try { Publish-ReportBatches -SourceDir $rbLone -DestDir $rbLoneDest -Map $reportBatches["xhcisnap"] } catch { $refused = $_.Exception.Message }
        Assert-True ($null -ne $refused -and $refused -match "snapget\.bat") "Publish-ReportBatches did not refuse a source with no snapget.bat, naming it: $refused"
        Assert-True (-not (Test-Path -LiteralPath $rbLoneDest)) "Publish-ReportBatches wrote its directory before refusing a source missing a batch."

        # A skipped tool: its directory absent, none of its batches anywhere.
        $rbSkip = Join-Path $rbRoot "skip-qual"
        Copy-Item -LiteralPath $rbCut -Destination $rbSkip -Recurse
        Remove-Item -LiteralPath (Join-Path $rbSkip "tools\xhciqual") -Recurse
        $refused = $null
        try { Assert-ReportBatchLayout -Root $rbSkip -Batches $reportBatches -What "fixture" } catch { $refused = $_.Exception.Message }
        Assert-True ($null -eq $refused) "Assert-ReportBatchLayout refused a cut with no tools\xhciqual (-SkipQualtool): $refused"

        $rbFaults = @(
            @{ Name = "stray-portsw";  Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\xhcisnap\SNAPGET.BAT") -Destination (Join-Path $t "tools\portsw\SNAPGET.BAT") }; Says = "portsw\\SNAPGET\.BAT" },
            @{ Name = "swapped";       Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\xhciqual\QUALREP.BAT") -Destination (Join-Path $t "tools\xhcisnap\QUALREP.BAT") }; Says = "xhcisnap\\QUALREP\.BAT" },
            @{ Name = "flavour-copy";  Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\xhcisnap\SNAPON.BAT") -Destination (Join-Path $t "release-x86\snapon.bat") }; Says = "release-x86\\snapon\.bat" },
            @{ Name = "root-copy";     Do = { param($t) Copy-Item -LiteralPath (Join-Path $t "tools\xhcisnap\SNAPOFF.BAT") -Destination (Join-Path $t "SNAPOFF.BAT") }; Says = "SNAPOFF\.BAT is a copy" },
            @{ Name = "deep-copy";     Do = { param($t) New-Item -ItemType Directory -Path (Join-Path $t "tools\xhcisnap\old") | Out-Null; Copy-Item -LiteralPath (Join-Path $t "tools\xhcisnap\SNAPGET.BAT") -Destination (Join-Path $t "tools\xhcisnap\old\SNAPGET.BAT") }; Says = "old\\SNAPGET\.BAT" },
            @{ Name = "missing-snap";  Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\xhcisnap\SNAPOFF.BAT") }; Says = "SNAPOFF\.BAT is missing" },
            @{ Name = "missing-qual";  Do = { param($t) Remove-Item -LiteralPath (Join-Path $t "tools\xhciqual\QUALREP.BAT") }; Says = "QUALREP\.BAT is missing" }
        )
        foreach ($c in $rbFaults) {
            $t = Join-Path $rbRoot ("fault-" + $c.Name)
            Copy-Item -LiteralPath $rbCut -Destination $t -Recurse
            & $c.Do $t
            foreach ($v in @("", "2.3.1.0", "2.4.0.0")) {
                $refused = $null
                try { Assert-ReportBatchLayout -Root $t -Batches $reportBatches -What "fixture $($c.Name)" -PublishedVersion $v } catch { $refused = $_.Exception.Message }
                Assert-True ($null -ne $refused -and $refused -match $c.Says) ("Assert-ReportBatchLayout -PublishedVersion '{0}' on the '{1}' fixture: expected a refusal matching '{2}', got: {3}" -f $v, $c.Name, $c.Says, $refused)
            }
        }

        # A tree with no tools\: accepted with none of them before 2.4.0.0,
        # refused with one anywhere, and refused for the cut and from 2.4.0.0.
        $rbLegacy = Join-Path $rbRoot "legacy"
        foreach ($d in @("release-x86", "debug-x86", "xhciqual", "xhcisnap", "portsw")) {
            New-Item -ItemType Directory -Force -Path (Join-Path $rbLegacy $d) | Out-Null
        }
        [System.IO.File]::WriteAllText((Join-Path $rbLegacy "xhcisnap\XHCISNAP.EXE"), "stand-in")
        $rbLegacyStray = Join-Path $rbRoot "legacy-stray"
        Copy-Item -LiteralPath $rbLegacy -Destination $rbLegacyStray -Recurse
        Copy-Item -LiteralPath (Join-Path $rbSrc "snapget.bat") -Destination (Join-Path $rbLegacyStray "xhcisnap\SNAPGET.BAT")
        foreach ($c in @(
            @{ Tree = $rbLegacy;      Version = "2.3.1.0"; Want = $true },
            @{ Tree = $rbLegacy;      Version = "2.2.0.0"; Want = $true },
            @{ Tree = $rbLegacy;      Version = "2.4.0.0"; Want = $false },
            @{ Tree = $rbLegacy;      Version = "";        Want = $false },
            @{ Tree = $rbLegacyStray; Version = "2.3.1.0"; Want = $false }
        )) {
            $refused = $null
            try { Assert-ReportBatchLayout -Root $c.Tree -Batches $reportBatches -What "fixture" -PublishedVersion $c.Version } catch { $refused = $_.Exception.Message }
            Assert-True (($null -eq $refused) -eq $c.Want) ("Assert-ReportBatchLayout -PublishedVersion '{0}' on '{1}': accepted={2}, expected {3}. {4}" -f $c.Version, (Split-Path -Leaf $c.Tree), ($null -eq $refused), $c.Want, $refused)
        }

        # **What the staged batches say, EXECUTED** (Codex review of 38.1,
        # round 1): the layout checks above pass a batch by name and place
        # alone, so Assert-ReportBatchContent holds each one in the tree to
        # the shared checker - for the cut, whose -SnapToolDir and
        # -QualtoolDir may hold anything, and for the upload assembly,
        # including -UploadSetOnly's tree - and, with -Reference (the cut),
        # to the repository's own bytes. Here the reference is a stand-in
        # repository holding the fixtures, as the tree holds them.
        Write-Step "a staged or published report batch is held to the shared checker"
        $rbRef = Join-Path $rbRoot "ref"
        foreach ($spec in $reportBatchSpecs) {
            New-Item -ItemType Directory -Force -Path (Join-Path $rbRef $spec.Dir) | Out-Null
            Copy-Item -LiteralPath (Join-Path $rbSrc $spec.Name) -Destination (Join-Path $rbRef ($spec.Dir + "\" + $spec.Name))
        }
        foreach ($ref in @("", $rbRef)) {
            $refused = $null
            try { Assert-ReportBatchContent -Root $rbCut -Batches $reportBatches -What "fixture cut" -Reference $ref } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-ReportBatchContent -Reference '$ref' refused the good fixture batches: $refused"
        }
        foreach ($tree in @($rbSkip, $rbLegacy)) {
            $refused = $null
            try { Assert-ReportBatchContent -Root $tree -Batches $reportBatches -What "fixture" -Reference $rbRef } catch { $refused = $_.Exception.Message }
            Assert-True ($null -eq $refused) "Assert-ReportBatchContent refused '$(Split-Path -Leaf $tree)', which has no batch of a skipped tool or no tools\ at all: $refused"
        }
        # Each malformed one refused in both modes, naming its file; the
        # stale one (passes the checker, but not the repository's bytes)
        # refused by the cut alone, and in a tool's directory as a
        # -SnapToolDir or -QualtoolDir would put it there.
        $rbContent = @(
            @{ Name = "lf-snapget";   File = "tools\xhcisnap\SNAPGET.BAT"; Edit = { param($s) $s.Replace("`r`n", "`n") };                         Stale = $false },
            @{ Name = "bom-snapon";   File = "tools\xhcisnap\SNAPON.BAT";  Edit = { param($s) ([string][char]0xEF + [char]0xBB + [char]0xBF) + $s }; Stale = $false },
            @{ Name = "disable-get";  File = "tools\xhcisnap\SNAPGET.BAT"; Edit = { param($s) $s.Replace("XHCISNAP -report %1`r`n", "XHCISNAP -report %1`r`nXHCISNAP -disable`r`n") }; Stale = $false },
            @{ Name = "nopause-off";  File = "tools\xhcisnap\SNAPOFF.BAT"; Edit = { param($s) $s.Replace(":end`r`npause`r`n", ":end`r`n") };        Stale = $false },
            @{ Name = "active-qual";  File = "tools\xhciqual\QUALREP.BAT"; Edit = { param($s) $s.Replace("XHCIQUAL --probe-only --no-page --log-stamp", "XHCIQUAL --no-page --log-stamp") }; Stale = $false },
            @{ Name = "cdrive-qual";  File = "tools\xhciqual\QUALREP.BAT"; Edit = { param($s) $s.Replace("XHCIQUAL --probe-only --no-page --log-stamp`r`n", "XHCIQUAL --probe-only --no-page --log C:\PROBE.LOG`r`n") }; Stale = $false },
            @{ Name = "stale-snapget"; File = "tools\xhcisnap\SNAPGET.BAT"; Edit = { param($s) $s.Replace("goto end`r`n:tool`r`n", "echo An older wording.`r`ngoto end`r`n:tool`r`n") }; Stale = $true },
            @{ Name = "stale-qual";   File = "tools\xhciqual\QUALREP.BAT"; Edit = { param($s) $s.Replace(":end`r`n", ":end`r`necho An older wording.`r`n") }; Stale = $true }
        )
        foreach ($c in $rbContent) {
            $t = Join-Path $rbRoot ("content-" + $c.Name)
            Copy-Item -LiteralPath $rbCut -Destination $t -Recurse
            $p = Join-Path $t $c.File
            $before = [System.IO.File]::ReadAllText($p)
            $after = [string](& $c.Edit $before)
            Assert-True ($after -cne $before) "the report-batch content control '$($c.Name)' did not change its fixture, so it controls nothing."
            [System.IO.File]::WriteAllBytes($p, [System.Text.Encoding]::GetEncoding(28591).GetBytes($after))
            $says = [regex]::Escape((Split-Path -Leaf $c.File))
            foreach ($ref in @("", $rbRef)) {
                $refused = $null
                try { Assert-ReportBatchContent -Root $t -Batches $reportBatches -What "fixture $($c.Name)" -Reference $ref } catch { $refused = $_.Exception.Message }
                if ($c.Stale -and $ref -eq "") {
                    Assert-True ($null -eq $refused) "Assert-ReportBatchContent (upload assembly) refused the '$($c.Name)' fixture, which the checker passes: $refused"
                } else {
                    Assert-True ($null -ne $refused -and $refused -match $says) ("Assert-ReportBatchContent -Reference '{0}' on the '{1}' fixture: expected a refusal naming {2}, got: {3}" -f $ref, $c.Name, (Split-Path -Leaf $c.File), $refused)
                }
            }
            if ($c.Stale) {
                $refused = $null
                try { Assert-ReportBatchContent -Root $t -Batches $reportBatches -What "fixture" -Reference $rbRef } catch { $refused = $_.Exception.Message }
                Assert-True ($null -ne $refused -and $refused -match "byte for byte") "Assert-ReportBatchContent did not call the '$($c.Name)' fixture a stale copy: $refused"
            }
        }
    }

    #
    # -Arch defaults to BOTH architectures, and that default is asserted rather
    # than left to reading - in the same place and for the same reason it was
    # asserted as x86-alone before. Roadmap task 21.5 passed on 2026-09-09: an
    # amd64 binary installed and ran on a Windows XP x64 guest through every
    # checkpoint clause, and the release flavour this publisher stages was then
    # installed and read separately rather than inferred from the qemu one.
    # Changing this line back is as deliberate as changing it forwards was.
    #
    Assert-True ($releaserText -match [regex]::Escape('[string[]]$Arch = @("x86", "x64"),')) `
        "make-release.ps1's -Arch no longer defaults to both architectures: an ordinary cut would silently stop publishing the 64-bit package that roadmap task 21.5 qualified."

    #
    # **And -Arch has to reach every one of the publisher's INF gate runs too.**
    # make-package.ps1 has the same assertion above, and make-release.ps1 had
    # none until 2026-09-16 - which is how all three of its calls came to run
    # under the gate's default x86 profile. The consequence is not a weakened
    # check but a refused cut: the amd64 INF fails seven checks under the x86
    # profile (PATH-MFGDEC three times, BOTH-XREF twice, OS-DEFAULT twice), so the
    # two-architecture default threw at the pre-build layout step, and fixing
    # only that site would have moved the refusal to the upload assembly, which
    # runs *after* the publish swap - the published-with-no-asset state this
    # script's own comments exist to prevent.
    #
    # Structural rather than a run for the reason the block above is: a real cut
    # needs two builds and the git-ignored tools\ staging. The -UploadSetOnly
    # case further down drives the third site for real.
    #
    Assert-True ($releaserText -match [regex]::Escape('"-InfPath", $InfPath, "-Arch", $Arch,')) `
        "make-release.ps1's Get-DeclaredMediaLayout no longer passes -Arch to the INF gate, so the amd64 INF would be parsed under the gate's x86 profile and refuse the cut."
    Assert-True ($releaserText -match [regex]::Escape('Get-DeclaredMediaLayout -InfPath $archInf -Arch $archPkgName[$a]')) `
        "make-release.ps1's pre-build layout loop no longer tells Get-DeclaredMediaLayout which architecture each INF is, so both legs would be gated as x86."
    Assert-True ($releaserText -match [regex]::Escape('-Arch $LegArches[$legDir]')) `
        "make-release.ps1's upload assembly no longer derives the media layout under the published directory's own architecture."
    Assert-True ($releaserText -match [regex]::Escape('"-InfPath", $flavorInf, "-Arch", $LegArches[$f],')) `
        "make-release.ps1 no longer passes -Arch to the per-directory INF gate in the upload assembly, so an assembled release-x64\ would be gated as x86 - after the publish swap."
    # Both callers have to carry the architecture in, or the mandatory parameter
    # above is satisfied by whatever the hashtable lookup returns.
    Assert-True (([regex]::Matches($releaserText, [regex]::Escape('-LegArches $legArches')).Count) -eq 2) `
        "make-release.ps1 no longer hands New-UploadSet a per-directory architecture map from both of its call sites."

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

    # --- selective suspend's test client is refused in a release (36.34) ----
    #
    # Design record 14 section 11: the test client under test\pmclient is
    # never shipped, and the packaging gate refuses it. The matcher is
    # EXECUTED, loaded from make-release.ps1's text and driven with fixture
    # directories: clean media passes; the client's binary, its INF or its
    # tool by name, and a copy renamed but still carrying the marker, are
    # each refused (the negative tests). Then both call sites are held to
    # their places before the publish swap.
    Write-Step "selective suspend's test client is refused in a release"
    $tcAt = $releaserText.IndexOf('function Find-TestClientFiles')
    $tcAssertAt = $releaserText.IndexOf('function Assert-NoTestClient')
    Assert-True ($tcAt -ge 0 -and $tcAssertAt -ge 0) "make-release.ps1 no longer has Find-TestClientFiles / Assert-NoTestClient, so a release can carry the test client."
    if ($tcAt -ge 0 -and $tcAssertAt -ge 0) {
        $fnEnd = $releaserText.IndexOf("`n}", $tcAt)
        Invoke-Expression $releaserText.Substring($tcAt, $fnEnd - $tcAt + 2)
        $fnEnd = $releaserText.IndexOf("`n}", $tcAssertAt)
        Invoke-Expression $releaserText.Substring($tcAssertAt, $fnEnd - $tcAssertAt + 2)
        $tcRoot = Join-Path $script:work "testclient"
        $tcCases = @(
            @{ Name = "clean";    File = $null;                          Bytes = $null;                                         Want = 0 },
            @{ Name = "sys";      File = "release\XHCIPM.SYS";            Bytes = "MZ";                                          Want = 1 },
            @{ Name = "inf";      File = "xhcipm.inf";                    Bytes = "; inf";                                       Want = 1 },
            @{ Name = "tool";     File = "xhcisnap\PmCtl.exe";            Bytes = "MZ";                                          Want = 1 },
            @{ Name = "renamed";  File = "release\usbhelp.sys";           Bytes = "MZ..XHCI98_TEST_CLIENT_DO_NOT_SHIP..";         Want = 1 },
            @{ Name = "nearmiss"; File = "release\notes.txt";             Bytes = "XHCI98_TEST_CLIENT_DO_NOT_SHI";                Want = 0 }
        )
        foreach ($c in $tcCases) {
            $dir = Join-Path $tcRoot $c.Name
            New-Item -ItemType Directory -Force -Path (Join-Path $dir "release") | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $dir "release\xhci98.sys"), "MZ this project's driver")
            [System.IO.File]::WriteAllText((Join-Path $dir "release\xhci98.inf"), "; this project's INF")
            if ($null -ne $c.File) {
                $path = Join-Path $dir $c.File
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
                [System.IO.File]::WriteAllText($path, $c.Bytes)
            }
            $hits = @(Find-TestClientFiles -Root $dir)
            Assert-True ($hits.Count -eq $c.Want) ("Find-TestClientFiles found {0} file(s) in the '{1}' fixture, not {2}: {3}" -f $hits.Count, $c.Name, $c.Want, ($hits -join ' | '))
            $refused = $false
            try { Assert-NoTestClient -Root $dir -What "fixture $($c.Name)" } catch { $refused = $true }
            Assert-True ($refused -eq ($c.Want -ne 0)) ("Assert-NoTestClient on the '{0}' fixture: refused={1}, expected {2}." -f $c.Name, $refused, ($c.Want -ne 0))
        }
    }
    $tcMarks = @(
        @{ Name = "the package's test-client check";    Find = 'Assert-NoTestClient -Root $pkgDir' },
        @{ Name = "the staged release's test-client check"; Find = 'Assert-NoTestClient -Root $destRoot' },
        @{ Name = "the -Force aside";                   Find = 'Move-Item -LiteralPath $finalRoot -Destination $asideRoot' },
        @{ Name = "the publish swap";                   Find = 'Move-Item -LiteralPath $destRoot -Destination $finalRoot' }
    )
    $tcAt2 = @()
    foreach ($m in $tcMarks) {
        $i = $releaserText.IndexOf($m.Find)
        Assert-True ($i -ge 0) ("make-release.ps1 no longer contains " + $m.Name + " as this test recognises it.")
        $tcAt2 += $i
    }
    if (@($tcAt2 | Where-Object { $_ -lt 0 }).Count -eq 0) {
        for ($i = 1; $i -lt $tcMarks.Count; $i++) {
            Assert-True ($tcAt2[$i - 1] -lt $tcAt2[$i]) `
                ("$($tcMarks[$i - 1].Name) must come before $($tcMarks[$i].Name) in make-release.ps1.")
        }
        # Not inside the -SkipUploadSet branch: the staged-release check
        # runs on every cut (round 1, finding 6). Its line is not indented
        # under an `if (-not $SkipUploadSet)`.
        $line = ($releaserText.Substring(0, $tcAt2[1]) -split "`n")[-1]
        Assert-True ($line.TrimEnd("`r") -eq '    ') "the staged release's test-client check is not at the publish step's own level."
    }
    # A renamed tool carrying the marker in a tools directory is refused.
    if ($tcAt -ge 0 -and $tcAssertAt -ge 0) {
        $dir = Join-Path $tcRoot "renamedtool"
        New-Item -ItemType Directory -Force -Path (Join-Path $dir "xhcisnap") | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $dir "xhcisnap\XHCISNAP.EXE"), "MZ XHCI98_TEST_CLIENT_DO_NOT_SHIP")
        $refused = $false
        try { Assert-NoTestClient -Root $dir -What "fixture renamedtool" } catch { $refused = $true }
        Assert-True $refused "a test tool renamed XHCISNAP.EXE in a staged release's tools directory is not refused."
    }
    Assert-True ($releaserText.Contains('Assert-NoTestClient -Root $uploadRoot')) `
        "make-release.ps1's upload assembly no longer refuses the test client."


    # --- the x64 test signature (design record 19) --------------------------
    #
    # No key is needed or used here. The judgements are pure functions in
    # package-common.ps1, driven with synthetic PE images and mock parsed
    # signatures; the refusals are reached through the scripts' arguments
    # before any key would be opened. The one thing these cannot reach is
    # SignTool itself, which the cut's own checks read on the signed bytes.
    Write-Step "x64 test signature: the signed-image comparison"
    function New-SyntheticPe {
        # A minimal PE32+ header in $Size bytes: MZ, e_lfanew 0x80, the PE
        # signature, optional-header magic 0x20B, 16 data directories, and a
        # body of non-zero filler so a stray difference is visible.
        param([int]$Size)
        $b = New-Object byte[] $Size
        for ($i = 0x200; $i -lt $Size; $i++) { $b[$i] = [byte](($i * 7 + 3) % 251 + 1) }
        $b[0] = 0x4D; $b[1] = 0x5A
        [BitConverter]::GetBytes([int]0x80).CopyTo($b, 0x3C)
        [BitConverter]::GetBytes([uint32]0x00004550).CopyTo($b, 0x80)
        $opt = 0x80 + 24
        [BitConverter]::GetBytes([uint16]0x20B).CopyTo($b, $opt)
        [BitConverter]::GetBytes([uint32]16).CopyTo($b, $opt + 108)
        [BitConverter]::GetBytes([uint32]0x1234).CopyTo($b, $opt + 64)
        return ,$b
    }
    function New-SyntheticSigned {
        # $Unsigned plus a table of $TableLen bytes at the next 8-byte
        # boundary, the checksum changed and the directory entry pointing at it.
        param([byte[]]$Unsigned, [int]$TableLen, [byte]$Pad = 0)
        $at = [int]([math]::Ceiling($Unsigned.Length / 8.0) * 8)
        $s = New-Object byte[] ($at + $TableLen)
        [Array]::Copy($Unsigned, $s, $Unsigned.Length)
        for ($i = $Unsigned.Length; $i -lt $at; $i++) { $s[$i] = $Pad }
        for ($i = $at; $i -lt $s.Length; $i++) { $s[$i] = 0xC3 }
        $opt = 0x80 + 24
        [BitConverter]::GetBytes([uint32]0x9999).CopyTo($s, $opt + 64)
        [BitConverter]::GetBytes([uint32]$at).CopyTo($s, $opt + 112 + 32)
        [BitConverter]::GetBytes([uint32]$TableLen).CopyTo($s, $opt + 112 + 36)
        return ,$s
    }
    $peU = New-SyntheticPe -Size 1021
    $peS = New-SyntheticSigned -Unsigned $peU -TableLen 64
    Assert-True (Test-SignedImageOf -Signed $peS -Unsigned $peU) `
        "Test-SignedImageOf refused an image that is the build plus a signature and nothing else."
    $peBody = [byte[]]$peS.Clone(); $peBody[0x300] = [byte]($peBody[0x300] -bxor 1)
    Assert-True (-not (Test-SignedImageOf -Signed $peBody -Unsigned $peU)) `
        "Test-SignedImageOf accepted a signed image whose body differs from the build."
    $pePad = New-SyntheticSigned -Unsigned $peU -TableLen 64 -Pad 1
    Assert-True (-not (Test-SignedImageOf -Signed $pePad -Unsigned $peU)) `
        "Test-SignedImageOf accepted non-zero bytes in the alignment padding before the signature."
    $peTail = New-Object byte[] ($peS.Length + 8); [Array]::Copy($peS, $peTail, $peS.Length)
    Assert-True (-not (Test-SignedImageOf -Signed $peTail -Unsigned $peU)) `
        "Test-SignedImageOf accepted bytes after the certificate table."
    Assert-True (-not (Test-SignedImageOf -Signed $peS -Unsigned $peS)) `
        "Test-SignedImageOf accepted an already-signed image as the build."
    Assert-True (-not (Test-SignedImageOf -Signed $peU -Unsigned $peU)) `
        "Test-SignedImageOf accepted an unsigned image as the signed one."
    Assert-True (-not (Test-SignedImageOf -Signed ([System.Text.Encoding]::ASCII.GetBytes("MZ not a driver")) -Unsigned $peU)) `
        "Test-SignedImageOf accepted a stand-in that is not a PE image."
    $peFile = Join-Path $script:work "synthetic-unsigned.sys"
    [System.IO.File]::WriteAllBytes($peFile, $peU)
    Assert-True ($null -eq (Get-EmbeddedSignature -Path $peFile)) `
        "Get-EmbeddedSignature read a signature out of an image with an empty certificate-table entry."
    $threw = $false
    try { [void](Get-EmbeddedSignature -Path $driver) } catch { $threw = $true }
    Assert-True $threw "Get-EmbeddedSignature did not refuse a text stand-in as a PE image, so it could pass one as unsigned."

    Write-Step "x64 test signature: the verdict on a parsed signature (mocks)"
    $tsThumb = "00112233445566778899AABBCCDDEEFF00112233"
    $tsGood = [pscustomobject]@{ Thumbprint = $tsThumb; Subject = "CN=x"; DigestOid = "1.3.14.3.2.26";
                                 ContentType = "1.3.6.1.4.1.311.2.1.4"; DataOid = "1.3.6.1.4.1.311.2.1.15";
                                 IndirectDigestOid = "1.3.14.3.2.26";
                                 VouchedDigest = "AB"; ImageDigest = "AB"; SignatureValid = $true; Timestamped = $false }
    Assert-True ((Test-ProjectTestSignature -Signature $tsGood -Thumbprint $tsThumb) -eq "") `
        "Test-ProjectTestSignature refused the signature record 19 prescribes."
    $tsCases = @(
        @{ Name = "unsigned";    Set = $null;                                 Says = "no embedded signature" },
        @{ Name = "other cert";  Set = @{ Thumbprint = "FF" + $tsThumb.Substring(2) }; Says = "not by the project test certificate" },
        @{ Name = "SHA-256";     Set = @{ DigestOid = "2.16.840.1.101.3.4.2.1" }; Says = "not SHA-1" },
        @{ Name = "SHA-256 file"; Set = @{ IndirectDigestOid = "2.16.840.1.101.3.4.2.1" }; Says = "not SHA-1" },
        @{ Name = "plain CMS";   Set = @{ ContentType = "1.2.840.113549.1.7.1" }; Says = "not an Authenticode PE-image signature" },
        @{ Name = "not a PE";    Set = @{ DataOid = "1.3.6.1.4.1.311.2.1.25" }; Says = "not an Authenticode PE-image signature" },
        @{ Name = "bad PKCS #7"; Set = @{ SignatureValid = $false };          Says = "does not verify" },
        @{ Name = "stale digest"; Set = @{ ImageDigest = "CD" };              Says = "is not the image's" },
        @{ Name = "no digest";   Set = @{ VouchedDigest = ""; ImageDigest = "" }; Says = "is not the image's" },
        @{ Name = "timestamped"; Set = @{ Timestamped = $true };              Says = "countersignature" }
    )
    foreach ($c in $tsCases) {
        $mock = $null
        if ($null -ne $c.Set) {
            $mock = $tsGood.PSObject.Copy()
            foreach ($k in $c.Set.Keys) { $mock.$k = $c.Set[$k] }
        }
        $why = Test-ProjectTestSignature -Signature $mock -Thumbprint $tsThumb
        Assert-True ($why -match [regex]::Escape($c.Says)) `
            ("Test-ProjectTestSignature on the '{0}' mock said '{1}', expected '{2}'." -f $c.Name, $why, $c.Says)
    }

    Write-Step "x64 test signature: the SpcIndirectDataContent parser (encoded bytes)"
    # A real encoding of the structure SignTool writes: data { SPC_PE_IMAGE_DATA },
    # messageDigest { { sha1, NULL }, OCTET STRING of 20 bytes }.
    $spcDigest = [byte[]](1..20)
    $spcAlg = [byte[]](0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00)
    $spcData = [byte[]](0x30, 0x0C, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F)
    $spcDigestInfo = [byte[]](@(0x30, 0x21) + $spcAlg + @(0x04, 0x14) + $spcDigest)
    $spcBody = [byte[]]($spcData + $spcDigestInfo)
    $spcGood = [byte[]](@(0x30, $spcBody.Length) + $spcBody)
    $spc = ConvertFrom-SpcIndirectData -Content $spcGood
    Assert-True ($spc.DataOid -eq "1.3.6.1.4.1.311.2.1.15" -and $spc.DigestOid -eq "1.3.14.3.2.26" -and
                 $spc.Digest -eq "0102030405060708090A0B0C0D0E0F1011121314") `
        ("ConvertFrom-SpcIndirectData misread a well-formed encoding: {0} {1} {2}" -f $spc.DataOid, $spc.DigestOid, $spc.Digest)
    $spcBad = @(
        @{ Name = "trailing byte";   Bytes = [byte[]]($spcGood + @(0x00)) },
        @{ Name = "not a SEQUENCE";  Bytes = [byte[]](@(0x31, $spcBody.Length) + $spcBody) },
        @{ Name = "digest only";     Bytes = [byte[]](@(0x30, 0x16, 0x04, 0x14) + $spcDigest) },
        @{ Name = "digest not last"; Bytes = [byte[]](@(0x30, ($spcBody.Length + 2)) + $spcBody + @(0x05, 0x00)) },
        @{ Name = "truncated";       Bytes = [byte[]]($spcGood[0..($spcGood.Length - 2)]) }
    )
    # Review round 2: what the parser does not interpret is still validated.
    # Each variant below is the good encoding with one element changed.
    function New-SpcVariant {
        param([byte[]]$Data, [byte[]]$Alg)
        $di = [byte[]](@(0x30, ($Alg.Length + 22)) + $Alg + @(0x04, 0x14) + $spcDigest)
        $body = [byte[]]($Data + $di)
        return ,[byte[]](@(0x30, $body.Length) + $body)
    }
    $spcOkValue = New-SpcVariant -Data ([byte[]](0x30, 0x0E, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                 0x30, 0x00)) -Alg $spcAlg
    $spcOk2 = ConvertFrom-SpcIndirectData -Content $spcOkValue
    Assert-True ($spcOk2.DataOid -eq "1.3.6.1.4.1.311.2.1.15") `
        "ConvertFrom-SpcIndirectData refused an empty SpcPeImageData, which its schema allows (both fields optional)."
    # SpcPeImageData as SignTool writes it: { flags BIT STRING (empty),
    # file [0] { [2] { [0] IMPLICIT BMPString } } }.
    $spcPeImage = New-SpcVariant -Data ([byte[]](0x30, 0x17, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                 0x30, 0x09, 0x03, 0x01, 0x00, 0xA0, 0x04, 0xA2, 0x02, 0x80, 0x00)) -Alg $spcAlg
    Assert-True ((ConvertFrom-SpcIndirectData -Content $spcPeImage).DataOid -eq "1.3.6.1.4.1.311.2.1.15") `
        "ConvertFrom-SpcIndirectData refused an SpcPeImageData value of the shape SignTool writes."
    $spcAlgNoParams = [byte[]](0x30, 0x07, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A)
    Assert-True ((ConvertFrom-SpcIndirectData -Content (New-SpcVariant -Data $spcData -Alg $spcAlgNoParams)).DigestOid -eq "1.3.14.3.2.26") `
        "ConvertFrom-SpcIndirectData refused a digest algorithm with its parameters absent, which DER allows."
    $spcBad += @(
        @{ Name = "params 05 FF";      Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0xFF))) },
        @{ Name = "params not NULL";   Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x04, 0x00))) },
        @{ Name = "params then more";  Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x0B, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x05, 0x00))) },
        @{ Name = "OID unterminated";  Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x0A, 0x06, 0x06, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x81, 0x05, 0x00))) },
        @{ Name = "OID trailing 0x80"; Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x0A, 0x06, 0x06, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x80, 0x05, 0x00))) },
        @{ Name = "nested NULL 05 01"; Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x11, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x03, 0x05, 0x01, 0x00)) -Alg $spcAlg) },
        @{ Name = "nested BIT STRING"; Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x11, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x03, 0x03, 0x01, 0x08)) -Alg $spcAlg) },
        @{ Name = "constructed NULL";  Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x10, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x02, 0x25, 0x00)) -Alg $spcAlg) },
        @{ Name = "BIT STRING pad";    Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x12, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x04, 0x03, 0x02, 0x07, 0xFF)) -Alg $spcAlg) },
        @{ Name = "SpcString odd";     Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x18, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x0A, 0x03, 0x01, 0x00, 0xA0, 0x05, 0xA2, 0x03, 0x80, 0x01, 0x41)) -Alg $spcAlg) },        @{ Name = "primitive value";   Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x0F, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x05, 0x01, 0x00)) -Alg $spcAlg) },
        @{ Name = "OID non-minimal";   Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x0A, 0x06, 0x06, 0x2B, 0x0E, 0x03, 0x80, 0x02, 0x1A, 0x05, 0x00))) },
        @{ Name = "data value broken"; Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x10, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x30, 0x02, 0x05, 0x05)) -Alg $spcAlg) },
        @{ Name = "data two values";   Bytes = (New-SpcVariant -Data ([byte[]](0x30, 0x12, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0F,
                                                                           0x05, 0x00, 0x05, 0x00, 0x05, 0x00)) -Alg $spcAlg) },
        @{ Name = "long form for 9";   Bytes = (New-SpcVariant -Data $spcData -Alg ([byte[]](0x30, 0x81, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00))) }
    )
    foreach ($c in $spcBad) {
        $threw = $false
        try { [void](ConvertFrom-SpcIndirectData -Content $c.Bytes) } catch { $threw = $true }
        Assert-True $threw ("ConvertFrom-SpcIndirectData accepted the '{0}' encoding, so a CMS message ending in the right digest could pass as Authenticode." -f $c.Name)
    }

    # The pinned thumbprint is the one design record 19 records, and the
    # packager signs only after the import gates have read the build, whose
    # SITES rule needs the objects beside the image (review round 1).
    $rec19 = [System.IO.File]::ReadAllText((Join-Path $repo "docs\contributing\design\19-x64-test-signing.md"))
    Assert-True ($rec19.Contains($script:TestSignThumbprint)) `
        "package-common.ps1's pinned thumbprint $($script:TestSignThumbprint) is not the one design record 19 records."
    $tsPkgText = [System.IO.File]::ReadAllText($packager)
    $tsSignAt = $tsPkgText.IndexOf('Invoke-TestSign -Source $DriverPath')
    $tsHcdAt = $tsPkgText.IndexOf('& powershell.exe @gateArgs')
    $tsGateAt = $tsPkgText.IndexOf('& powershell.exe @uasGateArgs')
    $tsHlprAt = $tsPkgText.IndexOf('& powershell.exe @hlprGateArgs')
    $tsStageAt = $tsPkgText.IndexOf('$stageDir = $OutDir + ".staging-"')
    Assert-True ($tsSignAt -gt 0 -and $tsHcdAt -gt 0 -and $tsGateAt -gt 0 -and $tsHlprAt -gt 0 -and
                 $tsSignAt -gt $tsHcdAt -and $tsSignAt -gt $tsGateAt -and $tsSignAt -gt $tsHlprAt) `
        "make-package.ps1 signs before its import gates have read the build, so their SITES rule finds no objects beside the signed copy."
    Assert-True ($tsSignAt -lt $tsStageAt -and $tsStageAt -gt 0) `
        "make-package.ps1 no longer signs before it stages, so the staged binaries are not the signed ones."

    Write-Step "x64 test signature: the packager's and the cut's refusals"
    $tsMissing = Join-Path $script:work "no-such-key.pfx"
    $r = Invoke-Packager @("-InfPath", $prodInf, "-DriverPath", $driver, "-OutDir", (Join-Path $script:work "pkg-ts-x86"),
                           "-TestSignPfx", $tsMissing)
    Assert-True ($r.ExitCode -ne 0 -and $r.Output -match "never signed") `
        ("make-package.ps1 did not refuse -TestSignPfx on the x86 package. Output:`n" + $r.Output)
    $r = Invoke-Packager @("-Arch", "amd64", "-InfPath", $amd64Inf, "-DriverPath", $driver,
                           "-OutDir", (Join-Path $script:work "pkg-ts-nokey"), "-TestSignPfx", $tsMissing)
    Assert-True ($r.ExitCode -ne 0 -and $r.Output -match "no test-signing key at") `
        ("make-package.ps1 did not refuse a -TestSignPfx naming no file. Output:`n" + $r.Output)
    $tsRel = Join-Path $script:work "rel-ts"
    New-Item -ItemType Directory -Force -Path $tsRel | Out-Null
    foreach ($bad in @(
        @{ Args = @("-ReleasesDir", $tsRel);                                            Says = "was given no test-signing key" },
        @{ Args = @("-ReleasesDir", $tsRel, "-Arch", "x86", "-TestSignPfx", $tsMissing); Says = "publishes none" },
        @{ Args = @("-ReleasesDir", $tsRel, "-TestSignPfx", $tsMissing);                Says = "no test-signing key at" },
        @{ Args = @("-UploadSetOnly", "-ReleasesDir", $tsRel, "-TestSignPfx", $tsMissing); Says = "stages nothing" }
    )) {
        $r = Invoke-Releaser $bad.Args
        Assert-True ($r.ExitCode -ne 0) "make-release.ps1 accepted $($bad.Args -join ' ')."
        Assert-True ($r.Output -match [regex]::Escape($bad.Says)) `
            ("expected '$($bad.Says)' in the test-signing refusal. Output:`n" + $r.Output)
    }
    # And where the cut applies it: the x64 legs' packager runs are given the
    # key, and both published x64 binaries are held to the signature.
    foreach ($anchor in @(
        @{ Find = '$pkgArgs += @("-TestSignPfx", $TestSignPfx)'; Why = "the x64 legs are no longer signed at staging" },
        @{ Find = 'Assert-TestSigned -Sys $sys -BuiltSys $builtSys'; Why = "the published x64 xhci98.sys is no longer held to the signature" },
        @{ Find = 'Assert-TestSigned -Sys $Sys -BuiltSys $builtSys'; Why = "the published x64 xhciuas.sys is no longer held to the signature" },
        @{ Find = '-Signed:$legSigned'; Why = "the UAS check no longer knows the leg is signed" }
    )) {
        Assert-True ($releaserText.Contains($anchor.Find)) `
            ("make-release.ps1 no longer contains '" + $anchor.Find + "': " + $anchor.Why + ".")
    }

    # --- the sign-only mode and the gated build's record (SIGN.1) ----------
    #
    # make-package.ps1 -SignOnly skips the host suite and the import gates
    # for binaries whose bytes are those a gated build-driver.cmd run
    # recorded. The record's parse, the comparison and every refusal are
    # tested here on fixtures; nothing is built and nothing is signed (every
    # packager case below names a key that does not exist, so a run that got
    # past the record would still be refused before SignTool).
    Write-Step "sign-only: the gated build's record and its refusals"
    $soDir = Join-Path $script:work "signonly"
    New-Item -ItemType Directory -Force -Path $soDir | Out-Null
    $soBins = @{}
    $soSha = @{}
    foreach ($n in @("xhci98.sys", "xhciuas.sys", "xhcicm.sys")) {
        $p = Join-Path $soDir $n
        [System.IO.File]::WriteAllBytes($p, [System.Text.Encoding]::ASCII.GetBytes("stand-in $n XHCI98_FLAVOUR_RELEASE"))
        $soBins[$n] = $p
        $soSha[$n] = Get-FileSha256 -Path $p
    }
    Assert-True ($soSha["xhci98.sys"] -eq (Get-StandInSha ([System.IO.File]::ReadAllBytes($soBins["xhci98.sys"])))) `
        "Get-FileSha256 disagrees with the test's own SHA-256."
    $soHead = "0123456789abcdef0123456789abcdef01234567"
    $soText = ConvertTo-GatedManifest -Arch "amd64" -Flavor "release" -Commit $soHead -Dirty $false `
        -TargetEvidence $true -Time "2026-10-16T00:00:00Z" -Sha256 $soSha
    Assert-True (-not ($soText -match "(?<!`r)`n")) "the gated build record is not CRLF."
    $soM = ConvertFrom-GatedManifest -Text $soText
    Assert-True ($soM.Arch -eq "amd64" -and $soM.Flavor -eq "release" -and $soM.Commit -eq $soHead -and
                 $soM.Dirty -eq "0" -and $soM.TargetEvidence -eq "1" -and $soM.Sha256.Count -eq 3 -and
                 $soM.Sha256["xhcicm.sys"] -eq $soSha["xhcicm.sys"]) `
        "the gated build record does not read back as written."
    # Malformed records are refused, not read leniently.
    foreach ($bad in @(
        @{ Why = "no format line";   Text = ($soText -replace "(?m)^format=1\r?\n", "") },
        @{ Why = "format 2";         Text = ($soText -replace "(?m)^format=1", "format=2") },
        @{ Why = "no commit line";   Text = ($soText -replace "(?m)^commit=.*\r?\n", "") },
        @{ Why = "dirty=2";          Text = ($soText -replace "(?m)^dirty=0", "dirty=2") },
        @{ Why = "a short hash";     Text = ($soText -replace "(?m)^(sha256\.xhci98\.sys=)[0-9A-F]{64}", '${1}ABCD') },
        @{ Why = "a duplicate key";  Text = ($soText + "arch=x86`r`n") },
        @{ Why = "an unknown key";   Text = ($soText + "gates=all`r`n") },
        @{ Why = "a line with no =";  Text = ($soText + "xhci98.sys`r`n") },
        @{ Why = "no sha256 line";   Text = ($soText -replace "(?m)^sha256\..*\r?\n", "") }
    )) {
        $threw = $false
        try { [void](ConvertFrom-GatedManifest -Text $bad.Text) } catch { $threw = $true }
        Assert-True $threw ("ConvertFrom-GatedManifest accepted a record with {0}." -f $bad.Why)
    }
    # The comparison, on every reason it refuses and the one it admits.
    function Get-SoFault {
        param($M = $soM, [string]$Arch = "amd64", [string]$Flavor = "release", [hashtable]$Bins = $soBins,
              [string]$Head = $soHead, [bool]$Dirty = $false, [bool]$NoTe = $false)
        return Get-GatedManifestFault -Manifest $M -Arch $Arch -Flavor $Flavor -Binaries $Bins `
            -Head $Head -Dirty $Dirty -NoTargetEvidence $NoTe
    }
    Assert-True ((Get-SoFault) -eq "") ("a matching record was refused: " + (Get-SoFault))
    $soAltered = Join-Path $soDir "xhcicm-altered.sys"
    [System.IO.File]::WriteAllBytes($soAltered, [System.Text.Encoding]::ASCII.GetBytes("stand-in xhcicm.sys XHCI98_FLAVOUR_RELEASE!"))
    $soAlteredBins = $soBins.Clone(); $soAlteredBins["xhcicm.sys"] = $soAltered
    $soExtraBins = $soBins.Clone(); $soExtraBins["xhcihlpr.sys"] = $soBins["xhci98.sys"]
    $soDirtyM = ConvertFrom-GatedManifest -Text ($soText -replace "(?m)^dirty=0", "dirty=1")
    $soNoTeM = ConvertFrom-GatedManifest -Text ($soText -replace "(?m)^target_evidence=1", "target_evidence=0")
    foreach ($c in @(
        @{ Why = "the x86 package";        Says = "records the release amd64 build"; F = (Get-SoFault -Arch "x86") },
        @{ Why = "the debug flavour";      Says = "records the release amd64 build"; F = (Get-SoFault -Flavor "debug") },
        @{ Why = "an altered binary";      Says = "xhcicm.sys at";                   F = (Get-SoFault -Bins $soAlteredBins) },
        @{ Why = "an unrecorded binary";   Says = "no hash for xhcihlpr.sys";        F = (Get-SoFault -Bins $soExtraBins) },
        @{ Why = "a dirty build";          Says = "uncommitted changes";             F = (Get-SoFault -M $soDirtyM) },
        @{ Why = "another commit";         Says = "it was built at commit";          F = (Get-SoFault -Head "fedcba9876543210fedcba9876543210fedcba98") },
        @{ Why = "a dirty tree";           Says = "uncommitted changes since";       F = (Get-SoFault -Dirty $true) },
        @{ Why = "no target evidence";     Says = "-NoTargetEvidence";               F = (Get-SoFault -M $soNoTeM) }
    )) {
        Assert-True ($c.F -ne "" -and $c.F.Contains($c.Says)) `
            ("the gated build record was not refused for {0} with '{1}': '{2}'" -f $c.Why, $c.Says, $c.F)
    }
    Assert-True ((Get-SoFault -M $soNoTeM -NoTe $true) -eq "") `
        "a record without target evidence was refused for a run that passes -NoTargetEvidence too."

    # make-package.ps1 itself: each refusal, before anything is staged. The
    # stand-in driver says debug, the packager's default flavour.
    $soDbgSha = @{ "xhci98.sys" = (Get-FileSha256 -Path $driver) }
    $soState = Get-GitTreeState -Repo $repo
    function New-SoRecord {
        param([string]$Name, [string]$Arch = "amd64", [string]$Commit = $soState.Head, [hashtable]$Sha = $soDbgSha)
        $p = Join-Path $soDir $Name
        [System.IO.File]::WriteAllBytes($p, [System.Text.Encoding]::ASCII.GetBytes(
            (ConvertTo-GatedManifest -Arch $Arch -Flavor "debug" -Commit $Commit -Dirty $false `
                -TargetEvidence $true -Time "2026-10-16T00:00:00Z" -Sha256 $Sha)))
        return $p
    }
    $soWrongHash = @{ "xhci98.sys" = ("0" * 64) }
    # -GatedManifestPath is the self-tests' alone: it never goes with a key,
    # and a run naming it checks the record and stops.
    $soCases = @(
        @{ Why = "-SignOnly without -TestSignPfx (amd64)"; Says = "without -TestSignPfx";
           Args = @("-Arch", "amd64", "-SignOnly") },
        @{ Why = "-SignOnly without -TestSignPfx (x86)";   Says = "without -TestSignPfx";
           Args = @("-SignOnly") },
        @{ Why = "-SignOnly with -SkipBinaryGates";        Says = "does not combine with -SkipBinaryGates";
           Args = @("-Arch", "amd64", "-SignOnly", "-TestSignPfx", $tsMissing, "-SkipBinaryGates") },
        @{ Why = "-GatedManifestPath with -TestSignPfx";   Says = "is for the packager's self-tests";
           Args = @("-Arch", "amd64", "-SignOnly", "-TestSignPfx", $tsMissing, "-GatedManifestPath", (New-SoRecord -Name "key.txt")) },
        @{ Why = "-GatedManifestPath without -SignOnly";   Says = "is for the packager's self-tests";
           Args = @("-Arch", "amd64", "-GatedManifestPath", (New-SoRecord -Name "nosignonly.txt")) },
        @{ Why = "a missing record";                       Says = "no gated build record at";
           Args = @("-Arch", "amd64", "-SignOnly", "-GatedManifestPath", (Join-Path $soDir "none.txt")) },
        @{ Why = "an unreadable record";                   Says = "is not a gated build record";
           Args = @("-Arch", "amd64", "-SignOnly", "-GatedManifestPath", $soBins["xhci98.sys"]) },
        @{ Why = "the other architecture's record";        Says = "records the debug x86 build";
           Args = @("-Arch", "amd64", "-SignOnly", "-GatedManifestPath", (New-SoRecord -Name "x86.txt" -Arch "x86")) },
        @{ Why = "a hash mismatch";                        Says = "is not the binary the build gated";
           Args = @("-Arch", "amd64", "-SignOnly", "-GatedManifestPath", (New-SoRecord -Name "hash.txt" -Sha $soWrongHash)) },
        @{ Why = "a commit mismatch";                      Says = "it was built at commit";
           Args = @("-Arch", "amd64", "-SignOnly", "-GatedManifestPath", (New-SoRecord -Name "commit.txt" -Commit $soHead)) }
    )
    $soN = 0
    foreach ($c in $soCases) {
        $soN++
        $soOut = Join-Path $script:work ("pkg-signonly-" + $soN)
        $soArgs = @("-InfPath", $(if ($c.Args -contains "amd64") { $amd64Inf } else { $prodInf }),
                    "-DriverPath", $driver, "-OutDir", $soOut) + $c.Args
        $r = Invoke-Packager $soArgs -WithBinaryGates
        Assert-True ($r.ExitCode -ne 0 -and $r.Output.Contains($c.Says)) `
            ("make-package.ps1 did not refuse {0} with '{1}'. Output:`n{2}" -f $c.Why, $c.Says, $r.Output)
        Assert-True ($r.Output -notmatch "=== host test suite|== host test suite") `
            ("make-package.ps1 ran the host test suite before refusing {0}." -f $c.Why)
        Assert-True (-not (Test-Path -LiteralPath $soOut)) ("a package refused for {0} left an output directory." -f $c.Why)
    }
    # A record that admits the stand-in is reported as admitting it, and the
    # run stops there - in a clean tree. In a tree with uncommitted changes the
    # refusal is the tree's, which is the right answer there too.
    $soOkOut = Join-Path $script:work "pkg-signonly-ok"
    $r = Invoke-Packager @("-Arch", "amd64", "-InfPath", $amd64Inf, "-DriverPath", $driver,
                           "-OutDir", $soOkOut, "-SignOnly",
                           "-GatedManifestPath", (New-SoRecord -Name "ok.txt")) -WithBinaryGates
    if ($soState.Dirty) {
        Assert-True ($r.ExitCode -ne 0 -and $r.Output.Contains("uncommitted changes since")) `
            ("a matching record in a dirty tree was not refused for the tree. Output:`n" + $r.Output)
    } else {
        Assert-True ($r.ExitCode -eq 0 -and $r.Output.Contains("nothing is staged or signed") -and
                     $r.Output.Contains("REUSED from the gated build")) `
            ("a matching record was not reported as admitting the stand-in. Output:`n" + $r.Output)
    }
    Assert-True (-not (Test-Path -LiteralPath $soOkOut)) "a -GatedManifestPath run staged a package."

    # The tree's state: untracked files, and files git was told not to look at.
    $soGit = Join-Path $soDir "git"
    New-Item -ItemType Directory -Force -Path $soGit | Out-Null
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & git -C $soGit init -q 2>&1 | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $soGit "a.txt"), "a")
        [System.IO.File]::WriteAllText((Join-Path $soGit "b.txt"), "b")
        & git -C $soGit add a.txt b.txt 2>&1 | Out-Null
        & git -C $soGit -c user.name=t -c user.email=t@t -c commit.gpgsign=false commit -q -m t 2>&1 | Out-Null
    } finally {
        $ErrorActionPreference = $savedEap
    }
    $s = Get-GitTreeState -Repo $soGit
    Assert-True ($s.Head -match '^[0-9a-f]{40}$' -and -not $s.Dirty) "Get-GitTreeState did not read a clean fixture repository as clean."
    New-Item -ItemType Directory -Force -Path (Join-Path $soGit "sub") | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $soGit "sub\new.h"), "n")
    Assert-True ((Get-GitTreeState -Repo $soGit).Dirty) "Get-GitTreeState missed an untracked file in an untracked directory."
    Remove-Item -LiteralPath (Join-Path $soGit "sub") -Recurse -Force
    foreach ($flag in @("--assume-unchanged", "--skip-worktree")) {
        $ErrorActionPreference = "Continue"
        try { & git -C $soGit update-index $flag a.txt 2>&1 | Out-Null } finally { $ErrorActionPreference = $savedEap }
        Assert-True ((Get-GitTreeState -Repo $soGit).Dirty) "Get-GitTreeState missed a file marked $flag."
        $ErrorActionPreference = "Continue"
        try { & git -C $soGit update-index ($flag -replace '^--', '--no-') a.txt 2>&1 | Out-Null } finally { $ErrorActionPreference = $savedEap }
    }
    Assert-True (-not (Get-GitTreeState -Repo $soGit).Dirty) "Get-GitTreeState read the fixture repository as dirty after its flags were cleared."
    Assert-True ((Get-GitTreeState -Repo (Join-Path $soDir "not-a-repo")).Head -eq "none") "Get-GitTreeState gave a commit for a directory that is no repository."

    # The record's writer certifies nothing on its own: without the token of
    # the build-driver.cmd run holding the lock it refuses, before it reads or
    # writes anything. The token is cleared for the child, so a self-test run
    # inside build-driver.cmd cannot reach the real record either.
    $writer = Join-Path $PSScriptRoot "write-gated-manifest.ps1"
    $savedRun = $env:XHCI98_GATED_RUN
    try {
        foreach ($tok in @($null, "not-this-run")) {
            $env:XHCI98_GATED_RUN = $tok
            $ErrorActionPreference = "Continue"
            try {
                $wOut = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $writer -Arch x86 -Flavor release -Finish 2>&1 | Out-String
                $wRc = $LASTEXITCODE
            } finally { $ErrorActionPreference = $savedEap }
            Assert-True ($wRc -ne 0 -and ($wOut -replace '\s+', ' ').Contains("does not certify binaries on its own")) `
                ("write-gated-manifest.ps1 ran outside a build-driver.cmd run (token '{0}'). Output:`n{1}" -f $tok, $wOut)
        }
    } finally {
        $env:XHCI98_GATED_RUN = $savedRun
    }

    # Where the record is checked and written.
    $soPkgText = [System.IO.File]::ReadAllText($packager)
    $soCheckAt = $soPkgText.IndexOf('Get-GatedManifestFault -Manifest $gated')
    $soSnapAt = $soPkgText.IndexOf('$bytes = [System.IO.File]::ReadAllBytes($gatedBinaries[$name])')
    $soHostAt = $soPkgText.IndexOf('Write-Step "host test suite"')
    Assert-True ($soSnapAt -gt 0 -and $soCheckAt -gt $soSnapAt -and $soHostAt -gt $soCheckAt -and $tsSignAt -gt $soCheckAt) `
        "make-package.ps1 no longer reads each binary once and checks the gated build record before its gates and its signing."
    Assert-True ($soPkgText.Contains('$DriverPath = $snapBinaries["xhci98.sys"]')) `
        "make-package.ps1 -SignOnly no longer stages and signs the copy it hashed."
    Assert-True (([regex]::Matches($soPkgText, 'if \(\$runBinaryGates\)')).Count -eq 4) `
        "make-package.ps1 no longer guards exactly its four binary gates (host suite, import gates, helper) by `$runBinaryGates."
    $soBuildText = [System.IO.File]::ReadAllText((Join-Path $repo "scripts\build-driver.cmd"))
    $soLockAt = $soBuildText.IndexOf('mkdir "%GATEDLOCK%" 2>nul')
    $soMainAt = $soBuildText.IndexOf('call :mainbody')
    $soBeginAt = $soBuildText.IndexOf('call :gated -Begin')
    $soBuildAt = $soBuildText.IndexOf('echo === build %FLAVOR% ===')
    $soHlprAt = $soBuildText.IndexOf(':skiphlpr')
    $soFinishAt = $soBuildText.IndexOf('call :gated -Finish %GATEOPT%')
    Assert-True ($soLockAt -gt 0 -and $soMainAt -gt $soLockAt -and $soBeginAt -gt $soMainAt -and $soBuildAt -gt $soBeginAt -and
                 $soHlprAt -gt $soBuildAt -and $soFinishAt -gt $soHlprAt) `
        "build-driver.cmd no longer takes its lock for the run, begins the record before a flavour builds, and finishes it only after the flavour's last gate."
    foreach ($cap in @(
        @{ Gate = '-Image "%OUTSYS%" -Flavor %FLAVOR% -Arch %ARCH% %GATEOPT%'; Cap = 'call :gated -Capture -Name xhci98.sys -Image "%OUTSYS%"' },
        @{ Gate = '-AllowPath "%UASALLOW%" %GATEOPT%';  Cap = 'call :gated -Capture -Name xhciuas.sys -Image "%UASSYS%"' },
        @{ Gate = '-AllowPath "%CMALLOW%" %GATEOPT%';   Cap = 'call :gated -Capture -Name xhcicm.sys -Image "%CMSYS%"' },
        @{ Gate = '-AllowPath "%HLPRALLOW%" %GATEOPT%'; Cap = 'call :gated -Capture -Name xhcihlpr.sys -Image "%HLPRSYS%"' }
    )) {
        $g = $soBuildText.IndexOf($cap.Gate)
        $k = $soBuildText.IndexOf($cap.Cap)
        $h = $soBuildText.IndexOf($cap.Cap.Replace("-Capture", "-Hash"))
        Assert-True ($g -gt 0 -and $k -gt $g -and ($k - $g) -lt 300) `
            ("build-driver.cmd no longer captures the hash right after the import gate: " + $cap.Cap)
        Assert-True ($h -gt 0 -and $g -gt $h -and ($g - $h) -lt 300) `
            ("build-driver.cmd no longer hashes the binary right before its import gate: " + $cap.Cap.Replace("-Capture", "-Hash"))
    }
    # The run's state is noted once, before :mainbody and so before any
    # self-test; :mainbody has a scope of its own, so its exits' endlocal
    # cannot take the cleanup's variables; the token is set for one call.
    $soRunStartAt = $soBuildText.IndexOf('call :gated -RunStart')
    Assert-True ($soRunStartAt -gt $soLockAt -and $soMainAt -gt $soRunStartAt) `
        "build-driver.cmd no longer notes the tree's state when it takes the lock, before :mainbody."
    Assert-True ($soBuildText.Contains(":mainbody`r`nsetlocal`r`n")) "build-driver.cmd's :mainbody no longer opens a scope of its own."
    Assert-True ($soBuildText.Contains("set /p XHCI98_GATED_RUN=<`"%GATEDLOCK%\run.txt`"") -and
                 ([regex]::Matches($soBuildText, 'set "XHCI98_GATED_RUN="')).Count -eq 1 -and
                 ([regex]::Matches($soBuildText, 'XHCI98_GATED_RUN=')).Count -eq 2) `
        "build-driver.cmd sets XHCI98_GATED_RUN somewhere other than around the one writer call."
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
