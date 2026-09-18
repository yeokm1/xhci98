<#
.SYNOPSIS
Regression tests for the INF gate (roadmap Phase 3 task 6).

.DESCRIPTION
A lint nobody has watched fail is not a gate. This runs
scripts\inf-gate\check-inf.ps1 against the production src\xhci98.inf - which
must pass - and then against one deliberately broken copy per rule, asserting
that the specific rule id fires and that the exit code is non-zero. Each
mutation reproduces a real documented failure mode rather than a syntax error:
a $Windows NT$ signature, a dirid-12 destination, a missing .NTx86 section, a
service pointing at a file the install never copies, and so on.

The OS-supplied-file rules (OS-*, PKG-*) get the same treatment, and they
need it most: every way of unwiring the LayoutFile route - the directive gone,
a media-name field, a lost NO_OVERWRITE flag, a path that stops copying
usbd.sys, a Microsoft file back on the media - is silent on the target and
shows up only as the root hub failing to load.

Everything happens on copies under the host temporary directory. The package
tests use stand-in files, so nothing here needs the git-ignored tools\
staging: no build, no VM, and no Microsoft binaries.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\test-inf-checks.ps1
#>

[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

$repo = Get-RepoRoot
$gate = Join-Path $PSScriptRoot "check-inf.ps1"
$prodInf = Join-Path $repo "src\xhci98.inf"
$prodInfAmd64 = Join-Path $repo "src\xhci98-amd64.inf"

. (Join-Path (Split-Path -Parent $PSScriptRoot) "test-harness.ps1")

function Invoke-Gate {
    param([string]$Path, [string]$PackageDir = "", [string[]]$Extra = @())
    $psArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $gate, "-InfPath", $Path)
    if ($PackageDir -ne "") { $psArgs += @("-PackageDir", $PackageDir) }
    if ($Extra.Count -gt 0) { $psArgs += $Extra }
    # ErrorActionPreference relaxed across the call, as
    # `scripts\import-gate\check-imports.ps1` relaxes it for dumpbin (repo audit
    # D5): in Windows PowerShell 5.1 each stderr line from a native command
    # becomes an ErrorRecord, and under "Stop" the first one aborts this
    # self-test with an empty message. That matters most here, because the gate
    # under test is *expected* to fail on most of these cases and its diagnosis
    # is what the assertions read.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & powershell.exe @psArgs 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $saved
    }
    return @{ Output = $out; ExitCode = $LASTEXITCODE }
}

function New-MutatedInf {
    # $Mutate takes the production text and returns the text to write.
    # -Source names which production INF to mutate: the 32-bit file by default,
    # or src\xhci98-amd64.inf for the -Arch amd64 cases at the foot of this
    # file. One mutator rather than two, so the "the mutation has to actually
    # mutate" guard below covers both files.
    param([string]$Name, [scriptblock]$Mutate, [switch]$Utf16, [switch]$LfOnly, [switch]$Latin1, [switch]$BareCr,
          [switch]$Utf8Bom, [switch]$InfUnchanged, [string]$Source = "")
    if ($Source -eq "") { $Source = $prodInf }
    $original = [System.IO.File]::ReadAllText($Source)
    $text = & $Mutate $original

    # **The mutation has to actually mutate**, and this is where that is
    # checked once for every case rather than trusted case by case. A pattern
    # that stops matching - because the value it was anchored on changed -
    # writes the production text back out unchanged, and a case asserting a
    # rule FIRES then runs against an INF that is not broken. It fails, so it
    # is loud; but it is loud in the wrong direction, reading as "that rule
    # stopped working" and sending the next person into the gate instead of
    # into this file. It has happened once for real (the Provider string, see
    # the note below) and was latent a second time: the DriverVer un-padding
    # regexes below stripped leading zeros, which does nothing at all to a
    # release date like 10/22/2026 - and one of the three cases they feed
    # expects exit 0, so it would have *passed*, on the unmutated file.
    #
    # Two kinds of case are exempt and say so at the call. The three encoding
    # switches: for those the bytes are the mutation and identical text is the
    # point. And -InfUnchanged, for the version cross-check block, where what
    # is mutated is the `xhci98.rc` staged beside the INF - those cases assert
    # their own mutation landed, one file over.
    if ($text -ceq $original -and -not ($Utf16 -or $LfOnly -or $BareCr -or $Utf8Bom -or $InfUnchanged)) {
        Assert-True $false ("$Name : the mutation left " + (Split-Path -Leaf $Source) + " unchanged, so whatever this case asserts, it asserts it about the production INF. Its pattern no longer matches - fix the pattern, not the gate.")
    }

    $path = Join-Path $script:work ("$Name.inf")
    if ($BareCr) {
        # ONE bare CR, not a whole-file CRLF -> CR conversion. The rule under
        # test (FILE-EOL's CR arithmetic, repo audit D5) exists because a lone CR
        # is a line break this gate's own CRLF splitting cannot see, so the case
        # worth witnessing is the one where FILE-EOL is the ONLY thing that can
        # object: a classic-Mac file would trip a dozen structural rules and
        # would still "pass" this assertion with the CR arithmetic deleted.
        #
        # It goes at the head of the file, where Read-Inf's per-line Trim()
        # swallows it and every other rule sees the production INF unchanged.
        [System.IO.File]::WriteAllText($path, ("`r" + $text), (New-Object System.Text.ASCIIEncoding))
    } elseif ($Utf8Bom) {
        # Three bytes of BOM in front of otherwise perfect ASCII - the one
        # encoding mistake an editor makes silently on save, and the branch of
        # FILE-ENCODING that had no test until the 2026-09-07 audit's H9.
        # Neither setup engine reads a BOM as anything but garbage at the head
        # of [Version].
        [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($true)))
    } elseif ($Utf16) {
        [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UnicodeEncoding($false, $true)))
    } elseif ($Latin1) {
        # ASCIIEncoding would fold the high byte back to '?', which is exactly
        # the byte the gate is meant to catch.
        [System.IO.File]::WriteAllText($path, $text, [System.Text.Encoding]::GetEncoding(28591))
    } elseif ($LfOnly) {
        [System.IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"), (New-Object System.Text.ASCIIEncoding))
    } else {
        [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.ASCIIEncoding))
    }
    return $path
}

function Assert-RuleFires {
    param([string]$Name, [string]$Rule, [scriptblock]$Mutate, [switch]$Utf16, [switch]$LfOnly, [switch]$Latin1, [switch]$BareCr,
          [switch]$Utf8Bom, [string]$Source = "", [string]$Arch = "")
    $path = New-MutatedInf -Name $Name -Mutate $Mutate -Utf16:$Utf16 -LfOnly:$LfOnly -Latin1:$Latin1 -BareCr:$BareCr -Utf8Bom:$Utf8Bom -Source $Source
    $extra = @()
    if ($Arch -ne "") { $extra = @("-Arch", $Arch) }
    $r = Invoke-Gate -Path $path -Extra $extra
    Assert-True ($r.ExitCode -ne 0) ("$Name : the gate accepted a broken INF (exit 0).")
    # A WARN line carries the same [RULE] tag, and several rules have both
    # forms, so only a FAIL line counts as the rule firing.
    Assert-True ($r.Output -match [regex]::Escape("FAIL [$Rule]")) ("$Name : expected rule $Rule to fail. Output was:`n" + $r.Output)
}

$tempBase = [System.IO.Path]::GetFullPath($env:TEMP)
$script:work = Join-Path $tempBase ("xhci98-inf-gate-test-" + [System.IO.Path]::GetRandomFileName())

