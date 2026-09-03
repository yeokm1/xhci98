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

    # ---- the files the OS supplies (Phase 17, release 1.0.0.1; Phase 19) ----
    #
    # Since 1.0.0.1 the media carries no Microsoft file: usbd.sys and usbhub.sys
    # are copied from the OS's own install source through LayoutFile, and since
    # 1.0.0.2 the NT path copies usbport.sys and usbhub20.sys the same way.
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
        param($t) $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT,Xhci.AddReg.Global`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT",
                             "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT,Xhci.AddReg.Global`r`nCopyFiles=Xhci.CopyFiles")
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
        param($t) $t.Replace("[DefaultInstall.NTx86]`r`nCopyFiles=Xhci.CopyFiles,Xhci.CopyNT`r`nAddReg=Xhci.AddReg.Global`r`n", "")
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

    # ---- SUSP-* : DisableSelectiveSuspend on every route (Phase 19) ---------
    #
    # The machine-wide value, on four routes since 1.0.0.2. Each route losing
    # it is a hot-plug that nothing notices on Windows 98 or Windows XP, and a
    # 0 or a non-DWORD is the same defect with the value still "present".
    Write-Step "DisableSelectiveSuspend on every route"
    Assert-RuleFires "susp-no-9x-device" "SUSP-MISSING" {
        param($t) $t.Replace("[Xhci.Dev]`r`nAddReg=Xhci.AddReg,Xhci.AddReg.Global", "[Xhci.Dev]`r`nAddReg=Xhci.AddReg")
    }
    Assert-RuleFires "susp-no-nt-device" "SUSP-MISSING" {
        param($t) $t.Replace("[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT,Xhci.AddReg.Global", "[Xhci.Dev.NTx86]`r`nAddReg=Xhci.AddReg.NT")
    }
    Assert-RuleFires "susp-no-9x-default" "SUSP-MISSING" {
        param($t) $t.Replace("CopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98`r`nAddReg=Xhci.AddReg.Global`r`n",
                             "CopyFiles=Inf.CopyFiles,Xhci.CopyFiles,Xhci.CopyW98`r`n")
    }
    Assert-RuleFires "susp-no-nt-default" "SUSP-MISSING" {
        param($t) $t.Replace("CopyFiles=Xhci.CopyFiles,Xhci.CopyNT`r`nAddReg=Xhci.AddReg.Global`r`n",
                             "CopyFiles=Xhci.CopyFiles,Xhci.CopyNT`r`n")
    }
    Assert-RuleFires "susp-value-zero" "SUSP-VALUE" {
        param($t) $t.Replace("DisableSelectiveSuspend,0x00010001,1", "DisableSelectiveSuspend,0x00010001,0")
    }
    Assert-RuleFires "susp-not-dword" "SUSP-VALUE" {
        param($t) $t.Replace("DisableSelectiveSuspend,0x00010001,1", "DisableSelectiveSuspend,,1")
    }
    Assert-RuleFires "susp-dup" "SUSP-DUP" {
        param($t) $t.Replace("DisableSelectiveSuspend,0x00010001,1`r`n",
                             "DisableSelectiveSuspend,0x00010001,1`r`nHKLM,System\CurrentControlSet\Services\USB,DisableSelectiveSuspend,0x00010001,1`r`n")
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

    # Task 11-V.6's fix, asserted against the production INF by value on BOTH
    # paths - an assertion, not a mutation control, and named as such. Until
    # 1.0.0.2 this pinned the value's ABSENCE on the Windows 2000 path, because
    # that target's native usbport never idle-suspends this controller; the
    # Windows XP reading of 2026-09-03 (roadmap task 19.2: usbport's
    # SuspendController within thirty seconds, the hot-plugged mouse invisible)
    # made it an NT-path need, so the pin inverted. Pinned as a whole row: the
    # value 1 is what stops the idle suspend, and a 0 here would be a silently
    # disabled fix that every VAL-* rule would pass. TWO rows per target, not
    # one, and the count is the assertion: the value is delivered by the device
    # install AND by right-click Install, because on Windows 98 an
    # update-over-an-existing-install bugchecks before its registry phase, so a
    # single-route value never reaches a machine that already had this driver.
    # A drop to one row is that regression and must fail here. The SUSP-*
    # rules in the gate say the same thing about a mutated INF; this is the
    # production file.
    foreach ($os in @("Windows 98", "Windows 2000")) {
        $want11v6 = "reg|" + $os + "|Xhci.AddReg.Global|HKLM|System\CurrentControlSet\Services\USB|DisableSelectiveSuspend|0x00010001|1|remove"
        Assert-True (@($fp | Where-Object { $_ -eq $want11v6 }).Count -eq 2) (
            "on " + $os + " both the device install and right-click Install must write Services\USB\DisableSelectiveSuspend = 1 (task 11-V.6's fix, on the NT path since 1.0.0.2). Rows:`n" + ($fp -join "`n"))
    }

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
