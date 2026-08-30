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

The per-target usbd.sys rules (TGT-*, PKG-*) get the same treatment, and they
need it most: every way of breaking that split - one media file shared by both
paths, one CopyFiles section shared by both, a missing source name, a lost
NO_OVERWRITE flag, a swapped pair on the media - installs a file called
usbd.sys either way and shows up only as usbhub20.sys failing to load.

Everything happens on copies under the host temporary directory. The package
tests use stand-in files and a stand-in manifest, so nothing here needs the
git-ignored tools\ staging: no build, no VM, and no Microsoft binaries.

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

function Invoke-Gate {
    param([string]$Path, [string]$PackageDir = "", [string]$SourceManifest = "",
          [string[]]$Extra = @())
    $psArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $gate, "-InfPath", $Path)
    if ($PackageDir -ne "") { $psArgs += @("-PackageDir", $PackageDir) }
    if ($SourceManifest -ne "") { $psArgs += @("-SourceManifest", $SourceManifest) }
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
    param([string]$Name, [scriptblock]$Mutate, [switch]$Utf16, [switch]$LfOnly, [switch]$Latin1, [switch]$BareCr,
          [switch]$InfUnchanged)
    $original = [System.IO.File]::ReadAllText($prodInf)
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
    if ($text -ceq $original -and -not ($Utf16 -or $LfOnly -or $BareCr -or $InfUnchanged)) {
        Assert-True $false ("$Name : the mutation left src\xhci98.inf unchanged, so whatever this case asserts, it asserts it about the production INF. Its pattern no longer matches - fix the pattern, not the gate.")
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
    param([string]$Name, [string]$Rule, [scriptblock]$Mutate, [switch]$Utf16, [switch]$LfOnly, [switch]$Latin1, [switch]$BareCr)
    $path = New-MutatedInf -Name $Name -Mutate $Mutate -Utf16:$Utf16 -LfOnly:$LfOnly -Latin1:$Latin1 -BareCr:$BareCr
    $r = Invoke-Gate -Path $path
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
        param($t) $t.Replace("usbd98.sys=1", "usbd98.sys=1,..")
    }
    Assert-RuleFires "source-rooted-subdir" "BOTH-SOURCE" {
        param($t) $t.Replace("usbd98.sys=1", "usbd98.sys=1,C:\usbfiles")
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
    Assert-RuleFires "logdbgview-no-9x" "VAL-MISSING" {
        param($t) $t.Replace("HKR,,XhciLogDebugView,0x00010001,0`r`n`r`n; This value lives",
                             "`r`n; This value lives")
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
    # A Win2000-only INF - the mistake this task exists to prevent - must fail
    # on the Win98 half rather than quietly install on one target.
    Assert-RuleFires "nt-only" "PATH-W98" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg,Xhci.AddReg.Global`r`nCopyFiles=Xhci.CopyFiles", "[Xhci.Dev]`r`nCopyFiles=Xhci.CopyFiles")
    }

    # ---- the per-target usbd.sys (roadmap Phase 3 task 7) ------------------
    #
    # Every mutation here is a way the two builds could be interchanged. None
    # of them produces a visible symptom on the target: the machine ends up
    # with a file called usbd.sys either way, and the wrong one shows up only
    # as usbhub20.sys failing to load with a 0xc0000034 that names usbhub20.sys
    # rather than usbd.sys (docs\contributing\lessons.md, "usbhub20.sys bugchecks Win2000").
    Write-Step "the per-target usbd.sys"
    Assert-RuleFires "tgt-no-usbd" "TGT-MISSING" {
        param($t)
        $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg,Xhci.AddReg.Global`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98",
                   "[Xhci.Dev]`r`nAddReg=Xhci.AddReg,Xhci.AddReg.Global`r`nCopyFiles=Xhci.CopyFiles").
           Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K",
                   "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles")
    }
    # One target loses it - the asymmetric case, which is worse than losing it
    # on both because one target keeps working and hides the packaging bug.
    Assert-RuleFires "tgt-nt-only-usbd" "TGT-MISSING" {
        param($t)
        $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K",
                   "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles")
    }
    # Both paths naming one media file: whichever build is staged, one target
    # gets the other's usbd.sys. This is the single-file INF this task exists
    # to prevent.
    Assert-RuleFires "tgt-samesrc" "TGT-SAMESRC" {
        param($t) $t.Replace("usbd.sys,usbd2k.sys,,16", "usbd.sys,usbd98.sys,,16")
    }
    Assert-RuleFires "tgt-sharedsec" "TGT-SHAREDSEC" {
        param($t)
        $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K",
                   "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW98")
    }
    # No source-name field: the media can then only carry one build at all.
    Assert-RuleFires "tgt-medianame" "TGT-MEDIANAME" {
        param($t) $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,,,16")
    }
    Assert-RuleFires "tgt-dup" "TGT-DUP" {
        param($t) $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,usbd98.sys,,16`r`nusbd.sys,usbd2k.sys,,16")
    }
    Assert-RuleFires "tgt-no-flag" "TGT-FLAGS" {
        param($t) $t.Replace("usbd.sys,usbd2k.sys,,16", "usbd.sys,usbd2k.sys")
    }
    # 16|4: NO_OVERWRITE plus NOVERSIONCHECK, which overwrites the target
    # regardless of version - including a newer post-SP4 usbd.sys.
    Assert-RuleFires "tgt-noversioncheck" "TGT-FLAGS" {
        param($t) $t.Replace("usbd.sys,usbd2k.sys,,16", "usbd.sys,usbd2k.sys,,20")
    }
    # The two source names swapped between the paths. Structurally perfect -
    # two paths, two sections, two distinct media files, correct flags - and
    # it installs Win98 SE's usbd.sys on Windows 2000. Only the source
    # manifest's TARGET column can tell the difference.
    Assert-RuleFires "tgt-swapped-names" "TGT-TARGET" {
        param($t)
        $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,usbdTMP.sys,,16").
           Replace("usbd.sys,usbd2k.sys,,16", "usbd.sys,usbd98.sys,,16").
           Replace("usbd.sys,usbdTMP.sys,,16", "usbd.sys,usbd2k.sys,,16")
    }
    # A media name no manifest row claims: the packager would never stage it,
    # so the install looks for a file the package cannot contain.
    Assert-RuleFires "tgt-unknown-media" "TGT-TARGET" {
        param($t) $t.Replace("usbd2k.sys=1", "usbdnt.sys=1").Replace("usbd.sys,usbd2k.sys,,16", "usbd.sys,usbdnt.sys,,16")
    }
    Assert-RuleFires "tgt-dest" "TGT-DEST" {
        param($t) $t.Replace("Xhci.CopyW98=10,System32\Drivers", "Xhci.CopyW98=11")
    }
    # Right-click Install goes through DefaultInstall, so the split has to hold
    # there too or a pre-stage lays down the other target's build.
    Assert-RuleFires "tgt-default-swap" "TGT-DEFAULT" {
        param($t) $t.Replace("CopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98",
                             "CopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW2K")
    }
    # No NT half at all: setupapi falls back to the undecorated section, and a
    # right-click Install on Windows 2000 stages the Win98 files. The per-path
    # rules skip a missing section, so this is its own refusal.
    Assert-RuleFires "no-defaultinstall-nt" "TGT-DEFAULT" {
        param($t) $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K`r`n", "")
    }
    # A -SourceManifest the caller typed and that does not exist is a mistake,
    # not a host with nothing staged, so the gate refuses rather than warns.
    $noManifest = Join-Path $script:work "no-such-manifest.expected"
    $r = Invoke-Gate -Path $prodInf -SourceManifest $noManifest
    Assert-True ($r.ExitCode -ne 0) "missing-manifest : an explicit -SourceManifest that does not exist was accepted (exit 0)."
    Assert-True ($r.Output -match [regex]::Escape("FAIL [TGT-TARGET]")) ("missing-manifest : expected TGT-TARGET to fail. Output was:`n" + $r.Output)

    # ---- the Win98-only usbhub.sys (batch 13-E, task 13-E.1) ---------------
    #
    # Added by the pre-cut audit (finding B6): the seven W98-* rules
    # below were traced and are sound, and **not one of them had ever been
    # watched fire**. A lint nobody has watched fail is not a gate - that is
    # this file's own opening sentence, and the W98 family had been shipped
    # without it while every TGT-* sibling had one.
    #
    # The asymmetry is what makes them worth the cases. usbd.sys goes to BOTH
    # targets under different names, so a mistake there is symmetric and the
    # TGT-* rules can be stated as "both, and differently". usbhub.sys goes to
    # ONE target on purpose, so its rules must fire in two opposite directions
    # and the likelier future mistake is somebody tidying the asymmetry away.
    Write-Step "the Win98-only usbhub.sys"

    # The bug batch 13-E found on real hardware: every composite device stops at
    # `USB Composite Device`, Code 2, and nothing on the machine says why.
    Assert-RuleFires "w98-no-hub" "W98-MISSING" {
        param($t) $t.Replace("`r`nusbhub.sys,usbhub98.sys,,16", "").
                     Replace("`r`nusbhub98.sys=1", "")
    }
    # The opposite direction, and the more dangerous one: somebody making the
    # two install paths look symmetrical. On Windows 2000 that name belongs to
    # the OS's own USB 1.1 hub driver.
    Assert-RuleFires "w98-hub-on-nt" "W98-ONWIN2K" {
        param($t) $t.Replace("[Xhci.CopyW2K]`r`nusbd.sys,usbd2k.sys,,16",
                             "[Xhci.CopyW2K]`r`nusbd.sys,usbd2k.sys,,16`r`nusbhub.sys,usbhub98.sys,,16")
    }
    # The same two directions over the right-click Install pair.
    Assert-RuleFires "w98-no-hub-default" "W98-MISSING" {
        param($t) $t.Replace("[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98",
                             "[DefaultInstall]`r`nCopyFiles=Inf.CopyFiles,Xhci.CopyFiles")
    }
    Assert-RuleFires "w98-hub-on-nt-default" "W98-ONWIN2K" {
        param($t) $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K",
                             "[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyW2K,Xhci.CopyW98")
    }
    Assert-RuleFires "w98-hub-dup" "W98-DUP" {
        param($t) $t.Replace("usbhub.sys,usbhub98.sys,,16",
                             "usbhub.sys,usbhub98.sys,,16`r`nusbhub.sys,usbhub98.sys,,16")
    }
    # No source-name field: the media would have to carry the file under the
    # target's own name, which is the confusion the distinct media name exists
    # to prevent.
    Assert-RuleFires "w98-hub-medianame" "W98-MEDIANAME" {
        param($t) $t.Replace("usbhub.sys,usbhub98.sys,,16", "usbhub.sys,,,16")
    }
    # Without COPYFLG_NO_OVERWRITE a machine whose native USB stack was
    # installed loses its own usbhub.sys to Windows 98 SE's 4.10.2222 build.
    Assert-RuleFires "w98-hub-no-flag" "W98-FLAGS" {
        param($t) $t.Replace("usbhub.sys,usbhub98.sys,,16", "usbhub.sys,usbhub98.sys")
    }
    # A media name the manifest records as the WIN2000 build. Structurally
    # perfect and it installs the wrong OS's file; only the manifest's TARGET
    # column can tell.
    Assert-RuleFires "w98-hub-wrong-target" "W98-TARGET" {
        param($t) $t.Replace("usbhub.sys,usbhub98.sys,,16", "usbhub.sys,usbd2k.sys,,16")
    }
    # Windows 98 loads its composite parent from System32\Drivers. Dirid 11 is
    # \Windows\System, where nothing looks for it - and the copy succeeds.
    Assert-RuleFires "w98-hub-dest" "W98-DEST" {
        param($t) $t.Replace("Xhci.CopyW98=10,System32\Drivers", "Xhci.CopyW98=11")
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
        param($t) $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,usbd98.sys,,0")
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
    Assert-RowVerdict -Rows $fp -Prefix "file|Windows 2000|Xhci.CopyW2K|" -Verdict "keep" `
        -What "the untouched Windows 2000 usbd.sys rows"

    # An unparseable flags field is not silently a zero.
    $badFlagInf = New-MutatedInf -Name "fp-bad-flags" -Mutate {
        param($t) $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,usbd98.sys,,sixteen")
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
            param($t) $t.Replace("usbd.sys,usbd98.sys,,16", ("usbd.sys,usbd98.sys,," + $case.Flags))
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

    # Task 11-V.6's fix, asserted against the production INF by value and by
    # ABSENCE on the other path - an assertion, not a mutation control, and
    # named as such. A value present on only one install path is normally the
    # exact failure this gate exists to catch (the VAL-* rules); this one is
    # deliberate, because Windows 2000's usbport never idle-suspends this
    # controller, so the asymmetry has to be pinned rather than merely allowed.
    # Pinned as a whole row: the value 1 is what stops the idle suspend, and a
    # 0 here would be a silently disabled fix that every VAL-* rule would pass.
    # TWO rows, not one, and the count is the assertion: the value is delivered
    # by the device install AND by right-click Install, because on Windows 98 an
    # update-over-an-existing-install bugchecks before its registry phase, so a
    # single-route value never reaches a machine that already had this driver.
    # A drop to one row is that regression and must fail here.
    $want11v6 = "reg|Windows 98|Xhci.AddReg.Global|HKLM|System\CurrentControlSet\Services\USB|DisableSelectiveSuspend|0x00010001|1|remove"
    Assert-True (@($fp | Where-Object { $_ -eq $want11v6 }).Count -eq 2) (
        "both the device install and right-click Install must write Services\USB\DisableSelectiveSuspend = 1 (task 11-V.6's fix). Rows:`n" + ($fp -join "`n"))
    Assert-True (@($fp | Where-Object { $_ -like "reg|Windows 2000|*DisableSelectiveSuspend*" }).Count -eq 0) (
        "the Windows 2000 path must NOT write DisableSelectiveSuspend - that target has no idle suspend to stop. Rows:`n" + ($fp -join "`n"))

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
        param($t) $t.Replace("usbd.sys,usbd98.sys,,16", "usbd.sys,usbd98.sys,,4294967296")
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

    # The package tests use stand-in files and a matching stand-in manifest, so
    # they run on a host with no install media staged under tools\ - the
    # gitignored directory a fresh clone does not have. What is being tested is
    # the gate's arithmetic, not the real binaries' contents.
    $w98Bytes = [System.Text.Encoding]::ASCII.GetBytes("stand-in for Windows 98 SE usbd.sys 4.10.2222")
    $w2kBytes = [System.Text.Encoding]::ASCII.GetBytes("stand-in for Windows 2000 SP4 USBD.SYS 5.00.2195.6658")
    # Windows 98's composite parent, carried since batch 13-E. Win98-only, so
    # there is deliberately no Windows 2000 counterpart to pair it with.
    $hubBytes = [System.Text.Encoding]::ASCII.GetBytes("stand-in for Windows 98 SE usbhub.sys 4.10.2222")

    function New-StandInPackage {
        param([string]$Name, [byte[]]$For98, [byte[]]$For2K, [byte[]]$ForHub)
        $dir = Join-Path $script:work $Name
        New-Item -ItemType Directory -Path $dir | Out-Null
        Copy-Item -LiteralPath $prodInf -Destination (Join-Path $dir "xhci98.inf")
        Set-Content -LiteralPath (Join-Path $dir "xhci98.sys") -Value "not a real driver" -Encoding ASCII
        if ($null -ne $For98) { [System.IO.File]::WriteAllBytes((Join-Path $dir "usbd98.sys"), $For98) }
        if ($null -ne $For2K) { [System.IO.File]::WriteAllBytes((Join-Path $dir "usbd2k.sys"), $For2K) }
        # Defaults to the composite parent stand-in so that only tests which
        # care about it have to mention it; a caller omitting it on purpose
        # passes an empty array, which is how the pkg-nohub PKG-LAYOUT case
        # below is made.
        if ($null -eq $ForHub) { $ForHub = $hubBytes }
        if ($ForHub.Length -gt 0) { [System.IO.File]::WriteAllBytes((Join-Path $dir "usbhub98.sys"), $ForHub) }
        return $dir
    }

    function Get-StandInSha {
        param([byte[]]$Bytes)
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("X2") }) -join "") }
        finally { $sha.Dispose() }
    }

    # VERSION "-" is the manifest's "no version resource" marker; the stand-ins
    # have none.
    $standInManifest = Join-Path $script:work "stand-in-sources.expected"
    Set-Content -LiteralPath $standInManifest -Encoding ASCII -Value @(
        "# stand-in manifest for scripts\inf-gate\test-inf-checks.ps1",
        ("usbd98.sys win98   tools\win98se-extracted\usbd.sys  - {0} {1} stand-in" -f $w98Bytes.Length, (Get-StandInSha $w98Bytes)),
        ("usbd2k.sys win2000 tools\win2ksp4-extracted\USBD.SYS - {0} {1} stand-in" -f $w2kBytes.Length, (Get-StandInSha $w2kBytes)),
        ("usbhub98.sys win98 tools\win98se-extracted\usbhub.sys - {0} {1} stand-in" -f $hubBytes.Length, (Get-StandInSha $hubBytes))
    )

    $goodPkg = New-StandInPackage -Name "pkg-good" -For98 $w98Bytes -For2K $w2kBytes
    $r = Invoke-Gate -Path $prodInf -PackageDir $goodPkg -SourceManifest $standInManifest
    Assert-True ($r.ExitCode -eq 0) ("a complete -PackageDir was rejected:`n" + $r.Output)

    # The failure the whole mechanism exists to catch, and the only one that no
    # amount of INF structure can: the right names, the wrong contents.
    $swappedPkg = New-StandInPackage -Name "pkg-swapped" -For98 $w2kBytes -For2K $w98Bytes
    $r = Invoke-Gate -Path $prodInf -PackageDir $swappedPkg -SourceManifest $standInManifest
    Assert-True ($r.ExitCode -ne 0) "a package with the two usbd.sys builds swapped was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-IDENTITY]")) ("expected PKG-IDENTITY to fire on a swapped package. Output:`n" + $r.Output)

    # [SourceDisksFiles] may place a media file in a subdirectory. Authenticate
    # the exact path Setup consumes, not a same-named root file that happens to
    # contain the expected build.
    $subdirInf = New-MutatedInf -Name "pkg-subdir" -Mutate {
        param($t) $t.Replace("usbd98.sys=1", "usbd98.sys=1,usbfiles")
    }
    $subdirPkg = New-StandInPackage -Name "pkg-subdir" -For98 $w98Bytes -For2K $w2kBytes
    $subdir = Join-Path $subdirPkg "usbfiles"
    New-Item -ItemType Directory -Path $subdir | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $subdir "usbd98.sys"), $w2kBytes)
    $r = Invoke-Gate -Path $subdirInf -PackageDir $subdirPkg -SourceManifest $standInManifest
    Assert-True ($r.ExitCode -ne 0) "a package whose authenticated root file masked a substituted [SourceDisksFiles] subdirectory file was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-IDENTITY]")) ("expected PKG-IDENTITY to authenticate the [SourceDisksFiles] subdirectory path. Output:`n" + $r.Output)

    $partialPkg = New-StandInPackage -Name "pkg-partial" -For98 $w98Bytes -For2K $null
    $r = Invoke-Gate -Path $prodInf -PackageDir $partialPkg -SourceManifest $standInManifest
    Assert-True ($r.ExitCode -ne 0) "a package missing the Win2000 usbd.sys was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-LAYOUT]")) ("expected PKG-LAYOUT to fire on a package missing usbd2k.sys. Output:`n" + $r.Output)

    # The same absence for the composite parent, and until the pre-cut audit the
    # -ForHub parameter above described a caller that did not exist: nothing
    # ever dropped usbhub98.sys, so the one file batch 13-E's fix delivers was
    # the one file no layout case checked for (pre-cut audit, finding B6).
    $noHubPkg = New-StandInPackage -Name "pkg-nohub" -For98 $w98Bytes -For2K $w2kBytes -ForHub @()
    $r = Invoke-Gate -Path $prodInf -PackageDir $noHubPkg -SourceManifest $standInManifest
    Assert-True ($r.ExitCode -ne 0) "a package missing the Win98 usbhub.sys was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-LAYOUT]")) ("expected PKG-LAYOUT to fire on a package missing usbhub98.sys. Output:`n" + $r.Output)

    # A manifest that names two identical builds defeats the split silently,
    # so the reader rejects it rather than the packager discovering it later.
    $sameManifest = Join-Path $script:work "same-build-sources.expected"
    Set-Content -LiteralPath $sameManifest -Encoding ASCII -Value @(
        ("usbd98.sys win98   tools\win98se-extracted\usbd.sys  - {0} {1} stand-in" -f $w98Bytes.Length, (Get-StandInSha $w98Bytes)),
        ("usbd2k.sys win2000 tools\win2ksp4-extracted\USBD.SYS - {0} {1} stand-in" -f $w98Bytes.Length, (Get-StandInSha $w98Bytes))
    )
    $r = Invoke-Gate -Path $prodInf -PackageDir $goodPkg -SourceManifest $sameManifest
    Assert-True ($r.ExitCode -ne 0) "a manifest giving both targets the same build was accepted."
    Assert-True ($r.Output -match [regex]::Escape("[PKG-IDENTITY]")) ("expected PKG-IDENTITY to reject a same-build manifest. Output:`n" + $r.Output)

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