try {
    New-Item -ItemType Directory -Path $script:work | Out-Null

    Write-Step "the production INF must pass"
    $baseline = Invoke-Gate -Path $prodInf
    Assert-True ($baseline.ExitCode -eq 0) ("src\xhci98.inf does not pass its own gate:`n" + $baseline.Output)
    Assert-True ($baseline.Output -notmatch "FAIL \[") "src\xhci98.inf produced a FAIL line."
    Assert-True ($baseline.Output -notmatch "WARN:") ("src\xhci98.inf produced a warning:`n" + $baseline.Output)
    # Both models sections READ - the undecorated one and NT 6.x's - for the
    # reason the 64-bit baseline below gives.
    Assert-True ($baseline.Output -match "models: 2\b") ("src\xhci98.inf: expected the gate to gather two models (the undecorated one and NTx86.6.0). Output:`n" + $baseline.Output)

    Write-Step "file format"
    Assert-RuleFires "utf16" "FILE-ENCODING" { param($t) $t } -Utf16
    Assert-RuleFires "lfonly" "FILE-EOL" { param($t) $t } -LfOnly
    # The other half of FILE-EOL. Until this case existed, the D5 bare-CR
    # arithmetic was the one FILE-* rule with no mutation proving it fires -
    # `-LfOnly` exercises the LF branch only, and a regression of the CR branch
    # would have passed a step titled "file format".
    Assert-RuleFires "cronly" "FILE-EOL" { param($t) $t } -BareCr
    # **Matched by pattern, not by the provider's value.** This mutation used to
    # spell out `xhci98 Project`, so changing the Provider string (
    # to a person's name) turned the Replace into a no-op: the "mutated" text
    # was identical to the original, and a test that asserts a rule FIRES on a
    # file it never actually mutated is testing nothing.
    #
    # It would have *failed* rather than passed - Assert-RuleFires demands a
    # nonzero exit, and an unmodified INF gives zero - so the breakage would
    # have been loud. Loud but **misdirecting**, which is the reason to fix the
    # shape rather than rely on it: the failure reads as "FILE-ENCODING stopped
    # working" and sends the next person into the gate's encoding check, when
    # what actually happened is that this line stopped editing the file at all.
    # Anchoring on the
    # quoted-value form makes it independent of what the value happens to be -
    # and the form is stable, because a [Strings] entry has to be quoted. The
    # `Provider=%Provider%` reference in [Version] has no quote after the `=`,
    # so it cannot match and the substitution stays single-site.
    Assert-RuleFires "highbyte" "FILE-ENCODING" {
        param($t) $t -replace 'Provider="([^"]*)"', ('Provider="$1' + [char]0xF6 + '"')
    } -Latin1

    Write-Step "Win98 parser traps"
    # 29 characters, one over the limit, renamed at both the header and the
    # single reference so nothing else can fire first.
    Assert-RuleFires "sectlen" "W98-SECTLEN" {
        param($t) $t.Replace("Xhci.AddService", "Xhci.AddServiceForTheDeviceXX")
    }
    Assert-RuleFires "dupsect" "W98-DUPSECT" {
        param($t) $t + "`r`n[Strings]`r`nProvider=`"second one`"`r`n"
    }
    Assert-RuleFires "dirid12" "W98-DIRID12" {
        param($t) $t.Replace("Xhci.CopyFiles=10,System32\Drivers", "Xhci.CopyFiles=12")
    }
    Assert-RuleFires "dirid12-stray" "W98-DIRID12" {
        param($t) $t.Replace("HKR,,NTMPDriver,,xhci98.sys", "HKR,,NTMPDriver,,xhci98.sys`r`nHKR,,Image,,%12%\xhci98.sys")
    }
    Assert-RuleFires "long-tmp" "W98-83PATH" {
        param($t) $t.Replace("xhci98.sys,,xhci98.tmp", "xhci98.sys,,xhci98pending.tmp")
    }
    Assert-RuleFires "long-subdir" "W98-83PATH" {
        param($t) $t.Replace("Xhci.CopyFiles=10,System32\Drivers", "Xhci.CopyFiles=10,System32\DriversDir")
    }

    Write-Step "shared rules"
    Assert-RuleFires "signature" "BOTH-VERSION" {
        param($t) $t.Replace('Signature="$CHICAGO$"', 'Signature="$Windows NT$"')
    }
    Assert-RuleFires "classguid" "BOTH-VERSION" {
        param($t) $t.Replace("{36FC9E60-C465-11CF-8056-444553540000}", "{4D36E97D-E325-11CE-BFC1-08002BE10318}")
    }
    Assert-RuleFires "driverver" "BOTH-VERSION" {
        param($t) $t -replace 'DriverVer=\d{2}/\d{2}/\d{4},[\d.]+', 'DriverVer=1.0.0.0'
    }
    #
    # Task 12.4's exception, and it is tested in the direction that matters: what
    # the switch does NOT relax. These run here rather than after the block
    # below, because that block leaves a mutated xhci98.rc in the work directory
    # and every INF beside it then fails the version cross-check first - which
    # would make these cases pass for the wrong reason.
    #
    Write-Step "the unpadded-DriverVer exception (task 12.4)"
    Assert-RuleFires "unpadded-default" "BOTH-VERSION" {
        param($t) $t -replace '(?m)^DriverVer=\d+/\d+/', 'DriverVer=8/6/'
    }
    $unpaddedInf = New-MutatedInf -Name "unpadded-allowed" -Mutate {
        param($t) $t -replace '(?m)^DriverVer=\d+/\d+/', 'DriverVer=8/6/'
    }
    $r = Invoke-Gate -Path $unpaddedInf -Extra @("-AllowUnpaddedDriverVer")
    Assert-True ($r.ExitCode -eq 0) `
        ("-AllowUnpaddedDriverVer did not accept an unpadded date:`n" + $r.Output)
    Assert-True ($r.Output -match "AllowUnpaddedDriverVer") `
        ("the relaxed run must say so in its own output. Output was:`n" + $r.Output)
    # The switch relaxes the padding and nothing else: a two-digit year, a
    # non-US date order and a missing version are still refused with it on.
    $stillBad = @(
        @{ Name = "unpadded-2digit-year"; Value = "DriverVer=8/16/26,9.9.9.9" },
        @{ Name = "unpadded-iso-date";    Value = "DriverVer=2026-08-16,9.9.9.9" },
        @{ Name = "unpadded-noversion";   Value = "DriverVer=8/16/2026" }
    )
    foreach ($case in $stillBad) {
        $path = New-MutatedInf -Name $case.Name -Mutate {
            param($t) $t -replace '(?m)^DriverVer=[^\r\n]*', $case.Value
        }
        $r = Invoke-Gate -Path $path -Extra @("-AllowUnpaddedDriverVer")
        Assert-True ($r.ExitCode -ne 0) `
            ($case.Name + " : -AllowUnpaddedDriverVer accepted '" + $case.Value + "', which it does not cover.")
        Assert-True ($r.Output -match [regex]::Escape("[BOTH-VERSION]")) `
            ($case.Name + " : expected BOTH-VERSION. Output was:`n" + $r.Output)
    }
    # And the switch does not disable the rest of the gate.
    $r = Invoke-Gate -Path (New-MutatedInf -Name "unpadded-and-broken" -Mutate {
        param($t) ($t -replace '(?m)^DriverVer=\d+/\d+/', 'DriverVer=8/6/').
                     Replace('Signature="$CHICAGO$"', 'Signature="$Windows NT$"')
    }) -Extra @("-AllowUnpaddedDriverVer")
    Assert-True ($r.ExitCode -ne 0) `
        "-AllowUnpaddedDriverVer accepted an INF with a `$Windows NT`$ signature."

    Write-Step "the version cross-check and the single-source header"
    {
        $prodHdr = Join-Path $repo "src\xhci_version.h"
        $prodRc  = Join-Path $repo "src\xhci98.rc"
        $hdrText = [System.IO.File]::ReadAllText($prodHdr)
        $rcText  = [System.IO.File]::ReadAllText($prodRc)
        $stagedHdr = Join-Path $script:work "xhci_version.h"
        $stagedRc  = Join-Path $script:work "xhci98.rc"
        $ascii = New-Object System.Text.ASCIIEncoding

        function Stage-VersionPair {
            param([string]$Header, [string]$Resource)
            [System.IO.File]::WriteAllText($stagedHdr, $Header, $ascii)
            [System.IO.File]::WriteAllText($stagedRc,  $Resource, $ascii)
        }

        #
        # **The good case first, and it asserts that the check RAN.** The rule
        # skips silently when no `xhci_version.h` is staged beside the INF -
        # which is correct for the packager's staged media and for this suite's
        # hand-written fragments, and fatal here: every case below would pass
        # against a gate that had skipped. So the baseline demands both exit 0
        # and the absence of the skip line.
        #
        Stage-VersionPair $hdrText $rcText
        $path = New-MutatedInf -Name "vergood" -Mutate { param($t) $t } -InfUnchanged
        $r = Invoke-Gate -Path $path
        Assert-True ($r.ExitCode -eq 0) ("vergood : the production INF, header and resource disagree:`n" + $r.Output)
        Assert-True (-not ($r.Output -match "cross-check skipped")) `
            ("vergood : the version cross-check SKIPPED, so every case below would pass vacuously. Output was:`n" + $r.Output)

        #
        # **The header is the authority, so mutate it and expect a refusal.**
        # Three fields, three different mistakes: the version out of step with
        # the INF, the two forms of the version out of step with each other -
        # which nothing in any toolchain would notice, because rc.exe wants the
        # integers and every other consumer wants the string - and the release
        # date, which Windows 2000 ranks a candidate driver by before it looks
        # at the version at all.
        #
        $hdrCases = @(
            @{ Name = "hdrver";  Pattern = '(?m)^(\s*#define\s+XHCI_VER_STR\s+")[\d.]+(")'; Replace = '${1}9.9.9.9${2}' },
            @{ Name = "hdrcsv";  Pattern = '(?m)^(\s*#define\s+XHCI_VER_CSV\s+)\d+,\d+,\d+,\d+'; Replace = '${1}9,9,9,9' },
            @{ Name = "hdrdate"; Pattern = '(?m)^(\s*#define\s+XHCI_DRIVERVER_DATE\s+")[^"]*(")'; Replace = '${1}01/01/2020${2}' }
        )
        foreach ($c in $hdrCases) {
            $mutated = $hdrText -replace $c.Pattern, $c.Replace
            Assert-True ($mutated -ne $hdrText) ($c.Name + " : the mutation matched nothing, so it tests nothing.")
            Stage-VersionPair $mutated $rcText
            $path = New-MutatedInf -Name $c.Name -Mutate { param($t) $t } -InfUnchanged
            $r = Invoke-Gate -Path $path
            Assert-True ($r.ExitCode -ne 0) ($c.Name + " : a version header disagreeing with the INF was accepted.")
            Assert-True ($r.Output -match [regex]::Escape("[BOTH-VERSION]")) ($c.Name + " : expected BOTH-VERSION. Output was:`n" + $r.Output)
        }

        # And each header field deleted outright, which is the other way the
        # authority can stop being readable.
        $hdrGone = @("XHCI_VER_CSV", "XHCI_VER_STR", "XHCI_DRIVERVER_DATE")
        foreach ($name in $hdrGone) {
            $mutated = $hdrText -replace ('(?m)^\s*#define\s+' + $name + '\s+.*\r?\n'), ''
            Assert-True ($mutated -ne $hdrText) ("hdrgone-" + $name + " : the deletion matched nothing, so it tests nothing.")
            Stage-VersionPair $mutated $rcText
            $path = New-MutatedInf -Name ("hdrgone-" + $name) -Mutate { param($t) $t } -InfUnchanged
            $r = Invoke-Gate -Path $path
            Assert-True ($r.ExitCode -ne 0) ("hdrgone-" + $name + " : a version header missing " + $name + " was accepted.")
            Assert-True ($r.Output -match [regex]::Escape("[BOTH-VERSION]")) ("hdrgone-" + $name + " : expected BOTH-VERSION. Output was:`n" + $r.Output)
        }

        #
        # **And the resource must not carry a version of its own.** This is the
        # vacuity guard, and it is the reason the whole block exists in this
        # shape: the first attempt at a macro (src\xhci98.rc's own comment) made
        # the old cross-check satisfiable by the definition it was checking
        # against. A literal put back into any of the four fields compiles
        # perfectly well and would ship a number no gate had compared, so each
        # of the four is mutated back to a literal on its own - a suite that
        # only did FILEVERSION would pass against a gate that only read
        # FILEVERSION, which is what the first draft of the old rule did.
        #
        $rcLiterals = @(
            @{ Name = "rclitfile";     Pattern = '(?m)^(\s*FILEVERSION\s+)XHCI_VER_CSV'; Replace = '${1}9,9,9,9' },
            @{ Name = "rclitprod";     Pattern = '(?m)^(\s*PRODUCTVERSION\s+)XHCI_VER_CSV'; Replace = '${1}9,9,9,9' },
            @{ Name = "rclitfilestr";  Pattern = '(?m)^(\s*VALUE\s+"FileVersion"\s*,\s*)XHCI_VER_STR\s*"\\0"'; Replace = '${1}"9.9.9.9\0"' },
            @{ Name = "rclitprodstr";  Pattern = '(?m)^(\s*VALUE\s+"ProductVersion"\s*,\s*)XHCI_VER_STR\s*"\\0"'; Replace = '${1}"9.9.9.9\0"' }
        )
        foreach ($c in $rcLiterals) {
            $mutated = $rcText -replace $c.Pattern, $c.Replace
            Assert-True ($mutated -ne $rcText) ($c.Name + " : the mutation matched nothing, so it tests nothing.")
            Stage-VersionPair $hdrText $mutated
            $path = New-MutatedInf -Name $c.Name -Mutate { param($t) $t } -InfUnchanged
            $r = Invoke-Gate -Path $path
            Assert-True ($r.ExitCode -ne 0) ($c.Name + " : a resource field carrying a version literal was accepted. The header stops being the authority the moment one of these compiles unchecked.")
            Assert-True ($r.Output -match [regex]::Escape("[BOTH-VERSION]")) ($c.Name + " : expected BOTH-VERSION. Output was:`n" + $r.Output)
        }

        # A field deleted outright, and the include dropped - the two ways the
        # resource can stop referring to the header without carrying a literal.
        $rcOther = @(
            @{ Name = "rcgone";      Pattern = '(?m)^\s*PRODUCTVERSION\s+XHCI_VER_CSV\r?\n'; Replace = '' },
            @{ Name = "rcnoinclude"; Pattern = '(?m)^\s*#include\s+"xhci_version\.h"\r?\n'; Replace = '' }
        )
        foreach ($c in $rcOther) {
            $mutated = $rcText -replace $c.Pattern, $c.Replace
            Assert-True ($mutated -ne $rcText) ($c.Name + " : the mutation matched nothing, so it tests nothing.")
            Stage-VersionPair $hdrText $mutated
            $path = New-MutatedInf -Name $c.Name -Mutate { param($t) $t } -InfUnchanged
            $r = Invoke-Gate -Path $path
            Assert-True ($r.ExitCode -ne 0) ($c.Name + " : a resource that no longer takes its version from the header was accepted.")
            Assert-True ($r.Output -match [regex]::Escape("[BOTH-VERSION]")) ($c.Name + " : expected BOTH-VERSION. Output was:`n" + $r.Output)
        }

        Remove-Item -LiteralPath $stagedHdr -Force
        Remove-Item -LiteralPath $stagedRc -Force
    }.Invoke() | Out-Null

    Write-Step "shared rules, continued"
    Assert-RuleFires "xref-addreg" "BOTH-XREF" {
        param($t) $t.Replace("AddReg=Xhci.AddReg", "AddReg=Xhci.NoSuchReg")
    }
    # A dangling CopyFiles= once threw inside the per-target rules instead of
    # being reported, and the throw took the media layout and the footprint
    # with it. Both are written from the parse, so both must still appear.
    $xrefCopyInf = New-MutatedInf -Name "xref-copyfiles" -Mutate {
        param($t) $t.Replace("CopyFiles=Xhci.CopyFiles,Xhci.CopyW98", "CopyFiles=Xhci.CopyFiles,Xhci.CopyW98,Xhci.NoSuchCopy")
    }
    $xrefLayout = Join-Path $script:work "xref-copyfiles-layout.txt"
    $xrefFootprint = Join-Path $script:work "xref-copyfiles-footprint.txt"
    $r = Invoke-Gate -Path $xrefCopyInf -Extra @("-EmitMediaLayout", $xrefLayout, "-EmitFootprint", $xrefFootprint)
    Assert-True ($r.ExitCode -ne 0) "xref-copyfiles : the gate accepted a dangling CopyFiles= (exit 0)."
    Assert-True ($r.Output -match [regex]::Escape("FAIL [BOTH-XREF]")) ("xref-copyfiles : expected BOTH-XREF to fail. Output was:`n" + $r.Output)
    Assert-True ($r.Output -match "INF gate FAILED") ("xref-copyfiles : the gate did not reach its summary, so it aborted rather than judged. Output was:`n" + $r.Output)
    Assert-True (Test-Path -LiteralPath $xrefLayout) "xref-copyfiles : -EmitMediaLayout wrote nothing; the packagers read that file."
    Assert-True (Test-Path -LiteralPath $xrefFootprint) "xref-copyfiles : -EmitFootprint wrote nothing."
    Assert-RuleFires "no-destdir" "BOTH-DESTDIR" {
        param($t) $t.Replace("Xhci.CopyFiles=10,System32\Drivers`r`n", "")
    }
    Assert-RuleFires "wrong-driver-dest" "BOTH-DESTDIR" {
        param($t) $t.Replace("Xhci.CopyFiles=10,System32\Drivers", "Xhci.CopyFiles=10,System32")
    }
    Assert-RuleFires "no-sourcefile" "BOTH-SOURCE" {
        param($t) $t.Replace("xhci98.sys=1`r`n", "")
    }
    Assert-RuleFires "source-parent-subdir" "BOTH-SOURCE" {
        param($t) $t.Replace("xhci98.inf=1", "xhci98.inf=1,..")
    }
    Assert-RuleFires "source-rooted-subdir" "BOTH-SOURCE" {
        param($t) $t.Replace("xhci98.inf=1", "xhci98.inf=1,C:\usbfiles")
    }
    Assert-RuleFires "no-sourcedisksnames" "BOTH-SOURCE" {
        param($t) $t -replace '(?ms)^\[SourceDisksNames\]\r\n.*?\r\n\r\n', ''
    }
    Assert-RuleFires "undef-string" "BOTH-STRINGS" {
        param($t) $t.Replace("%XhciDesc%=Xhci.Dev", "%XhciDescription%=Xhci.Dev")
    }

    # ---- VAL-* : tasks 11-V.7 and 11-V.9's values, on both paths ----
    #
    # The whole point of the rule is the ASYMMETRY, so the first two cases
    # remove the value from one path at a time. A single case that deleted both
    # would pass just as loudly while proving nothing about the half that
    # matters - and a value on one path only is exactly the state this rule
    # exists to catch.
    #
    # **BOTH of the values are exercised and not just one**, because the rule
    # set runs per value: a case list covering one of them would pass while the
    # other had no coverage at all, which is the same shape as a value written
    # on one install path only. *(This said "all THREE" until the post-Phase 13 review rounds,
    # contradicting the step title three lines below it - there have been two
    # since `XhciLogSnapshot` merged into the ladder.)*
    #
    # *(Every VAL-SZ case went with `XhciLogFile`. The rule fired
    # only for a FLG_ADDREG_TYPE_SZ value and there is no longer one - see the
    # note where it stood in check-inf.ps1. A future REG_SZ value here brings
    # the rule and these cases back together.)*
    Write-Step "the log's two per-device values"

    # **These anchors were rewritten at stage L3**, when `XhciLogSnapshot`
    # merged into the verbosity ladder. Every one of them is a literal
    # `.Replace()` over the INF's own text, so a value leaving the file turns a
    # case into a no-op that reports a PASS while mutating nothing - which is
    # the vacuous-coverage defect this suite once found in `VAL-MISSING`
    # itself. `Assert-RuleFires` refuses a mutation that changed no bytes for
    # exactly that reason; the anchors below are re-cut against the two-value
    # INF rather than trimmed.

    # The 9x path, one value at a time. The whole point of the rule is the
    # ASYMMETRY, so each case removes a value from ONE path: a case that
    # deleted it from both would pass just as loudly while proving nothing
    # about the half that matters.
    Assert-RuleFires "logverbosity-no-9x" "VAL-MISSING" {
        param($t) $t.Replace("HKR,,NTMPDriver,,xhci98.sys`r`nHKR,,XhciLogVerbosity,0x00010001,0",
                             "HKR,,NTMPDriver,,xhci98.sys")
    }
    # Anchored on NTMPDriver, which only the 9x section has, rather than on the
    # comment that used to follow it: that comment introduced
    # [Xhci.AddReg.Global] and went with the section in 1.1.0.0.
    Assert-RuleFires "logdbgview-no-9x" "VAL-MISSING" {
        param($t) $t.Replace("HKR,,NTMPDriver,,xhci98.sys`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                             "HKR,,NTMPDriver,,xhci98.sys`r`nHKR,,XhciLogVerbosity,0x00010001,0")
    }

    # And the NT path, the same two.
    Assert-RuleFires "logverbosity-no-nt" "VAL-MISSING" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,Unrelated,,x")
    }
    Assert-RuleFires "logdbgview-no-nt" "VAL-MISSING" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,Unrelated,,x")
    }

    Assert-RuleFires "logdbgview-type" "VAL-TYPE" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,,0")
    }
    Assert-RuleFires "logdbgview-default" "VAL-DEFAULT" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,1")
    }
    Assert-RuleFires "logdbgview-subkey" "VAL-SUBKEY" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,Parameters,XhciLogDebugView,0x00010001,0")
    }
    # **VAL-DUP had no case at all until the post-Phase 13 review rounds**, which is this suite's own
    # documented failure mode one rule over: `VAL-MISSING` was structurally
    # incapable of firing and only a case found it. The rule is worth having -
    # `Get-AddRegValues` returns every hit and the checker reads `$hits[0]`, so
    # a duplicated value means VAL-TYPE, VAL-DEFAULT and VAL-SUBKEY all judge
    # the FIRST line while the engine picks whichever it likes. The mutation
    # writes the value twice on one path with a *different* default on the
    # second line, which is the shape that actually costs something.
    Assert-RuleFires "logverbosity-duplicated-nt" "VAL-DUP" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogVerbosity,0x00010001,1")
    }

    # **The verbosity default is the one that matters most**, and since the
    # snapshot-value merge it matters twice over: it is the recording switch's
    # polarity AND the read channel's. A ring filling on every machine that ever
    # installs this driver is not the safe configuration, what the append sites
    # cost at real interrupt rates on Windows 98 metal is unmeasured, and a
    # nonzero default here would also open a diagnostic door nobody asked for -
    # exactly the unmeasured assumption task 13-L.1 exists to undo. This case
    # replaces the separate `logsnapshot-default` one, which had the same
    # subject through the value that is gone.
    Assert-RuleFires "logverbosity-default" "VAL-DEFAULT" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0",
                             "[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,1")
    }

    Write-Step "the two install paths"
    Assert-RuleFires "no-ntx86" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev.NTx86]", "[Xhci.Dev.Win2000]")
    }
    Assert-RuleFires "no-services" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev.NTx86.Services]", "[Xhci.Dev.NTx86.Svc]")
    }
    Assert-RuleFires "bad-svc-flag" "PATH-NT" {
        param($t) $t.Replace("AddService=xhci98,0x00000002,Xhci.AddService", "AddService=xhci98,0x00000000,Xhci.AddService")
    }
    Assert-RuleFires "svc-binary-gap" "PATH-NT" {
        param($t) $t.Replace("ServiceBinary=%12%\xhci98.sys", "ServiceBinary=%12%\xhci99.sys")
    }
    Assert-RuleFires "no-servicetype" "PATH-NT" {
        param($t) $t.Replace("ServiceType=1                       ; SERVICE_KERNEL_DRIVER`r`n", "")
    }
    Assert-RuleFires "bad-servicetype" "PATH-NT" {
        param($t) $t.Replace("ServiceType=1                       ; SERVICE_KERNEL_DRIVER", "ServiceType=2                       ; SERVICE_FILE_SYSTEM_DRIVER")
    }
    Assert-RuleFires "bad-starttype" "PATH-NT" {
        param($t) $t.Replace("StartType=3                         ; SERVICE_DEMAND_START", "StartType=4                         ; SERVICE_DISABLED")
    }
    Assert-RuleFires "bad-errorcontrol" "PATH-NT" {
        param($t) $t.Replace("ErrorControl=1                      ; SERVICE_ERROR_NORMAL", "ErrorControl=3                      ; SERVICE_ERROR_CRITICAL")
    }
    Assert-RuleFires "no-devloader" "PATH-W98" {
        param($t) $t.Replace("HKR,,DevLoader,,*NTKERN`r`n", "")
    }
    Assert-RuleFires "no-ntmpdriver" "PATH-W98" {
        param($t) $t.Replace("HKR,,NTMPDriver,,xhci98.sys`r`n", "")
    }
    # The Win98 section keeps NTMPDriver but loses its own CopyFiles of the
    # project files while the .NTx86 section and [DefaultInstall] keep theirs.
    # The global "some CopyFiles section delivers the binary" rule is satisfied
    # by those, and the gate passed exactly this INF (roadmap Phase 20, F14): a
    # clean Windows 98 install would write the loader value and copy no
    # driver. The OS-source list stays, so the OS-* rules are not what fires.
    Assert-RuleFires "w98-copyfiles-gap" "PATH-W98" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98",
                             "[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyW98")
    }
    # The undecorated install section losing its AddReg: Windows 98 binds the
    # device and then loads nothing, because DevLoader and NTMPDriver are what
    # that section carries.
    Assert-RuleFires "w98-no-addreg" "PATH-W98" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyFiles", "[Xhci.Dev]`r`nCopyFiles=Xhci.CopyFiles")
    }
    #
    # **A genuinely NT-only INF** - the mistake this task exists to prevent. It
    # has to fail on the Windows 98 half rather than quietly install on one
    # target. The case that stood here until the 2026-09-07 audit's H9 was
    # named for this and did not build it: it deleted the `AddReg=` line and
    # left `[Xhci.Dev]` in place, so what it exercised was the case above and
    # the Windows 98 *path* branch - a model whose undecorated section is
    # missing entirely - was never produced by anything.
    #
    # The whole section goes, models and all, which is what an author who wrote
    # the INF against Windows 2000 alone would produce.
    #
    Assert-RuleFires "nt-only" "PATH-W98" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98,Xhci.CopyUI`r`n", "")
    }

    # ---- the files the OS supplies (Phase 17, release 1.0.0.1; Phase 19) ----
    #
    # Since 1.0.0.1 the media carries no Microsoft file: usbd.sys and usbhub.sys
    # are copied from the OS's own install source through LayoutFile, and since
    # 1.0.1.0 the NT path copies usbport.sys and usbhub20.sys the same way.
    # Every way of unwiring that is silent on the target - a root hub at Code 2
    # on Windows 98, a 0xc0000034 naming usbhub20.sys on Windows 2000, Code 39
    # with an empty trace on an NT install that never had usbport.sys - so each
    # rule is watched firing here, in both directions where the asymmetry has
    # two.
    Write-Step "the files the OS supplies"
    Assert-RuleFires "os-no-layoutfile" "OS-LAYOUT" {
        param($t) $t.Replace("LayoutFile=layout.inf`r`n", "")
    }
    Assert-RuleFires "os-wrong-layoutfile" "OS-LAYOUT" {
        param($t) $t.Replace("LayoutFile=layout.inf", "LayoutFile=usb.inf")
    }
    # A Microsoft file back on the media, under its own name and under the
    # 1.0.0.0 media name: the exception legal-provenance section 5 withdrew.
    Assert-RuleFires "os-media-usbd" "OS-MEDIA" {
        param($t) $t.Replace("xhci98.inf=1`r`n", "xhci98.inf=1`r`nusbd.sys=1`r`n")
    }
    Assert-RuleFires "os-media-retired-name" "OS-MEDIA" {
        param($t) $t.Replace("xhci98.inf=1`r`n", "xhci98.inf=1`r`nusbd98.sys=1`r`n")
    }
    # A path that stops asking for usbd.sys, on each target and on each route.
    Assert-RuleFires "os-no-usbd-w98" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16`r`n", "[Xhci.CopyW98]`r`n")
    }
    Assert-RuleFires "os-no-usbd-nt" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT",
                             "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles")
    }
    Assert-RuleFires "os-default-no-usbd" "OS-MISSING" {
        param($t) $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT",
                             "[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles")
    }
    # The Windows 98 composite parent gone: the bug batch 13-E found on real
    # hardware, every composite device at Code 2 with nothing saying why.
    Assert-RuleFires "os-no-usbhub-w98" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16`r`nusbhub.sys,,,16", "[Xhci.CopyW98]`r`nusbd.sys,,,16")
    }
    # The NT path's own two, one at a time. usbport.sys gone is the Windows
    # XP reading of 2026-09-03: Code 39, the trace empty, nothing else saying
    # why. usbhub.sys gone is the hub driver the OS cannot bind.
    Assert-RuleFires "os-no-usbport-nt" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`n", "[Xhci.CopyNT]`r`n")
    }
    Assert-RuleFires "os-no-usbhub-nt" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16`r`nusbhub.sys,,,16`r`n",
                             "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16`r`n")
    }
    # The opposite direction: the Windows 98 path asking for a file its
    # layout.inf has no row for, so its engine has no source to resolve it
    # from. usbport.sys comes from NUSB or SweetLow there.
    Assert-RuleFires "os-usbport-on-w98" "OS-ONWIN98" {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", "[Xhci.CopyW98]`r`nusbport.sys,,,16`r`nusbd.sys,,,16")
    }
    Assert-RuleFires "os-default-nt-list-on-w98" "OS-ONWIN98" {
        param($t) $t.Replace("[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98",
                             "[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyNT")
    }
    # usbhub20.sys on any path, or on the media: Windows 2000's own USB.INF
    # places it with the root hub, XP has no such file, and the owner's
    # decision of 2026-09-03 is that this INF never names it.
    Assert-RuleFires "os-usbhub20-on-nt" "OS-NEVER" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbhub20.sys,,,16")
    }
    Assert-RuleFires "os-usbhub20-on-w98" "OS-NEVER" {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", "[Xhci.CopyW98]`r`nusbhub20.sys,,,16`r`nusbd.sys,,,16")
    }
    Assert-RuleFires "os-usbhub20-on-media" "OS-NEVER" {
        param($t) $t.Replace("xhci98.inf=1`r`n", "xhci98.inf=1`r`nusbhub20.sys=1`r`n")
    }
    # No NT half at all: setupapi falls back to the undecorated section, and a
    # right-click Install on Windows 2000 runs the Windows 98 file list.
    Assert-RuleFires "no-defaultinstall-nt" "OS-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n", "")
    }
    Assert-RuleFires "os-dup" "OS-DUP" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16`r`nusbd.sys,,,16")
    }
    # A media-name field sends the engine back to this disk for the file, which
    # is the 1.0.0.0 shape.
    Assert-RuleFires "os-srcname" "OS-SRCNAME" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,usbd2k.sys,,16")
    }
    Assert-RuleFires "os-no-flag" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys")
    }
    Assert-RuleFires "os-no-flag-usbport" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys")
    }
    # 16|4: NO_OVERWRITE plus NOVERSIONCHECK, which overwrites the target
    # regardless of version - including a newer serviced usbd.sys.
    Assert-RuleFires "os-noversioncheck" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,20")
    }
    # Dirid 11 is \Windows\System, where nothing looks for it - and the copy
    # succeeds.
    Assert-RuleFires "os-dest" "OS-DEST" {
        param($t) $t.Replace("Xhci.CopyW98=10,System32\Drivers", "Xhci.CopyW98=11")
    }

    # ---- usbui.dll, new in 1.0.2.0 -----------------------------------------
    #
    # **The 2026-09-07 audit's H7.** This file is the change the release turns
    # on, and it had ZERO self-test coverage: the missing-file, on-media,
    # never-named and flag rules were all untested for it, and so was the
    # wrong-destination rule - which matters more than the rest, because
    # `usbui.dll` is the only OS-supplied row that does not go to dirid 10 and
    # the per-row destination mechanism was introduced by the same commit that
    # added it. A rule with one user and no negative test is a rule nobody has
    # watched fail.
    #
    # What each break costs on a target: the NT root hub's Power tab silently
    # absent (the file not copied), or the file landing in System32\Drivers
    # where the property-page loader does not look for it, which is the same
    # silence from the other direction.
    Write-Step "usbui.dll on every route"

    # Gone from the 9x path, and from the NT path, one at a time.
    Assert-RuleFires "os-no-usbui-w98" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98,Xhci.CopyUI",
                             "[Xhci.Dev]`r`nAddReg=Xhci.AddReg`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98")
    }
    Assert-RuleFires "os-no-usbui-nt" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI",
                             "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT")
    }
    # And from a right-click route, which is the one a user with an earlier
    # release takes and the one no per-file rule would otherwise reach.
    Assert-RuleFires "os-no-usbui-9x-default" "OS-MISSING" {
        param($t) $t.Replace("[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98,Xhci.CopyUI",
                             "[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98")
    }
    # The row emptied rather than the section dropped: the section still exists
    # and [DestinationDirs] still names it, so nothing structural is missing.
    Assert-RuleFires "os-empty-copyui" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyUI]`r`nusbui.dll,,,16`r`n", "[Xhci.CopyUI]`r`n")
    }
    # **The wrong destination**, which is the rule this file is the only user
    # of: dirid 10 is System32\Drivers, where a user-mode property-page DLL is
    # not looked for. The copy succeeds and the tab is still missing.
    Assert-RuleFires "os-usbui-dest-drivers" "OS-DEST" {
        param($t) $t.Replace("Xhci.CopyUI=11", "Xhci.CopyUI=10,System32\Drivers")
    }
    # Dirid 12 is System32\Drivers by another spelling, so this is the same
    # mistake written the way an editor is most likely to write it.
    Assert-RuleFires "os-usbui-dest-12" "OS-DEST" {
        param($t) $t.Replace("Xhci.CopyUI=11", "Xhci.CopyUI=12")
    }
    # On the media: the file is Microsoft's, and this project ships none.
    Assert-RuleFires "os-usbui-on-media" "OS-MEDIA" {
        param($t) $t.Replace("xhci98.inf=1`r`n", "xhci98.inf=1`r`nusbui.dll=1`r`n")
    }
    # Without COPYFLG_NO_OVERWRITE it would replace the machine's own copy,
    # which on a machine that has one is a downgrade and a CD prompt.
    Assert-RuleFires "os-usbui-no-flag" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyUI]`r`nusbui.dll,,,16", "[Xhci.CopyUI]`r`nusbui.dll")
    }
    # A media-name field on it, the 1.0.0.0 shape applied to the new file.
    Assert-RuleFires "os-usbui-srcname" "OS-SRCNAME" {
        param($t) $t.Replace("[Xhci.CopyUI]`r`nusbui.dll,,,16", "[Xhci.CopyUI]`r`nusbui.dll,usbui2k.dll,,16")
    }

    # ---- rules whose negative branch had no test (audit H9) ----------------
    #
    # Each of these is a branch the gate carries and nothing had ever watched
    # fire. A rule that has never failed is a rule that might not be able to.
    Write-Step "the branches that had no negative test"

    # The encoding rule's UTF-8 byte-order-mark branch. Neither setup engine
    # reads a BOM as anything but three bytes of garbage at the head of
    # [Version], and the file is otherwise valid ASCII, so this is the one
    # encoding mistake an editor makes silently on save.
    Assert-RuleFires "file-encoding-bom" "FILE-ENCODING" {
        param($t) $t
    } -Utf8Bom

    # The 8.3 path rule: a copy row naming a file whose stem is longer than
    # eight characters. Windows 98's 16-bit engine truncates it.
    Assert-RuleFires "long-file-name" "W98-83PATH" {
        param($t) $t.Replace("[Xhci.CopyFiles]`r`nxhci98.sys,,xhci98.tmp",
                             "[Xhci.CopyFiles]`r`nxhci98driver.sys,,xhci98.tmp")
    }

    # The temporary-name field on the driver's own copy row (audit H10). Its
    # absence is a documented Windows 98 trap - the replace over the loaded
    # binary fails - and until that audit only the footprint diff noticed.
    Assert-RuleFires "w98-no-tempname" "W98-TEMPNAME" {
        param($t) $t.Replace("xhci98.sys,,xhci98.tmp", "xhci98.sys")
    }

    # Both right-click sections gone (audit H8). This is the shape that used to
    # pass green: with neither section present the OS-* and SUSP-* route lists
    # simply get shorter, so half the install routes stop being checked instead
    # of failing.
    Assert-RuleFires "no-default-sections-at-all" "OS-DEFAULT" {
        param($t)
        $s = $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n", "")
        $s.Replace("[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98,Xhci.CopyUI`r`n", "")
    }

    # COPYFLG_NO_VERSION_DIALOG (32), which the table in build-and-test.md
    # rejects and which the flag rule did not object to until audit H11. 16|32
    # keeps NO_OVERWRITE set, so only the added bit is under test.
    Assert-RuleFires "os-no-version-dialog" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,48")
    }
    # COPYFLG_FORCE_FILE_IN_USE (8), the third refused bit, likewise untested.
    Assert-RuleFires "os-force-file-in-use" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,24")
    }
    # COPYFLG_OVERWRITE_OLDER_ONLY (64), the fourth.
    Assert-RuleFires "os-overwrite-older" "OS-FLAGS" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,16", "[Xhci.CopyNT]`r`nusbport.sys,,,16`r`nusbd.sys,,,80")
    }

    # ---- SUSP-* : no idle-suspend registry value, anywhere --------------
    #
    # Inverted on 2026-09-17 with the rules themselves. From 1.0.1.0 to 1.0.2.0
    # these cases asserted that removing the machine-wide value from any of the
    # four routes failed the gate; the file wrote it and the driver did not.
    # Since 1.1.0.0 the driver declares USB_MINIPORT_FLAGS_DISABLE_SS (0x20)
    # and the file writes nothing, so the cases ADD a value back and assert
    # the gate refuses it.
    #
    # Adding is the harder direction to test and the one that matters. A rule
    # that merely stopped demanding the value would pass the production file
    # while saying nothing about a later edit that puts it back - and putting
    # it back is exactly what an editor reaching for the old fix would do.
    Write-Step "no idle-suspend registry value on any route"
    $suspRow = "HKLM,System\CurrentControlSet\Services\USB,DisableSelectiveSuspend,0x00010001,1"
    # On the 9x device install's own AddReg section.
    Assert-RuleFires "susp-global-9x" "SUSP-GLOBAL" {
        param($t) $t.Replace("[Xhci.AddReg]`r`nHKR,,DevLoader,,*NTKERN",
                             "[Xhci.AddReg]`r`n$suspRow`r`nHKR,,DevLoader,,*NTKERN")
    }
    # And on the NT one, which is where releases 1.0.1.0 to 1.0.2.0 had it.
    Assert-RuleFires "susp-global-nt" "SUSP-GLOBAL" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`n$suspRow`r`nHKR,,XhciLogVerbosity")
    }
    # The whole-file check's own case: a section no install route references.
    # The old route-walking rules would not have looked here, and a section
    # sitting in the file is one line away from being wired up again.
    Assert-RuleFires "susp-global-orphan" "SUSP-GLOBAL" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`n",
                             "[Xhci.AddReg.Global]`r`n$suspRow`r`n`r`n[Xhci.AddReg.NT]`r`n")
    }
    # The per-controller values, the replacement that was measured and refused:
    # usbport writes the first one back, and on Vista from a power-setting
    # callback (2026-09-17). Both spellings, because Windows 7 has only the
    # second and the NT 6.x path serves Vista and Windows 7 together.
    Assert-RuleFires "susp-hc-per-controller" "SUSP-HCVALUE" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`nHKR,,HcDisableSelectiveSuspend,0x00010001,1`r`nHKR,,XhciLogVerbosity")
    }
    Assert-RuleFires "susp-hc-all-win7" "SUSP-HCVALUE" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`nHKR,,HcDisableAllSelectiveSuspend,0x00010001,1`r`nHKR,,XhciLogVerbosity")
    }
    # A 0 is refused too. The old SUSP-VALUE rule existed because present-and-0
    # was a silently disabled fix; the same reasoning inverted says the value's
    # data is not what makes it unwanted - its being here at all is.
    Assert-RuleFires "susp-global-zero" "SUSP-GLOBAL" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`nHKLM,System\CurrentControlSet\Services\USB,DisableSelectiveSuspend,0x00010001,0`r`nHKR,,XhciLogVerbosity")
    }

    # ---- -EmitFootprint (roadmap tasks 11-B.3 and 11-V.3) ------------------
    #
    # The footprint is what task 11-V.3's uninstall clause is checked against,
    # so the thing to test is not that it prints something - it is that every
    # column is DERIVED. A verdict column that always said the same word would
    # look identical on the production INF and would be worthless on the run.
    # Each mutation below therefore changes an input and asserts the output
    # moved with it.
    Write-Step "the install footprint"

    function Get-Footprint {
        param([string]$Path)
        $out = Join-Path $script:work ("fp-" + [System.IO.Path]::GetRandomFileName() + ".txt")
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate `
            -InfPath $Path -EmitFootprint $out | Out-Null
        if (-not (Test-Path -LiteralPath $out)) { return $null }
        # Comment lines carry the source path and the legend, neither of which
        # is a claim about this INF's contents.
        return @(Get-Content -LiteralPath $out | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne "" })
    }

    # **Every non-comment line must be a row type this format defines.** That
    # sounds like a tautology and is not: the header block is built from
    # double-quoted PowerShell strings, and a backtick inside one of them is an
    # escape - a literal `remove` in the legend expanded to a carriage return
    # plus "emove" and split the header into a line that was not a comment and
    # was not a row. This check is what noticed. It is also the net over any
    # future row type added to the emitter and not to the legend.
    $knownRowTypes = @("path", "file", "reg", "service", "servicevalue", "unmodelled")
    $prodRows = Get-Footprint -Path $prodInf
    Assert-True ($null -ne $prodRows) "-EmitFootprint wrote no file for the production INF."
    foreach ($row in @($prodRows)) {
        $kind = ($row -split '\|')[0]
        Assert-True ($knownRowTypes -contains $kind) (
            "footprint line is neither a comment nor a known row type: '$row'")
    }

    $tracked = Join-Path $repo "scripts\inf-gate\expected-footprint.txt"
    Assert-True (Test-Path -LiteralPath $tracked) "scripts\inf-gate\expected-footprint.txt is missing; regenerate it with -EmitFootprint."
    if (Test-Path -LiteralPath $tracked) {
        $expected = @(Get-Content -LiteralPath $tracked | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne "" })
        $actual = Get-Footprint -Path $prodInf
        Assert-True ($null -ne $actual) "-EmitFootprint wrote no file for the production INF."
        Assert-True ((($actual -join "`n")) -eq (($expected -join "`n"))) (
            "the production INF's footprint differs from scripts\inf-gate\expected-footprint.txt." +
            "`nIf the INF's file or registry footprint really changed, task 11-V.3's uninstall" +
            "`nexpectation changed with it - regenerate the file and say so in the commit:" +
            "`n  powershell -File scripts\inf-gate\check-inf.ps1 -EmitFootprint scripts\inf-gate\expected-footprint.txt" +
            "`n`n--- expected ---`n" + ($expected -join "`n") +
            "`n`n--- actual ---`n" + ($actual -join "`n"))
    }

    # The verdict is read out of the copy flags, not attached to a filename.
    # Drop NO_OVERWRITE and usbd.sys becomes a file this package did place -
    # which is also why the gate fails the mutation, so the emit must happen
    # before the verdict for this to be observable at all.
    $noFlagInf = New-MutatedInf -Name "fp-no-overwrite" -Mutate {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", "[Xhci.CopyW98]`r`nusbd.sys,,,0")
    }
    # A CopyFiles section reachable from more than one install path appears
    # once per path - Xhci.CopyW98 is named by [Xhci.Dev] and by
    # [DefaultInstall] - so assert the verdict of EVERY matching row rather
    # than a count. A count would have to be updated whenever a path is added,
    # and the update most likely to be made is the one that makes it pass.
    function Assert-RowVerdict {
        param([string[]]$Rows, [string]$Prefix, [string]$Verdict, [string]$What)
        $matched = @($Rows | Where-Object { $_ -like ($Prefix + "*") })
        Assert-True ($matched.Count -ge 1) ("$What : no row matched '$Prefix'. Rows:`n" + ($Rows -join "`n"))
        $wrong = @($matched | Where-Object { $_ -notlike ("*|" + $Verdict) })
        Assert-True ($wrong.Count -eq 0) ("$What : expected every '$Prefix' row to read '$Verdict'. Rows:`n" + ($Rows -join "`n"))
    }

    $fp = Get-Footprint -Path $noFlagInf
    Assert-True ($null -ne $fp) "-EmitFootprint wrote nothing for an INF the gate rejects; it must be written from the parse, not the verdict."
    Assert-RowVerdict -Rows $fp -Prefix "file|Windows 98|Xhci.CopyW98|10|System32\Drivers|usbd.sys|" -Verdict "remove" `
        -What "dropping COPYFLG_NO_OVERWRITE must flip the Win98 usbd.sys rows"
    Assert-RowVerdict -Rows $fp -Prefix "file|Windows 2000|Xhci.CopyNT|" -Verdict "keep" `
        -What "the untouched Windows 2000 OS-file rows"

    # An unparseable flags field is not silently a zero.
    $badFlagInf = New-MutatedInf -Name "fp-bad-flags" -Mutate {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", "[Xhci.CopyW98]`r`nusbd.sys,,,sixteen")
    }
    $fp = Get-Footprint -Path $badFlagInf
    Assert-RowVerdict -Rows $fp -Prefix "file|Windows 98|Xhci.CopyW98|10|System32\Drivers|usbd.sys|" -Verdict "review" `
        -What "an unparseable copy-flags field must read 'review', not a guess"

    # NO_OVERWRITE is not the only flag that stops this package claiming a file,
    # and a first draft of this derivation treated it as if it were. Each row
    # below is a SETUPAPI.H flag whose description says the copy is conditional
    # or that the target had to exist already; every one of them read 'remove'
    # before the review that found this.
    foreach ($case in @(
        @{ Flags = "4";     Verdict = "remove"; Why = "COPYFLG_NOVERSIONCHECK ignores versions and overwrites, so the copy is unconditional - a first draft called it conditional" },
        @{ Flags = "32";    Verdict = "review"; Why = "COPYFLG_NO_VERSION_DIALOG makes the copy conditional on version" },
        @{ Flags = "64";    Verdict = "review"; Why = "COPYFLG_OVERWRITE_OLDER_ONLY leaves an equal-version target alone" },
        @{ Flags = "1024";  Verdict = "keep";   Why = "COPYFLG_REPLACEONLY copies only onto a file that already existed" },
        @{ Flags = "65536"; Verdict = "review"; Why = "a flag bit this emitter has not been taught is reported, not ignored" }
    )) {
        $inf2 = New-MutatedInf -Name ("fp-copyflag-" + $case.Flags) -Mutate {
            param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", ("[Xhci.CopyW98]`r`nusbd.sys,,," + $case.Flags))
        }.GetNewClosure()
        $fp = Get-Footprint -Path $inf2
        Assert-RowVerdict -Rows $fp -Prefix "file|Windows 98|Xhci.CopyW98|10|System32\Drivers|usbd.sys|" -Verdict $case.Verdict `
            -What ("copy flags " + $case.Flags + ": " + $case.Why)
    }

    # The same on the registry side. FLG_ADDREG_TYPE_DWORD is 0x00010001, so
    # each of these ORs one operation bit into the production value.
    foreach ($case in @(
        @{ Flags = "0x00010003"; Verdict = "keep";   Why = "FLG_ADDREG_NOCLOBBER writes only if the value was absent - the registry twin of COPYFLG_NO_OVERWRITE, and a draft classified the same condition two ways by making this one 'review'" },
        @{ Flags = "0x00010005"; Verdict = "none";   Why = "FLG_ADDREG_DELVAL places nothing at all" },
        @{ Flags = "0x00010009"; Verdict = "review"; Why = "FLG_ADDREG_APPEND may leave pre-existing REG_MULTI_SZ elements - recognised is not modelled, and a first draft let this fall through to remove" },
        @{ Flags = "0x00010011"; Verdict = "review"; Why = "FLG_ADDREG_KEYONLY creates the key and ignores the value this row names, so the row's own subject was never written" },
        @{ Flags = "0x00010021"; Verdict = "keep";   Why = "FLG_ADDREG_OVERWRITEONLY writes only a value that already existed" },
        @{ Flags = "0x00010081"; Verdict = "review"; Why = "an operation bit this emitter has not been taught is reported" },
        @{ Flags = "4294967296"; Verdict = "review"; Why = "an out-of-range flags field is a row, not an aborted emit - the same policy the copy side has" }
    )) {
        $inf2 = New-MutatedInf -Name ("fp-addregflag-" + $case.Flags) -Mutate {
            param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView,0x00010001,0",
                                 ("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity,0x00010001,0`r`nHKR,,XhciLogDebugView," + $case.Flags + ",0"))
        }.GetNewClosure()
        $fp = Get-Footprint -Path $inf2
        Assert-RowVerdict -Rows $fp -Prefix "reg|Windows 2000|Xhci.AddReg.NT|HKR||XhciLogDebugView|" -Verdict $case.Verdict `
            -What ("AddReg flags " + $case.Flags + ": " + $case.Why)
    }

    # A service is a key with values in it. The first draft emitted only
    # ServiceBinary and called the footprint complete. Assert the VALUES, not
    # just the directive names - an emitter that printed every value as empty
    # would satisfy a name-only check while the footprint said nothing.
    $fp = Get-Footprint -Path $prodInf
    foreach ($v in @(
        @{ Name = "DisplayName";    Value = "%XhciSvcDesc%" },
        @{ Name = "ServiceType";    Value = "1" },
        @{ Name = "StartType";      Value = "3" },
        @{ Name = "ErrorControl";   Value = "1" },
        @{ Name = "ServiceBinary";  Value = "%12%\xhci98.sys" },
        @{ Name = "LoadOrderGroup"; Value = "Base" }
    )) {
        $want = "servicevalue|Windows 2000|xhci98|Xhci.AddService|" + $v.Name + "|" + $v.Value
        Assert-True (@($fp | Where-Object { $_ -eq $want }).Count -eq 1) (
            "expected footprint row '$want'. Rows:`n" + ($fp -join "`n"))
    }

    # AddService's own flags field was dropped entirely by the first two drafts.
    Assert-True (@($fp | Where-Object { $_ -eq "service|Windows 2000|xhci98|Xhci.AddService|0x00000002|remove" }).Count -eq 1) (
        "the service row must carry the AddService flags field. Rows:`n" + ($fp -join "`n"))

    # Task 11-V.6's fix, asserted against the production INF - and inverted
    # twice now, which is worth saying because the direction is the whole
    # assertion. Until 1.0.1.0 this pinned the value's ABSENCE on the Windows
    # 2000 path, on the assumption that that target's native usbport never
    # idle-suspends this controller (an assumption, not a measurement; roadmap
    # Phase 20, F18). The Windows XP reading of 2026-09-03 (roadmap task 19.2:
    # usbport's SuspendController within thirty seconds, the hot-plugged mouse
    # invisible) made it an NT-path need and the pin became "two rows on every
    # target". Since 1.1.0.0 the fix is not a registry value at all - the
    # driver declares USB_MINIPORT_FLAGS_DISABLE_SS (0x20) - so the pin is
    # ABSENCE again, and this time on every path of both files.
    #
    # The footprint is the right place for it. It is derived from the INF's
    # own AddReg rows rather than from a rule's opinion of them, so a row
    # reappearing under any section name, on any route, with any data, shows
    # up here. The gate's SUSP-* rules say the same thing about a mutated INF;
    # this is the production file.
    $suspRows = @($fp | Where-Object { $_ -match "SelectiveSuspend" })
    Assert-True ($suspRows.Count -eq 0) (
        "src\xhci98.inf's footprint still carries a selective-suspend registry row. Since 1.1.0.0 this package writes none - the driver declares USB_MINIPORT_FLAGS_DISABLE_SS (0x20) instead, and the machine-wide value it used to write reached every controller usbport drives and outlived the devnode. Rows:`n" + ($suspRows -join "`n"))

    # The AddService flags field. Three cases are ways the service stops being
    # this package's to claim; two are cases that LOOK like one and are not -
    # ownership is what this column answers, and drafts twice answered a
    # different question in it.
    foreach ($case in @(
        @{ Flags = "0x0000000A"; Verdict = "review"; Why = "SPSVCINST_NOCLOBBER_DISPLAYNAME makes a service-key value conditional" },
        @{ Flags = "0x00000202"; Verdict = "review"; Why = "SPSVCINST_STOPSERVICE is a DelService flag and means nothing this derivation can act on" },
        @{ Flags = "0x00001002"; Verdict = "review"; Why = "an SPSVCINST bit this emitter has not been taught is reported" },
        @{ Flags = "0x00000001"; Verdict = "remove"; Why = "SPSVCINST_ASSOCSERVICE marks the function driver; the header states no deletion rule, and any bearing it has is on uninstall rather than on what this install wrote - a draft made a missing one 'review' and thereby answered a different question in an ownership column" },
        @{ Flags = "0";          Verdict = "remove"; Why = "no flags at all: an unconditional AddService naming a section that exists wrote the service" }
    )) {
        $inf2 = New-MutatedInf -Name ("fp-svcflag-" + $case.Flags) -Mutate {
            param($t) $t.Replace("AddService=xhci98,0x00000002,Xhci.AddService",
                                 ("AddService=xhci98," + $case.Flags + ",Xhci.AddService"))
        }.GetNewClosure()
        $fp = Get-Footprint -Path $inf2
        Assert-RowVerdict -Rows $fp -Prefix "service|Windows 2000|xhci98|Xhci.AddService|" -Verdict $case.Verdict `
            -What ("AddService flags " + $case.Flags + ": " + $case.Why)
    }

    # An AddService pointing at a section that does not exist is a gap in this
    # derivation, and the emitter runs before the verdict - so the gate's own
    # cross-reference failure is not visible in this file and the row has to say
    # so itself.
    $svcMissingInf = New-MutatedInf -Name "fp-svc-missing" -Mutate {
        param($t) $t.Replace("AddService=xhci98,0x00000002,Xhci.AddService",
                             "AddService=xhci98,0x00000002,Xhci.NoSuchSection")
    }
    $fp = Get-Footprint -Path $svcMissingInf
    Assert-True (@($fp | Where-Object { $_ -like "unmodelled|Windows 2000|Xhci.Dev.NTx86.Services|AddService xhci98 -> missing section*" }).Count -eq 1) (
        "an AddService naming a missing section must appear as a gap. Rows:`n" + ($fp -join "`n"))
    Assert-RowVerdict -Rows $fp -Prefix "service|Windows 2000|xhci98|Xhci.NoSuchSection|" -Verdict "review" `
        -What "a service whose install section is missing cannot be called removable"

    # ...and the .Services section is scanned for gaps too, which the first
    # draft did only for the install section.
    $svcExtraInf = New-MutatedInf -Name "fp-svc-extra" -Mutate {
        param($t) $t.Replace("AddService=xhci98,0x00000002,Xhci.AddService",
                             "AddService=xhci98,0x00000002,Xhci.AddService`r`nDelService=xhci97")
    }
    $fp = Get-Footprint -Path $svcExtraInf
    Assert-True (@($fp | Where-Object { $_ -eq "unmodelled|Windows 2000|Xhci.Dev.NTx86.Services|DelService" }).Count -eq 1) (
        "a directive beside AddService must appear as a gap. Rows:`n" + ($fp -join "`n"))

    # A flags field too large for a signed 32-bit integer is a `review` row, not
    # a terminated run: the emitter's stated policy is that an unreadable field
    # is reported, and $ErrorActionPreference = "Stop" would otherwise turn the
    # cast into an abort with no file written at all.
    $hugeFlagInf = New-MutatedInf -Name "fp-huge-flags" -Mutate {
        param($t) $t.Replace("[Xhci.CopyW98]`r`nusbd.sys,,,16", "[Xhci.CopyW98]`r`nusbd.sys,,,4294967296")
    }
    $fp = Get-Footprint -Path $hugeFlagInf
    Assert-True ($null -ne $fp) "an out-of-range copy-flags field aborted the emit instead of producing a row."
    Assert-RowVerdict -Rows $fp -Prefix "file|Windows 98|Xhci.CopyW98|10|System32\Drivers|usbd.sys|" -Verdict "review" `
        -What "an out-of-range copy-flags field must read 'review'"

    # A registry root that outlives the devnode is still a value this install
    # WROTE. The root is emitted; what removing the device does to a key outside
    # HKR is uninstall behaviour, and a draft put that in the ownership column -
    # the same error the service verdict made one round earlier.
    $hklmInf = New-MutatedInf -Name "fp-hklm" -Mutate {
        param($t) $t.Replace("HKR,,DevLoader,,*NTKERN",
                             "HKR,,DevLoader,,*NTKERN`r`nHKLM,Software\xhci98,Installed,0x00010001,1")
    }
    $fp = Get-Footprint -Path $hklmInf
    # Asserted on the INJECTED row by name rather than by counting HKLM rows:
    # the production INF has carried a real one since task 11-V.6's fix
    # (Services\USB\DisableSelectiveSuspend), so "exactly one" would now be
    # measuring the production file rather than the mutation. Naming the row is
    # the stricter form and does not drift when production gains another.
    Assert-True (@($fp | Where-Object { $_ -eq "reg|Windows 98|Xhci.AddReg|HKLM|Software\xhci98|Installed|0x00010001|1|remove" }).Count -eq 1) (
        "an unconditional non-HKR AddReg row wrote its value and must read 'remove'; its lifetime is a run measurement, not an ownership verdict. Rows:`n" + ($fp -join "`n"))

    # A directive the emitter does not translate must appear as a gap rather
    # than be dropped. This is the failure mode a hand-written manifest has.
    $delregInf = New-MutatedInf -Name "fp-delreg" -Mutate {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg",
                             "[Xhci.Dev]`r`nDelReg=Xhci.AddReg`r`nAddReg=Xhci.AddReg")
    }
    $fp = Get-Footprint -Path $delregInf
    Assert-True (@($fp | Where-Object { $_ -eq "unmodelled|Windows 98|Xhci.Dev|DelReg" }).Count -eq 1) (
        "an unmodelled install directive must be emitted as a gap. Rows:`n" + ($fp -join "`n"))

    # CopyFiles=@file has no section and no flags field. It is legal INF and
    # this file does not use it, so without a case here that branch would be
    # untested code in a document the uninstall clause is checked against.
    $atFileInf = New-MutatedInf -Name "fp-atfile" -Mutate {
        param($t) $t.Replace("CopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98",
                             "CopyFiles=@xhci98.inf,Xhci.CopyFiles,Xhci.CopyW98")
    }
    $fp = Get-Footprint -Path $atFileInf
    # The dirid column is the RESOLVED DefaultDestDir (10 in this INF), not the
    # literal word - a first draft emitted the name of the question.
    Assert-True (@($fp | Where-Object { $_ -eq "file|Windows 98|@|10||xhci98.inf|xhci98.inf||0|remove" }).Count -eq 1) (
        "CopyFiles=@file produced no footprint row, or did not resolve DefaultDestDir. Rows:`n" + ($fp -join "`n"))

    Write-Step "staged package layout"
    $emptyPkg = Join-Path $script:work "pkg-empty"
    New-Item -ItemType Directory -Path $emptyPkg | Out-Null
    $r = Invoke-Gate -Path $prodInf -PackageDir $emptyPkg
    Assert-True ($r.ExitCode -ne 0) "an empty -PackageDir was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-LAYOUT]")) ("expected PKG-LAYOUT to fire on an empty package. Output:`n" + $r.Output)

    # Since 1.0.0.1 a package is this project's two files and nothing else, so
    # the package cases are presence and the absence of any Microsoft file. The
    # stand-ins are text files: what is being tested is the gate's arithmetic,
    # not any binary's contents.
    function New-StandInPackage {
        param([string]$Name, [string[]]$Extra = @())
        $dir = Join-Path $script:work $Name
        New-Item -ItemType Directory -Path $dir | Out-Null
        Copy-Item -LiteralPath $prodInf -Destination (Join-Path $dir "xhci98.inf")
        Set-Content -LiteralPath (Join-Path $dir "xhci98.sys") -Value "not a real driver" -Encoding ASCII
        foreach ($e in $Extra) {
            Set-Content -LiteralPath (Join-Path $dir $e) -Value "stand-in $e" -Encoding ASCII
        }
        return $dir
    }

    $goodPkg = New-StandInPackage -Name "pkg-good"
    $r = Invoke-Gate -Path $prodInf -PackageDir $goodPkg
    Assert-True ($r.ExitCode -eq 0) ("a complete -PackageDir was rejected:`n" + $r.Output)

    $partialPkg = Join-Path $script:work "pkg-partial"
    New-Item -ItemType Directory -Path $partialPkg | Out-Null
    Copy-Item -LiteralPath $prodInf -Destination (Join-Path $partialPkg "xhci98.inf")
    $r = Invoke-Gate -Path $prodInf -PackageDir $partialPkg
    Assert-True ($r.ExitCode -ne 0) "a package missing xhci98.sys was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-LAYOUT]")) ("expected PKG-LAYOUT to fire on a package missing xhci98.sys. Output:`n" + $r.Output)

    # A Microsoft file back in the package: under the 1.0.0.0 media name, under
    # its own name, and in a subdirectory. Each is the exception
    # legal-provenance section 5 withdrew, arriving by drift.
    foreach ($case in @(
        @{ Name = "pkg-usbd98";  Extra = @("usbd98.sys") },
        @{ Name = "pkg-usbd";    Extra = @("usbd.sys") },
        @{ Name = "pkg-usbhub";  Extra = @("usbhub.sys") }
    )) {
        $pkg = New-StandInPackage -Name $case.Name -Extra $case.Extra
        $r = Invoke-Gate -Path $prodInf -PackageDir $pkg
        Assert-True ($r.ExitCode -ne 0) ("a package holding " + ($case.Extra -join ", ") + " was accepted.")
        Assert-True ($r.Output -match [regex]::Escape("[PKG-MSFILE]")) ("expected PKG-MSFILE to fire on " + $case.Name + ". Output:`n" + $r.Output)
    }
    $subPkg = New-StandInPackage -Name "pkg-subdir-msfile"
    New-Item -ItemType Directory -Path (Join-Path $subPkg "usbfiles") | Out-Null
    Set-Content -LiteralPath (Join-Path $subPkg "usbfiles\usbd2k.sys") -Value "stand-in" -Encoding ASCII
    $r = Invoke-Gate -Path $prodInf -PackageDir $subPkg
    Assert-True ($r.ExitCode -ne 0) "a package holding usbfiles\usbd2k.sys was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-MSFILE]")) ("expected PKG-MSFILE to fire on a subdirectory copy. Output:`n" + $r.Output)


    # ---- the 64-bit package's INF (roadmap task 21.3) -------------------
    #
    # src\xhci98-amd64.inf is a SECOND production INF, not a variant of the
    # first, and design record 11's decision 2 accepted one cost for it: two
    # files now carry one package's facts. Everything below is that cost being
    # paid rather than promised - the file passing its own profile, each rule
    # of that profile shown firing, each file shown FAILING under the other's
    # profile, and the shared facts compared directly between the two.

    Write-Step "the 64-bit INF must pass its own profile"

    $baseline64 = Invoke-Gate -Path $prodInfAmd64 -Extra @("-Arch", "amd64")
    Assert-True ($baseline64.ExitCode -eq 0) ("src\xhci98-amd64.inf does not pass its own gate:`n" + $baseline64.Output)
    Assert-True ($baseline64.Output -notmatch "FAIL \[") "src\xhci98-amd64.inf produced a FAIL line."
    Assert-True ($baseline64.Output -notmatch "WARN:") ("src\xhci98-amd64.inf produced a warning:`n" + $baseline64.Output)
    #
    # **Both models sections must be READ, not merely present.** Until
    # 2026-09-16 the gate took one models section per [Manufacturer] line, so a
    # staged INF carrying the NT 6.x section passed with that section - and the
    # whole install path behind it - never checked: "models: 1" over a file with
    # two. Passing says nothing about a path the rules did not walk.
    #
    Assert-True ($baseline64.Output -match "models: 2\b") ("src\xhci98-amd64.inf: expected the gate to gather two models (NT 5.2 and NT 6.x). Output:`n" + $baseline64.Output)

    #
    # **Each file must be REFUSED under the other's profile**, and this is the
    # check that makes -Arch worth having at all. Without it the amd64 profile
    # could be a set of rules that happens to accept anything the x86 one
    # accepts - "coverage" that never distinguishes the two files - and the
    # first sign would be a 64-bit package gated as though it were the 32-bit
    # one. The interesting half is the second: src\xhci98.inf under -Arch
    # amd64 must fail, because its [Manufacturer] line carries no NTamd64 field
    # and its undecorated [Xhci.Dev] is exactly what PATH-NO9X exists to refuse.
    #
    $wrongProfile = Invoke-Gate -Path $prodInfAmd64
    Assert-True ($wrongProfile.ExitCode -ne 0) ("the 64-bit INF passed the x86 profile. The two profiles do not distinguish the two files, so gating either says nothing about it.`n" + $wrongProfile.Output)
    Assert-True ($wrongProfile.Output -match [regex]::Escape("FAIL [PATH-MFGDEC]")) ("expected PATH-MFGDEC on the 64-bit INF under -Arch x86. Output:`n" + $wrongProfile.Output)

    #
    # The 32-bit file is refused at its [Manufacturer] line, which is the FIRST
    # thing wrong with it under this profile and the reason PATH-NO9X is not
    # what to assert here: with no NTamd64 field there is no [XhciModels.NTamd64]
    # to find, so no model is gathered, so the per-model loop PATH-NO9X lives in
    # never runs. That rule's own case is `amd64-undecorated-dev` below, where
    # the file is a valid 64-bit INF in every other respect. Asserting it here
    # would have been asserting a rule that cannot reach this input.
    #
    $wrongProfile2 = Invoke-Gate -Path $prodInf -Extra @("-Arch", "amd64")
    Assert-True ($wrongProfile2.ExitCode -ne 0) ("the 32-bit INF passed the amd64 profile.`n" + $wrongProfile2.Output)
    Assert-True ($wrongProfile2.Output -match [regex]::Escape("FAIL [PATH-MFGDEC]")) ("expected PATH-MFGDEC on the 32-bit INF under -Arch amd64. Output:`n" + $wrongProfile2.Output)
    Assert-True ($wrongProfile2.Output -match [regex]::Escape("FAIL [OS-DEFAULT]")) ("expected OS-DEFAULT to refuse the 32-bit file's undecorated [DefaultInstall] under -Arch amd64. Output:`n" + $wrongProfile2.Output)

    Write-Step "the [Manufacturer] decoration, in both directions"

    #
    # **This is the rule that pins design record 11's decision 2**, and these
    # are the cases whose subject is the 32-bit file. Widening its line towards
    # `NTx86,NTamd64` is the single-INF route the owner declined on 2026-09-09,
    # because that line is what Windows 98's 16-bit engine parses to find its
    # models section. The ONE field it carries, `NTx86.6.0`, was read on all four
    # engines that parse this file before it was taken (2026-09-16, roadmap
    # task 22.5); anything beyond it is unread there. Nothing else in the tree
    # would notice the edit: the widened file still passes every other rule.
    #
    Assert-RuleFires "x86-mfg-widened" "PATH-MFGDEC" {
        param($t) $t.Replace("%Mfg%=XhciModels,NTx86.6.0`r`n", "%Mfg%=XhciModels,NTx86.6.0,NTx86,NTamd64`r`n")
    }
    # The measured field lost: Vista and Windows 7 x86 then match the
    # undecorated models section and abort in the NT 5.x path's LayoutFile
    # copies, and every other rule still passes.
    Assert-RuleFires "x86-mfg-no-nt6" "PATH-MFGDEC" {
        param($t) $t.Replace("%Mfg%=XhciModels,NTx86.6.0`r`n", "%Mfg%=XhciModels`r`n")
    }
    Assert-RuleFires "x86-nt6-models-missing" "BOTH-XREF" {
        param($t) $t.Replace("[XhciModels.NTx86.6.0]", "[XhciModels.NTx86.6.1]")
    }
    # The 32-bit NT 6.x install path, checked the way the 64-bit one is below:
    # a second path is a second place each rule can break.
    Assert-RuleFires "x86-nt6-no-services" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev6.NTx86.Services]", "[Xhci.Dev6.NTx86.Svc]")
    }
    Assert-RuleFires "x86-nt6-no-driver-copy" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`n")
    }
    Assert-RuleFires "x86-nt6-copies-usbport" "OS-ONNT6" {
        param($t) $t.Replace("[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT`r`n")
    }
    Assert-RuleFires "x86-nt6-copies-w98-list" "OS-ONNT6" {
        param($t) $t.Replace("[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98`r`n")
    }
    # There is no NT 6.x SUSP case here any more, and its absence is the point.
    # Until 1.0.2.0 "x86-nt6-no-susp" checked that [Xhci.Dev6.NTx86] delivered
    # the machine-wide value, because the rules walked install routes and each
    # route needed its own case. The inverted rules are whole-file: they ask
    # whether the value appears at all, so a per-path case would exercise the
    # same line of the gate as "susp-global-nt" above and prove nothing extra.
    # The NT 6.x path keeps its own VAL-* case below, which is still per-route.
    Assert-RuleFires "x86-nt6-no-logvalues" "VAL-MISSING" {
        param($t) $t.Replace("[Xhci.Dev6.NTx86]`r`nAddReg=Xhci.AddReg.NT",
                             "[Xhci.Dev6.NTx86]")
    }
    # The other direction: the 64-bit file losing its decoration. The 64-bit
    # setup engine then looks for an undecorated [XhciModels], ignores it, and
    # the package installs nothing at all - which on the target is
    # indistinguishable from media that was never copied.
    Assert-RuleFires "amd64-mfg-undecorated" "PATH-MFGDEC" {
        param($t) $t.Replace("%Mfg%=XhciModels,NTamd64", "%Mfg%=XhciModels")
    } -Source $prodInfAmd64 -Arch amd64

    # The NT 6.x field lost. Nothing else breaks: [XhciModels.NTamd64.6.0] is
    # still there, unreferenced, and every NT 5.2 rule passes - while Vista and
    # Windows 7 x64 match the NT 5.2 models section and abort in its LayoutFile
    # copies (roadmap task 21.8).
    Assert-RuleFires "amd64-mfg-no-nt6" "PATH-MFGDEC" {
        param($t) $t.Replace("%Mfg%=XhciModels,NTamd64,NTamd64.6.0", "%Mfg%=XhciModels,NTamd64")
    } -Source $prodInfAmd64 -Arch amd64
    # A field the profile has no path for, with its models section present, so
    # the only thing wrong is that no rule would ever read that section.
    Assert-RuleFires "amd64-mfg-unknown" "PATH-MFGDEC" {
        param($t) $t.Replace("%Mfg%=XhciModels,NTamd64,NTamd64.6.0", "%Mfg%=XhciModels,NTamd64,NTamd64.6.0,NTamd64.6.1").Replace(
                             "[XhciModels.NTamd64.6.0]`r`n", "[XhciModels.NTamd64.6.1]`r`n%XhciDesc%=Xhci.Dev6,PCI\CC_0C0330`r`n`r`n[XhciModels.NTamd64.6.0]`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-nt6-models-missing" "BOTH-XREF" {
        param($t) $t.Replace("[XhciModels.NTamd64.6.0]", "[XhciModels.NTamd64.6.1]")
    } -Source $prodInfAmd64 -Arch amd64

    # Decision 11 (design record 11 section 12): the 28-character section-name
    # limit is Windows 98's, and Windows 98's engine never reads this file, so
    # under -Arch amd64 W98-SECTLEN does not run. Raised by a real refusal - a
    # staged [Xhci.Dev.NTamd64.6.0.Services] is 29 characters. The mutation
    # renames one section at its header and at every reference, to 31
    # characters, so length is the only thing that differs from the production
    # file; the x86 "sectlen" case above is what proves the rule still fires
    # where it applies. Both halves are asserted: the rule's own tag must be
    # absent, and the file must pass outright.
    $long64 = New-MutatedInf -Name "amd64-sectlen-ok" -Mutate {
        param($t) $t.Replace("Xhci.AddReg.NT", "Xhci.AddReg.NTThirtyOneCharsXXX")
    } -Source $prodInfAmd64
    $r = Invoke-Gate -Path $long64 -Extra @("-Arch", "amd64")
    Assert-True ($r.Output -notmatch [regex]::Escape("[W98-SECTLEN]")) ("amd64-sectlen-ok : W98-SECTLEN fired on the 64-bit file, which Windows 98's engine never reads. Output was:`n" + $r.Output)
    Assert-True ($r.ExitCode -eq 0) ("amd64-sectlen-ok : the gate refused a 64-bit INF whose only change is a 31-character section name. Output was:`n" + $r.Output)

    Write-Step "the 64-bit file's own install path"

    #
    # PATH-NO9X: an undecorated install section ADDED beside the decorated one,
    # which is what an author merging the two files by hand would produce. It
    # is not a missing section - everything the 64-bit engine needs is still
    # there and the file installs correctly on x64. What it also does is offer
    # an amd64 xhci98.sys to a 32-bit engine, which falls back to exactly this
    # section, copies the binary, and creates a service pointing at it.
    #
    Assert-RuleFires "amd64-undecorated-dev" "PATH-NO9X" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]`r`nAddReg=",
                             "[Xhci.Dev]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[Xhci.Dev.NTamd64]`r`nAddReg=")
    } -Source $prodInfAmd64 -Arch amd64

    #
    # **And the two DECORATED spellings a 32-bit engine reaches BEFORE the
    # undecorated one** (the 2026-09-16 audit's D7). The fallback chain is
    # .NTx86, then .NT, then the bare name, so `[Xhci.Dev.NTx86]` in the 64-bit
    # file is not a fallback at all - it is the section that engine was looking
    # for. Both were accepted while only the bare name was refused, which made
    # the rule read as "do not rely on the fallback" when what it means is "a
    # 32-bit engine must find nothing here".
    #
    Assert-RuleFires "amd64-ntx86-dev" "PATH-NO9X" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]`r`nAddReg=",
                             "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[Xhci.Dev.NTamd64]`r`nAddReg=")
    } -Source $prodInfAmd64 -Arch amd64

    Assert-RuleFires "amd64-nt-dev" "PATH-NO9X" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]`r`nAddReg=",
                             "[Xhci.Dev.NT]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[Xhci.Dev.NTamd64]`r`nAddReg=")
    } -Source $prodInfAmd64 -Arch amd64

    # The same hazard on the right-click route, which is the one a user takes
    # with no device present - so nothing about the hardware stops it.
    Assert-RuleFires "amd64-undecorated-default" "OS-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTamd64]`r`nCopyFiles=",
                             "[DefaultInstall]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[DefaultInstall.NTamd64]`r`nCopyFiles=")
    } -Source $prodInfAmd64 -Arch amd64

    # ...and its two decorated siblings, for the reason the device-install pair
    # above gives.
    Assert-RuleFires "amd64-ntx86-default" "OS-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTamd64]`r`nCopyFiles=",
                             "[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[DefaultInstall.NTamd64]`r`nCopyFiles=")
    } -Source $prodInfAmd64 -Arch amd64

    Assert-RuleFires "amd64-nt-default" "OS-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTamd64]`r`nCopyFiles=",
                             "[DefaultInstall.NT]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI`r`n`r`n[DefaultInstall.NTamd64]`r`nCopyFiles=")
    } -Source $prodInfAmd64 -Arch amd64

    # And the right-click section going missing altogether, which halves the
    # routes every OS-* and SUSP-* rule below is checked against rather than
    # failing anything - the 2026-09-07 audit's H8, in its 64-bit form.
    Assert-RuleFires "amd64-no-default" "OS-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTamd64]", "[DefaultInstall.NTamd65]")
    } -Source $prodInfAmd64 -Arch amd64

    Assert-RuleFires "amd64-no-services" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64.Services]", "[Xhci.Dev.NTamd64.Svc]")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-no-install-section" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]", "[Xhci.Dev.NT]")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-svc-binary-gap" "PATH-NT" {
        param($t) $t.Replace("ServiceBinary=%12%\xhci98.sys", "ServiceBinary=%12%\xhci99.sys")
    } -Source $prodInfAmd64 -Arch amd64
    # The NT 6.x install path gets the same checks, because a second path is a
    # second place each of them can break.
    Assert-RuleFires "amd64-nt6-undecorated-dev" "PATH-NO9X" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`nAddReg=",
                             "[Xhci.Dev6]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n`r`n[Xhci.Dev6.NTamd64]`r`nAddReg=")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-nt6-no-services" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64.Services]", "[Xhci.Dev6.NTamd64.Svc]")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-nt6-no-install-section" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`n", "[Xhci.Dev6.NTamd65]`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-nt6-no-driver-copy" "PATH-NT" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-bad-starttype" "PATH-NT" {
        param($t) $t.Replace("StartType=3                         ; SERVICE_DEMAND_START", "StartType=4                         ; SERVICE_DISABLED")
    } -Source $prodInfAmd64 -Arch amd64

    Write-Step "the 64-bit file's OS-supplied files and registry values"

    # Every one of these is silent on the target in exactly the way its 32-bit
    # counterpart is: a root hub that will not load, a property page that is
    # dropped without a word, a controller that idle-suspends and stops seeing
    # hot-plugs, or a log channel that cannot be turned on.
    Assert-RuleFires "amd64-no-usbui" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT,Xhci.CopyUI",
                             "[Xhci.Dev.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-no-usbport" "OS-MISSING" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16`r`n", "[Xhci.CopyNT]`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-usbport-no-flag" "OS-FLAGS" {
        param($t) $t.Replace("usbport.sys,,,16", "usbport.sys")
    } -Source $prodInfAmd64 -Arch amd64
    # usbui.dll to the drivers directory: it is a user-mode property-page DLL,
    # and dirid 11 is where all four operating systems' own USB INFs put it.
    Assert-RuleFires "amd64-usbui-dest" "OS-DEST" {
        param($t) $t.Replace("Xhci.CopyUI=11", "Xhci.CopyUI=10,System32\Drivers")
    } -Source $prodInfAmd64 -Arch amd64
    # The media carrying a Microsoft file again, on the 64-bit package this
    # time: legal-provenance section 5's withdrawal is not per-architecture.
    Assert-RuleFires "amd64-usbport-on-media" "OS-MEDIA" {
        param($t) $t.Replace("[SourceDisksFiles]`r`nxhci98.sys=1", "[SourceDisksFiles]`r`nusbport.sys=1`r`nxhci98.sys=1")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-usbhub20" "OS-NEVER" {
        param($t) $t.Replace("[Xhci.CopyNT]`r`nusbport.sys,,,16", "[Xhci.CopyNT]`r`nusbhub20.sys,,,16`r`nusbport.sys,,,16")
    } -Source $prodInfAmd64 -Arch amd64
    # The 64-bit file must refuse the same two spellings. Both files carry the
    # rules, but the accepted cost of two INFs is that they can drift, and this
    # is the drift that would matter: the 64-bit half is the one whose targets
    # (Vista x64, Windows 7 x64) were never read without the value at all.
    Assert-RuleFires "amd64-susp-global" "SUSP-GLOBAL" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`n$suspRow`r`nHKR,,XhciLogVerbosity")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-susp-hc" "SUSP-HCVALUE" {
        param($t) $t.Replace("[Xhci.AddReg.NT]`r`nHKR,,XhciLogVerbosity",
                             "[Xhci.AddReg.NT]`r`nHKR,,HcDisableSelectiveSuspend,0x00010001,1`r`nHKR,,XhciLogVerbosity")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-no-logvalues" "VAL-MISSING" {
        param($t) $t.Replace("[Xhci.Dev.NTamd64]`r`nAddReg=Xhci.AddReg.NT",
                             "[Xhci.Dev.NTamd64]")
    } -Source $prodInfAmd64 -Arch amd64
    #
    # **OS-ONNT6: the NT 6.x path naming an OS-supplied file.** This is the
    # shape Vista's file queue aborts on (task 21.8), and it is the obvious
    # "tidy-up" - making the two install paths copy the same list - so it is
    # refused for the driver list and for usbui.dll separately, since they are
    # two sections an editor could add one at a time.
    #
    Assert-RuleFires "amd64-nt6-copies-usbport" "OS-ONNT6" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-nt6-copies-usbui" "OS-ONNT6" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles`r`n",
                             "[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyUI`r`n")
    } -Source $prodInfAmd64 -Arch amd64
    # No NT 6.x SUSP case, for the reason the 32-bit half gives: the inverted
    # rules are whole-file and a per-path case would re-test one line.
    Assert-RuleFires "amd64-nt6-no-logvalues" "VAL-MISSING" {
        param($t) $t.Replace("[Xhci.Dev6.NTamd64]`r`nAddReg=Xhci.AddReg.NT",
                             "[Xhci.Dev6.NTamd64]")
    } -Source $prodInfAmd64 -Arch amd64
    Assert-RuleFires "amd64-logverbosity-default" "VAL-DEFAULT" {
        param($t) $t.Replace("HKR,,XhciLogVerbosity,0x00010001,0", "HKR,,XhciLogVerbosity,0x00010001,1")
    } -Source $prodInfAmd64 -Arch amd64
    #
    # **The version tie reaches the 64-bit file too**, and proving that needs
    # the header staged beside the mutated copy: the cross-check skips silently
    # when there is no xhci_version.h next to the INF, which is correct for the
    # packager's staged media and would make this case pass vacuously. So the
    # good case asserts the check RAN, exactly as the 32-bit block above does,
    # and only then is the drift case worth anything. Both packages are cut
    # from one header at one version; a 64-bit INF claiming a version nobody
    # built would install as an upgrade and report it for ever afterwards.
    #
    {
        $stagedHdr64 = Join-Path $script:work "xhci_version.h"
        $stagedRc64  = Join-Path $script:work "xhci98.rc"
        $ascii64 = New-Object System.Text.ASCIIEncoding
        [System.IO.File]::WriteAllText($stagedHdr64, [System.IO.File]::ReadAllText((Join-Path $repo "src\xhci_version.h")), $ascii64)
        [System.IO.File]::WriteAllText($stagedRc64,  [System.IO.File]::ReadAllText((Join-Path $repo "src\xhci98.rc")), $ascii64)

        $good64 = New-MutatedInf -Name "amd64-vergood" -Mutate { param($t) $t } -InfUnchanged -Source $prodInfAmd64
        $r = Invoke-Gate -Path $good64 -Extra @("-Arch", "amd64")
        Assert-True ($r.ExitCode -eq 0) ("amd64-vergood : the 64-bit INF and src\xhci_version.h disagree:`n" + $r.Output)
        Assert-True (-not ($r.Output -match "cross-check skipped")) `
            ("amd64-vergood : the version cross-check SKIPPED, so the drift case below would pass vacuously. Output was:`n" + $r.Output)

        Assert-RuleFires "amd64-driverver-drift" "BOTH-VERSION" {
            param($t) $t -replace '(?m)^(DriverVer=\d{2}/\d{2}/\d{4}),1\.1\.0\.0', '$1,1.1.0.1'
        } -Source $prodInfAmd64 -Arch amd64

        Remove-Item -LiteralPath $stagedHdr64 -Force
        Remove-Item -LiteralPath $stagedRc64 -Force
    }.Invoke() | Out-Null

    Write-Step "the two INFs must agree about the package"

    #
    # **The accepted cost of decision 2, mechanised.** Two files carry one
    # package's facts, and the failure mode of that arrangement is not a
    # crash - it is a 64-bit package that installs perfectly and behaves
    # differently from the 32-bit one for a release or two before anybody
    # notices. Nothing else in this tree compares them: each passes its own
    # gate, each stages its own media, and the two are never read together.
    #
    # What is compared is what MUST be the same because it is one package:
    # the hardware ID it binds to, the service it creates and every value of
    # that service, the registry values the driver reads and their defaults,
    # the machine-wide value, the OS-supplied file list and its flags, the
    # media contents, the [Version] identity, and every [Strings] token the
    # two share. What is deliberately NOT compared is what must differ - the
    # decorations, the Windows 98 half, the temporary-name field, the INF
    # copy - and that list is short enough to state, which is the argument
    # for comparing the rest exactly rather than approximately.
    #
    function Get-InfSection {
        # Section body with comments, blank lines and trailing whitespace
        # removed, so a comment edit in one file is not a false disagreement.
        param([string]$Path, [string]$Name)
        $lines = [System.IO.File]::ReadAllText($Path) -split "`r`n"
        $out = New-Object System.Collections.ArrayList
        $inSection = $false
        foreach ($raw in $lines) {
            $line = $raw
            $semi = $line.IndexOf(';')
            if ($semi -ge 0) { $line = $line.Substring(0, $semi) }
            $line = $line.Trim()
            if ($line -match '^\[(.+)\]$') {
                $inSection = ($matches[1] -ieq $Name)
                continue
            }
            if ($inSection -and $line -ne "") { [void]$out.Add($line) }
        }
        return @($out)
    }

    function Assert-InfsAgree {
        param([string]$Section, [string]$Why, [string[]]$Only = @())
        $a = @(Get-InfSection -Path $prodInf -Name $Section)
        $b = @(Get-InfSection -Path $prodInfAmd64 -Name $Section)
        if ($Only.Count -gt 0) {
            $keep = { param($rows) @($rows | Where-Object { $r = $_; @($Only | Where-Object { $r -imatch ('^\s*' + [regex]::Escape($_) + '\s*=') }).Count -gt 0 }) }
            $a = & $keep $a
            $b = & $keep $b
        }
        Assert-True ($a.Count -gt 0) ("INF-SYNC: [$Section] is empty or missing in src\xhci98.inf, so this comparison proves nothing.")
        Assert-True ((($a -join "`n")) -eq (($b -join "`n"))) (
            "INF-SYNC: src\xhci98.inf and src\xhci98-amd64.inf disagree in [$Section]." +
            "`n$Why" +
            "`nIf the change really belongs in one file only, say so here and exempt it - do not" +
            "`nedit one file and leave the other behind." +
            "`n`n--- src\xhci98.inf ---`n" + ($a -join "`n") +
            "`n`n--- src\xhci98-amd64.inf ---`n" + ($b -join "`n"))
    }

    Assert-InfsAgree -Section "Version" -Only @("Signature", "Class", "ClassGUID", "Provider", "LayoutFile", "DriverVer") `
        -Why "The two packages are one release cut from one src\xhci_version.h, and they install into the same device class through the same LayoutFile route."
    Assert-InfsAgree -Section "Xhci.AddService" `
        -Why "One package creates one service. A driver that is demand-start on one architecture and boot-start on the other is two products."
    Assert-InfsAgree -Section "Xhci.AddReg.NT" `
        -Why "These are the values the driver reads at run time through usbport. A default that drifted on one architecture is a diagnostic door open on machines whose owner never asked for one."
    # [Xhci.AddReg.Global] was compared here until 1.1.0.0 removed it from both
    # files. What replaced it is not comparable this way: the flag lives in
    # src\xhci_dispatch.c, one definition both architectures compile, so the
    # two packages cannot disagree about it the way two INF sections could.
    # The footprint assertion above is what holds the value out of either file.
    Assert-InfsAgree -Section "Xhci.CopyNT" `
        -Why "The NT paths of both packages fetch the same three files from the OS by the same LayoutFile route with the same COPYFLG_NO_OVERWRITE."
    Assert-InfsAgree -Section "Xhci.CopyUI" `
        -Why "usbui.dll is on every install path of both packages since 1.0.2.0, to dirid 11, with flag 16."
    Assert-InfsAgree -Section "SourceDisksFiles" `
        -Why "Both media carry this project's two files and nothing else. A Microsoft file appearing on one of them is legal-provenance section 5's withdrawal being undone on one architecture."
    Assert-InfsAgree -Section "SourceDisksNames" `
        -Why "One disk, described the same way."

    # The hardware ID: the two models lines are decorated differently and sit in
    # differently named sections, so the sections cannot be compared whole. What
    # must match is what the engine binds on.
    $idX86 = @(Get-InfSection -Path $prodInf -Name "XhciModels")
    $id64  = @(Get-InfSection -Path $prodInfAmd64 -Name "XhciModels.NTamd64")
    Assert-True ($idX86.Count -eq 1 -and $id64.Count -eq 1) "INF-SYNC: each file must have exactly one models line."
    Assert-True ($idX86[0] -eq $id64[0]) (
        "INF-SYNC: the two INFs bind different hardware. One package, one compatible ID." +
        "`n  src\xhci98.inf        $($idX86[0])" +
        "`n  src\xhci98-amd64.inf  $($id64[0])")
    # The NT 6.x models line names a different install section on purpose
    # (Xhci.Dev6), so its hardware ID is compared on its own - and it must be
    # the same one, or Vista and Windows 7 x64 bind something no other target
    # does.
    $id64nt6 = @(Get-InfSection -Path $prodInfAmd64 -Name "XhciModels.NTamd64.6.0")
    Assert-True ($id64nt6.Count -eq 1) "INF-SYNC: src\xhci98-amd64.inf must have exactly one NT 6.x models line."
    if ($id64nt6.Count -eq 1 -and $idX86.Count -eq 1) {
        $hwX86 = (($idX86[0] -split '=', 2)[1] -split ',')[1].Trim()
        $hwNt6 = (($id64nt6[0] -split '=', 2)[1] -split ',')[1].Trim()
        Assert-True ($hwX86 -eq $hwNt6) (
            "INF-SYNC: the NT 6.x models line binds different hardware from every other target." +
            "`n  src\xhci98.inf                       $($idX86[0])" +
            "`n  src\xhci98-amd64.inf (NTamd64.6.0)   $($id64nt6[0])")
    }
    # And the two NT 6.x models lines agree with each other whole - the same
    # hardware ID and the same install section name - because they are one
    # path on two architectures.
    $idX86nt6 = @(Get-InfSection -Path $prodInf -Name "XhciModels.NTx86.6.0")
    Assert-True ($idX86nt6.Count -eq 1) "INF-SYNC: src\xhci98.inf must have exactly one NT 6.x models line."
    if ($idX86nt6.Count -eq 1 -and $id64nt6.Count -eq 1) {
        Assert-True ($idX86nt6[0] -eq $id64nt6[0]) (
            "INF-SYNC: the two INFs' NT 6.x models lines differ." +
            "`n  src\xhci98.inf        (NTx86.6.0)     $($idX86nt6[0])" +
            "`n  src\xhci98-amd64.inf  (NTamd64.6.0)   $($id64nt6[0])")
    }

    # [Strings]: every token the two share must have the same text, because
    # only one of the two packages can ever install on a machine and a user
    # reading Device Manager - or quoting it in a bug report - should see one
    # product either way. Tokens present in one file only are not a
    # disagreement; there are none today, and a 64-bit-only string would be.
    $sX86 = @{}
    foreach ($row in (Get-InfSection -Path $prodInf -Name "Strings")) {
        if ($row -match '^\s*([^=]+?)\s*=\s*(.+)$') { $sX86[$matches[1].Trim()] = $matches[2].Trim() }
    }
    $s64 = @{}
    foreach ($row in (Get-InfSection -Path $prodInfAmd64 -Name "Strings")) {
        if ($row -match '^\s*([^=]+?)\s*=\s*(.+)$') { $s64[$matches[1].Trim()] = $matches[2].Trim() }
    }
    Assert-True ($sX86.Count -gt 0 -and $s64.Count -gt 0) "INF-SYNC: [Strings] parsed empty in one of the two INFs."
    foreach ($tok in @($sX86.Keys | Sort-Object)) {
        if (-not $s64.ContainsKey($tok)) { continue }
        Assert-True ($sX86[$tok] -ceq $s64[$tok]) (
            "INF-SYNC: [Strings] token %$tok% differs between the two INFs." +
            "`n  src\xhci98.inf        $($sX86[$tok])" +
            "`n  src\xhci98-amd64.inf  $($s64[$tok])" +
            "`nOnly one of the two packages installs on a given machine; both should name one product.")
    }

    Write-Step "the 64-bit install footprint"

    #
    # A footprint file of its own, compared the same way and for the same
    # reason (task 11-V.3): the 64-bit package places a different set of things
    # on a different number of routes, so comparing it against the 32-bit file's
    # footprint would mean nothing. Two claims, two files.
    #
    $tracked64 = Join-Path $repo "scripts\inf-gate\expected-footprint-amd64.txt"
    Assert-True (Test-Path -LiteralPath $tracked64) "scripts\inf-gate\expected-footprint-amd64.txt is missing; regenerate it with -Arch amd64 -EmitFootprint."
    if (Test-Path -LiteralPath $tracked64) {
        $out64 = Join-Path $script:work ("fp64-" + [System.IO.Path]::GetRandomFileName() + ".txt")
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate `
            -InfPath $prodInfAmd64 -Arch amd64 -EmitFootprint $out64 | Out-Null
        Assert-True (Test-Path -LiteralPath $out64) "-EmitFootprint wrote no file for the 64-bit INF."
        if (Test-Path -LiteralPath $out64) {
            $actual64 = @(Get-Content -LiteralPath $out64 | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne "" })
            $expected64 = @(Get-Content -LiteralPath $tracked64 | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne "" })
            foreach ($row in $actual64) {
                $kind = ($row -split '\|')[0]
                Assert-True ($knownRowTypes -contains $kind) (
                    "64-bit footprint line is neither a comment nor a known row type: '$row'")
            }
            Assert-True ((($actual64 -join "`n")) -eq (($expected64 -join "`n"))) (
                "the 64-bit INF's footprint differs from scripts\inf-gate\expected-footprint-amd64.txt." +
                "`nIf the INF's file or registry footprint really changed, task 11-V.3's uninstall" +
                "`nexpectation changed with it - regenerate the file and say so in the commit:" +
                "`n  powershell -File scripts\inf-gate\check-inf.ps1 -Arch amd64 -EmitFootprint scripts\inf-gate\expected-footprint-amd64.txt" +
                "`n`n--- expected ---`n" + ($expected64 -join "`n") +
                "`n`n--- actual ---`n" + ($actual64 -join "`n"))
        }
    }

} finally {
    if (Test-Path -LiteralPath $script:work) {
        Remove-Item -LiteralPath $script:work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Err ("INF gate self-tests FAILED: {0} of {1} check(s)." -f $script:failures.Count, $script:checks)
    exit 1
}
Write-Ok ("INF gate self-tests passed ({0} checks)" -f $script:checks)
exit 0
