<#
.SYNOPSIS
Setup-engine compatibility gate for src\xhci98.inf (roadmap Phase 3 task 6).

.DESCRIPTION
One INF has to satisfy two unrelated setup engines, and both of them fail
quietly. Windows 98's 16-bit engine has no log at all and answers a
section-name overrun, a dirid-12 destination, or a missing .NTx86 counterpart
with nothing more than a device that does not work; Windows 2000 writes
setupapi.log but still installs a device with no service and no complaint. This
script turns the documented restrictions into a build-time failure instead.

What it checks, grouped by the failure each rule prevents:

  FILE-*   Encoding and line endings. Win98's parser is ANSI/CRLF only; a
           Unicode or LF-only INF is ignored or misparsed.
  W98-*    The Win98-parser traps in docs\contributing\build-and-test.md ("Win98 INF-parser
           traps"): the 28-character section-name limit, first-section-wins on
           duplicates, dirid 12 resolving to \Windows\System\Iosubsys, and
           8.3-clean file and path components.
  BOTH-*   Rules both engines share: $CHICAGO$ signature, the USB class,
           resolvable section cross-references, CopyFiles sections named in
           DestinationDirs with driver files sent to System32\Drivers,
           SourceDisksNames/SourceDisksFiles coverage, and defined %strings%.
  PATH-*   The two install paths themselves - every model must reach both an
           undecorated Win98 install section carrying DevLoader/NTMPDriver and
           a .NTx86 section whose .NTx86.Services AddService names a binary the
           same CopyFiles section actually delivers, with the required kernel
           driver service type, demand start, and normal error control.
  OS-*     The files the operating system supplies - usbd.sys and usbhub.sys
           on both targets, usbport.sys on the NT targets - which the media
           does not carry (usbd.sys and usbhub.sys since release 1.0.0.1,
           usbport.sys on the NT path since 1.0.0.2): [Version] must name
           LayoutFile=layout.inf, every install path (device and right-click)
           must copy usbd.sys and usbhub.sys and the NT paths alone
           usbport.sys (Windows 98's layout.inf has no such file; NUSB or
           SweetLow's stack places it there), each under its own name with
           COPYFLG_NO_OVERWRITE and no overwrite flag to System32\Drivers,
           none of them, nor a 1.0.0.0 media name for one, may appear in
           [SourceDisksFiles], and usbhub20.sys, which Windows 2000's own
           USB.INF places with the root hub and XP does not have, is named
           on no path at all.
  SUSP-*   The one machine-wide value, Services\USB\DisableSelectiveSuspend =
           1: every install path (device and right-click, both targets) must
           write it as a DWORD 1. Windows 98's usbport builds idle-suspend the
           controller within half a second and Windows XP's within thirty
           seconds, and a halted xHC cannot report a hot-plug. Until 1.0.0.2
           the NT path omitted it and the self-tests pinned the asymmetry;
           the XP reading of 2026-09-03 inverted that.
  VAL-*    Per-device registry values the driver reads at run time. Each must
           be written by BOTH install paths, as the right type, with the right
           default - a value present on one path only is invisible on the other
           target and neither engine reports it (roadmap tasks 11-V.7, 11-V.9
           and 13-L.2). There are two of them and both are DWORDs - three until
           at the snapshot-value merge, when XhciLogSnapshot joined the ladder.
           (**VAL-SZ was removed with XhciLogFile.** It was the
           string half - a REG_SZ's data is text two setup engines may quote,
           trim or tokenise differently, which a DWORD's is not - and with no
           REG_SZ left in the table it was a rule structurally incapable of
           firing, which is precisely the defect this gate's own self-tests
           once found in VAL-MISSING. The caution it encoded is not lost: it is
           written into src\xhci98.inf beside the values, and a future REG_SZ
           value here must bring the rule back with it.)
  PKG-*    A staged package (-PackageDir): every [SourceDisksFiles] entry is
           present, and no Microsoft file is in it under any name.

Every failure line starts with its rule id so the self-tests
(scripts\inf-gate\test-inf-checks.ps1) can assert that a specific rule fired
rather than merely that something did.

.PARAMETER InfPath
The INF to check. Defaults to src\xhci98.inf.

.PARAMETER PackageDir
Optional. A staged install-media directory: every file named in
[SourceDisksFiles] must be present in it (honouring the per-file subdir
field), and nothing in it may be a Microsoft file. Use this to check a
package layout before copying it to a VM.

.PARAMETER AllowUnpaddedDriverVer
Accept a `DriverVer` date whose month or day is a single digit - `8/16/2026`
rather than `08/16/2026`. **This exists for exactly one experiment**, roadmap
task 12.4: Windows 2000 records no driver date for this package, and the two
uncontrolled differences from Microsoft's own working entry are that ours is
unsigned and that its date is zero-padded. Separating them needs a package that
is identical except for the padding, and this gate's `^\d{2}/\d{2}/\d{4}` rule
is what forbids building one.

What is relaxed is a **local convention, not the file format**: an unpadded date
is Microsoft's own form (SP4's `[EHCI.NT]` carries `DriverVer=2/15/2003,...`).
The rule is worth keeping for everything else because a two-digit-everywhere
date is the one form neither setup engine can misread, so this switch relaxes
**only** the padding: the field must still be `M/D/YYYY,x.y.z.w`, a two-digit
year is still refused, and every other rule in this file is untouched. The run
says loudly that it was used.

.PARAMETER EmitMediaLayout
Optional. Writes `sourcename=relative\path` for every [SourceDisksFiles] entry
to this file - the layout a package must have to satisfy -PackageDir, taken
from this script's own parse of the INF.

scripts\package\make-package.ps1 stages against it rather than parsing
[SourceDisksFiles] a second time. Two parsers would be free to disagree, and
the way they would disagree is a package staged at one path and authenticated
at another - which is precisely the bug the per-file subdir handling in
-PackageDir exists to close. Callers must check the exit code before trusting
the file: it is written from the parse, not from the verdict.

.PARAMETER EmitFootprint
Optional. Writes what one install of this INF *claims to place* - every file and
every registry entry, per install path, each with a verdict on whether this
install wrote what is present - to this file, again from this script's own parse.

Roadmap task 11-V.3 requires an uninstall to "remove only files/registry
entries owned by this package", and that clause is not checkable against a
memory of what the INF does or against a walk of an installed machine: a walk
answers "what is on this box", which includes everything every other install
put there. The INF is the only authority for what this package claimed, so the
expected footprint is derived from it - and from the same parse the rules above
use, so the two cannot drift.

The verdict column is derived, not asserted. COPYFLG_NO_OVERWRITE means the
file may have been there already and this package cannot claim it, so such a
row reads `keep`: removing `usbd.sys` on uninstall would take a file this
package never placed and leave `usbhub20.sys` unable to load.

What the file does NOT contain is what an engine leaves behind of its own
accord - Win98's registered `xhci98.tmp`, Windows 2000's `oemN.inf` copy. Those
are measurements, they belong beside the run that takes them, and putting them
in a derivation would make it a mixture of two kinds of claim.

Like -EmitMediaLayout, it is written from the parse rather than from the
verdict, so callers must check the exit code before trusting it.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\inf-gate\check-inf.ps1

.EXAMPLE
powershell -File scripts\inf-gate\check-inf.ps1 -PackageDir out\pkg-debug
#>

[CmdletBinding()]
param(
    [string]$InfPath = "",
    [string]$PackageDir = "",
    [string]$EmitMediaLayout = "",
    [string]$EmitFootprint = "",
    [switch]$AllowUnpaddedDriverVer
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

$script:failures = @()
$script:warnings = @()

function Add-Failure {
    param([string]$Rule, [string]$Message)
    $script:failures += "[$Rule] $Message"
    Write-Host ("FAIL [{0}] {1}" -f $Rule, $Message) -ForegroundColor Red
}

function Add-Warning2 {
    param([string]$Rule, [string]$Message)
    $script:warnings += "[$Rule] $Message"
    Write-Warn ("[{0}] {1}" -f $Rule, $Message)
}

# --------------------------------------------------------------------
# Parsing. Deliberately dumb and literal: this is a lint of the text the
# setup engines see, not an INF interpreter. Directives are matched
# case-insensitively because both engines are case-insensitive.
# --------------------------------------------------------------------

function Remove-InfComment {
    param([string]$Line)
    # No quoting subtleties are needed for this file, but a ';' inside a
    # double-quoted string (EnumPropPages-style values) must not start a
    # comment, so track quotes.
    $inQuote = $false
    for ($i = 0; $i -lt $Line.Length; $i++) {
        $c = $Line[$i]
        if ($c -eq '"') { $inQuote = -not $inQuote }
        elseif ($c -eq ';' -and -not $inQuote) { return $Line.Substring(0, $i) }
    }
    return $Line
}

function Read-Inf {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    $rawLines = $text -split "`r`n", 0, "SimpleMatch"

    $inf = @{
        Bytes         = $bytes
        Text          = $text
        SectionOrder  = New-Object System.Collections.ArrayList
        Sections      = @{}   # lower-case name -> array of @{ Line; Text }
        SectionNames  = @{}   # lower-case name -> first-seen literal name
        Duplicates    = New-Object System.Collections.ArrayList
        LineCount     = $rawLines.Count
    }

    $current = $null
    for ($i = 0; $i -lt $rawLines.Count; $i++) {
        $lineNo = $i + 1
        $line = (Remove-InfComment $rawLines[$i]).Trim()
        if ($line -eq "") { continue }
        if ($line -match '^\[(.+)\]$') {
            $name = $matches[1].Trim()
            $key = $name.ToLowerInvariant()
            if ($inf.Sections.ContainsKey($key)) {
                [void]$inf.Duplicates.Add(@{ Name = $name; Line = $lineNo })
            } else {
                $inf.Sections[$key] = New-Object System.Collections.ArrayList
                $inf.SectionNames[$key] = $name
                [void]$inf.SectionOrder.Add($name)
            }
            $current = $key
            continue
        }
        if ($null -ne $current) {
            [void]$inf.Sections[$current].Add(@{ Line = $lineNo; Text = $line })
        }
    }
    return $inf
}

function Get-Section {
    param($Inf, [string]$Name)
    $key = $Name.ToLowerInvariant()
    if ($Inf.Sections.ContainsKey($key)) { return @($Inf.Sections[$key]) }
    return $null
}

function Test-SectionExists {
    param($Inf, [string]$Name)
    return $Inf.Sections.ContainsKey($Name.ToLowerInvariant())
}

function Get-Directive {
    # Returns the comma-separated values of every "key=..." entry in a section
    # whose key matches, as trimmed strings.
    param($Inf, [string]$Section, [string]$Key)
    $out = New-Object System.Collections.ArrayList
    $entries = Get-Section $Inf $Section
    if ($null -eq $entries) { return @() }
    foreach ($e in $entries) {
        if ($e.Text -match ('^\s*' + [regex]::Escape($Key) + '\s*=\s*(.*)$')) {
            foreach ($v in ($matches[1] -split ',')) {
                $t = $v.Trim()
                if ($t -ne "") { [void]$out.Add($t) }
            }
        }
    }
    return @($out)
}

function Get-DirectiveRaw {
    # Whole right-hand side, uncomma-split, for directives whose value is
    # positional (AddService, SourceDisksNames rows, CopyFiles rows).
    param($Inf, [string]$Section, [string]$Key)
    $out = New-Object System.Collections.ArrayList
    $entries = Get-Section $Inf $Section
    if ($null -eq $entries) { return @() }
    foreach ($e in $entries) {
        if ($e.Text -match ('^\s*' + [regex]::Escape($Key) + '\s*=\s*(.*)$')) {
            [void]$out.Add(@{ Line = $e.Line; Value = $matches[1].Trim() })
        }
    }
    return @($out)
}

function Test-Name83 {
    param([string]$Name)
    if ($Name -match '^([^.\\/:*?"<>|]{1,8})(\.[^.\\/:*?"<>|]{1,3})?$') { return $true }
    return $false
}

# --------------------------------------------------------------------

$repo = Get-RepoRoot
if ($InfPath -eq "") { $InfPath = Join-Path $repo "src\xhci98.inf" }
if (-not (Test-Path -LiteralPath $InfPath)) {
    Write-Err "no INF at '$InfPath'."
    exit 1
}
$InfPath = (Resolve-Path -LiteralPath $InfPath).Path

Write-Step ("INF gate: {0}" -f $InfPath)

# ---- FILE-* --------------------------------------------------------

$bytes = [System.IO.File]::ReadAllBytes($InfPath)

if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
    Add-Failure "FILE-ENCODING" "the file starts with a UTF-16LE BOM. Win98's parser reads ANSI only and will not see this INF at all."
} elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
    Add-Failure "FILE-ENCODING" "the file starts with a UTF-8 BOM. Write it as plain ASCII."
}

$nonAscii = @($bytes | Where-Object { $_ -ge 0x80 })
if ($nonAscii.Count -gt 0) {
    Add-Failure "FILE-ENCODING" ("{0} byte(s) are >= 0x80. Keep the file 7-bit ASCII so the Win98 and Win2000 codepages cannot disagree about it." -f $nonAscii.Count)
}

# Every LF must be the tail of a CRLF **and every CR the head of one**. Counting
# LF against CRLF alone catches a bare LF and not a bare CR (repo audit D5), and
# a bare CR is a line ending some readers honour - a classic-Mac-style line, or
# the tail of a botched rewrite - that this file's own CRLF splitting does not
# see at all, so the section and key parsing below would silently join two
# logical lines. `scripts\package\make-11v-media.ps1` already fixed and
# documented the identical gap in its own check.
$lfTotal = @($bytes | Where-Object { $_ -eq 10 }).Count
$crTotal = @($bytes | Where-Object { $_ -eq 13 }).Count
$crlf = ([regex]::Matches([System.Text.Encoding]::ASCII.GetString($bytes), "`r`n")).Count
if ($lfTotal -ne $crlf) {
    Add-Failure "FILE-EOL" ("{0} of {1} line ending(s) are bare LF. Win98's parser needs CRLF." -f ($lfTotal - $crlf), $lfTotal)
}
if ($crTotal -ne $crlf) {
    Add-Failure "FILE-EOL" ("{0} carriage return(s) are not followed by a line feed. Win98's parser needs CRLF, and a bare CR is a line break this gate's own parsing cannot see." -f ($crTotal - $crlf))
}
if ($bytes.Length -gt 0 -and $bytes[$bytes.Length - 1] -ne 10) {
    Add-Warning2 "FILE-EOL" "the file does not end with a newline."
}

$inf = Read-Inf -Path $InfPath

# ---- W98-SECTLEN / W98-DUPSECT -------------------------------------

foreach ($name in $inf.SectionOrder) {
    if ($name.Length -gt 28) {
        Add-Failure "W98-SECTLEN" ("section [{0}] is {1} characters; Win98's parser limit is 28." -f $name, $name.Length)
    }
}

foreach ($d in $inf.Duplicates) {
    Add-Failure "W98-DUPSECT" ("section [{0}] is defined more than once (line {1}). Win2000 merges same-named sections but Win98 uses the first and silently drops the rest." -f $d.Name, $d.Line)
}

# ---- BOTH-VERSION --------------------------------------------------

if (-not (Test-SectionExists $inf "Version")) {
    Add-Failure "BOTH-VERSION" "there is no [Version] section."
} else {
    $sig = @(Get-Directive $inf "Version" "Signature")
    if ($sig.Count -ne 1 -or $sig[0].Trim('"') -ne '$CHICAGO$') {
        Add-Failure "BOTH-VERSION" ('[Version] Signature must be "$CHICAGO$" - it is the one signature every WDM platform accepts, and "$Windows NT$" makes Win98 ignore the file. Found: ''{0}''.' -f ($sig -join ','))
    }
    $guid = @(Get-Directive $inf "Version" "ClassGUID")
    if ($guid.Count -ne 1 -or $guid[0].ToUpperInvariant() -ne "{36FC9E60-C465-11CF-8056-444553540000}") {
        Add-Failure "BOTH-VERSION" ("[Version] ClassGUID must be the existing USB class {{36FC9E60-C465-11CF-8056-444553540000}}; anything else needs a [ClassInstall] this project does not have. Found: '{0}'." -f ($guid -join ','))
    }
    $cls = @(Get-Directive $inf "Version" "Class")
    if ($cls.Count -ne 1 -or $cls[0].ToUpperInvariant() -ne "USB") {
        Add-Failure "BOTH-VERSION" ("[Version] Class must be USB. Found: '{0}'." -f ($cls -join ','))
    }
    $dv = @(Get-DirectiveRaw $inf "Version" "DriverVer")
    #
    # The padding rule and its one exception (roadmap task 12.4). The relaxed
    # pattern differs from the strict one in the two quantifiers and **nowhere
    # else**: the year is still four digits, the separator is still a comma, and
    # the version still has to be there. That is what keeps this a relaxation of
    # a local convention rather than of the format.
    #
    $dvPattern = '^\d{2}/\d{2}/\d{4}\s*,\s*\d+(\.\d+){1,3}$'
    $dvShape = "MM/DD/YYYY,x.y.z.w"
    if ($AllowUnpaddedDriverVer) {
        $dvPattern = '^\d{1,2}/\d{1,2}/\d{4}\s*,\s*\d+(\.\d+){1,3}$'
        $dvShape = "M/D/YYYY,x.y.z.w (padding rule relaxed)"
    }
    if ($dv.Count -ne 1) {
        Add-Failure "BOTH-VERSION" "[Version] needs exactly one DriverVer."
    } elseif ($dv[0].Value -notmatch $dvPattern) {
        Add-Failure "BOTH-VERSION" ("DriverVer must be {0}. Found: '{1}'." -f $dvShape, $dv[0].Value)
    } else {
        if ($AllowUnpaddedDriverVer) {
            # Loud, per the task: a gate that can be talked into widening what it
            # accepts is worth less afterwards than the answer is worth, so the
            # run that used the switch says so in its own output.
            Add-Warning2 "BOTH-VERSION" ("-AllowUnpaddedDriverVer: the MM/DD zero-padding rule is relaxed for this run only (roadmap task 12.4's experiment). DriverVer = '{0}'. This package is NOT install media - it exists to answer whether Windows 2000 rejects our driver date because it is padded." -f $dv[0].Value)
        }
        # Task 8-A.4, as rebuilt by task 14.1.10. DriverVer and the binary's
        # own version resource are two statements of one fact, and the failure
        # they produce apart is a quiet one: Windows 2000 ranks candidate
        # drivers by DriverVer, so a package whose INF says 1.1.0.0 while its
        # .sys says 1.0.0.0 upgrades cleanly and then reports the wrong build in
        # every file-properties dialog and bug report afterwards.
        #
        # **The authority moved.** It used to be this INF, with the four
        # resource fields read out of src\xhci98.rc and compared against it.
        # Task 14.1.10 made src\xhci_version.h the one editable source, so the
        # authority is that header: the resource *includes* it and cannot
        # disagree, and what is left to check is the copy that cannot include
        # anything - this INF's DriverVer, both halves of it.
        #
        # **Only when the header is beside the INF**, which is true of
        # src\xhci98.inf and false of the copies this gate is run against
        # elsewhere - the packager checks a staged INF whose media directory
        # carries no source at all, and the gate's own regression suite feeds it
        # hand-written fragments. Failing those would be checking where the file
        # is rather than what it says.
        #
        $verHdrPath = Join-Path (Split-Path -Parent $InfPath) "xhci_version.h"
        $rcPath     = Join-Path (Split-Path -Parent $InfPath) "xhci98.rc"
        if (-not (Test-Path $verHdrPath)) {
            Write-Host ("  (no xhci_version.h beside this INF - DriverVer/version cross-check skipped)") -ForegroundColor DarkGray
        } else {
            $infVersion = ($dv[0].Value -split ',')[1].Trim()
            $infDate    = ($dv[0].Value -split ',')[0].Trim()
            $hdrText    = Get-Content -LiteralPath $verHdrPath -Raw
            $hdrName    = Split-Path -Leaf $verHdrPath

            #
            # **Both forms of the number are read, and they are checked against
            # each other first.** The header spells the version out twice
            # because FILEVERSION takes four integers and cannot be given a
            # string, and no other consumer wants the integers - so nothing in
            # any toolchain would notice the two drifting apart. This is the
            # check that replaces the four the resource used to get.
            #
            $csv = $null; $str = $null; $hdrDate = $null
            if ($hdrText -match '(?m)^\s*#define\s+XHCI_VER_CSV\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*$') {
                $csv = "{0}.{1}.{2}.{3}" -f $matches[1], $matches[2], $matches[3], $matches[4]
            } else {
                Add-Failure "BOTH-VERSION" ("{0} has no readable XHCI_VER_CSV - four comma-separated integers and nothing else on the line. It is what FILEVERSION and PRODUCTVERSION expand to." -f $hdrName)
            }
            if ($hdrText -match '(?m)^\s*#define\s+XHCI_VER_STR\s+"([\d.]+)"\s*$') {
                $str = $matches[1]
            } else {
                Add-Failure "BOTH-VERSION" ("{0} has no readable XHCI_VER_STR - a bare four-part version in quotes and nothing else on the line. It is what the two resource strings and both tools expand to." -f $hdrName)
            }
            if ($hdrText -match '(?m)^\s*#define\s+XHCI_DRIVERVER_DATE\s+"([^"]*)"\s*$') {
                $hdrDate = $matches[1]
            } else {
                Add-Failure "BOTH-VERSION" ("{0} has no readable XHCI_DRIVERVER_DATE - the release date in quotes, in the MM/DD/YYYY form DriverVer takes." -f $hdrName)
            }

            if ($null -ne $csv -and $null -ne $str -and $csv -ne $str) {
                Add-Failure "BOTH-VERSION" ("{0} disagrees with itself: XHCI_VER_CSV is {1} and XHCI_VER_STR is {2}. They are one number in the two forms the toolchains need, and nothing else in the build compares them." -f $hdrName, $csv, $str)
            }
            if ($null -ne $str -and $str -ne $infVersion) {
                Add-Failure "BOTH-VERSION" ("{0} says the version is {1} but this INF's DriverVer says {2}. The header is the one place the version is edited; bump it there, or the installed driver reports a build nobody shipped." -f $hdrName, $str, $infVersion)
            }
            if ($null -ne $hdrDate -and $hdrDate -ne $infDate) {
                Add-Failure "BOTH-VERSION" ("{0} says the release date is {1} but this INF's DriverVer says {2}. Windows 2000 ranks a candidate driver by that date before it looks at the version, so the two disagreeing is not cosmetic." -f $hdrName, $hdrDate, $infDate)
            }

            #
            # **And the resource must not carry a version of its own.** This is
            # the rule that keeps the header an authority rather than a
            # suggestion, and it exists because the first attempt at a macro
            # (see src\xhci98.rc's own comment) made this cross-check vacuous:
            # the gate searched the .rc for the version text and the #define
            # line satisfied the search whatever the VALUE entries said. A
            # literal here would not be caught by anything else in the build -
            # rc.exe is perfectly happy to compile one - and it would be the
            # copy an installed machine reads.
            #
            if (Test-Path $rcPath) {
                $rcText = Get-Content -LiteralPath $rcPath -Raw
                $rcName = Split-Path -Leaf $rcPath
                $rcRules = @(
                    @{ Name = "FILEVERSION";               Want = 'XHCI_VER_CSV'; Pattern = '(?m)^\s*FILEVERSION\s+(.+?)\s*$' },
                    @{ Name = "PRODUCTVERSION";            Want = 'XHCI_VER_CSV'; Pattern = '(?m)^\s*PRODUCTVERSION\s+(.+?)\s*$' },
                    @{ Name = 'VALUE "FileVersion"';       Want = 'XHCI_VER_STR'; Pattern = '(?m)^\s*VALUE\s+"FileVersion"\s*,\s*(.+?)\s*$' },
                    @{ Name = 'VALUE "ProductVersion"';    Want = 'XHCI_VER_STR'; Pattern = '(?m)^\s*VALUE\s+"ProductVersion"\s*,\s*(.+?)\s*$' }
                )
                foreach ($rule in $rcRules) {
                    if ($rcText -notmatch $rule.Pattern) {
                        Add-Failure "BOTH-VERSION" ("{0} has no {1} line at all. Task 8-A.4 requires all four version fields." -f $rcName, $rule.Name)
                        continue
                    }
                    $decl = $matches[1]
                    if ($decl -notmatch [regex]::Escape($rule.Want)) {
                        Add-Failure "BOTH-VERSION" ("{0}'s {1} reads '{2}', which does not use {3}. The version is edited in {4} and nowhere else (roadmap task 14.1.10); a literal here compiles cleanly and ships a number this gate did not check." -f $rcName, $rule.Name, $decl, $rule.Want, $hdrName)
                    }
                }
                if ($rcText -notmatch '(?m)^\s*#include\s+"xhci_version\.h"\s*$') {
                    Add-Failure "BOTH-VERSION" ("{0} does not include the version header, so the macros above are undefined and rc.exe would compile whatever they happen to expand to." -f $rcName)
                }
            } else {
                Write-Host ("  (no xhci98.rc beside this INF - resource declaration check skipped)") -ForegroundColor DarkGray
            }
        }
    }
}

# ---- gather the install sections from [Manufacturer] ---------------

$models = New-Object System.Collections.ArrayList     # @{ Section; Id; Desc }
$modelSections = New-Object System.Collections.ArrayList

if (-not (Test-SectionExists $inf "Manufacturer")) {
    Add-Failure "BOTH-XREF" "there is no [Manufacturer] section, so no device can ever match this INF."
} else {
    foreach ($e in (Get-Section $inf "Manufacturer")) {
        if ($e.Text -notmatch '^\s*[^=]+=\s*(.+)$') { continue }
        foreach ($sec in ($matches[1] -split ',')) {
            $sec = $sec.Trim()
            # A trailing TargetOSVersion field is XP-era; this project has none.
            if ($sec -eq "") { continue }
            if (-not (Test-SectionExists $inf $sec)) {
                Add-Failure "BOTH-XREF" ("[Manufacturer] names models section [{0}] (line {1}), which does not exist." -f $sec, $e.Line)
            } else {
                [void]$modelSections.Add($sec)
            }
            break   # only the first field is the models-section name
        }
    }
}

foreach ($ms in $modelSections) {
    foreach ($e in (Get-Section $inf $ms)) {
        if ($e.Text -notmatch '^\s*(.+?)\s*=\s*(.+)$') { continue }
        $desc = $matches[1]
        $fields = @($matches[2] -split ',' | ForEach-Object { $_.Trim() })
        if ($fields.Count -lt 2) {
            Add-Failure "BOTH-XREF" ("models line {0} in [{1}] has no hardware ID." -f $e.Line, $ms)
            continue
        }
        [void]$models.Add(@{ Section = $fields[0]; Id = $fields[1]; Desc = $desc; Line = $e.Line })
    }
}

if ($models.Count -eq 0) {
    Add-Failure "BOTH-XREF" "no models line was found - nothing in this INF installs on anything."
}

# ---- PATH-* : both install paths must be present and correct -------

$driverBinaries = New-Object System.Collections.ArrayList

foreach ($m in $models) {
    $base = $m.Section

    # Win98 half: the undecorated section, DevLoader and NTMPDriver.
    if (-not (Test-SectionExists $inf $base)) {
        Add-Failure "PATH-W98" ("model '{0}' names install section [{1}], which does not exist. Win98 reads only undecorated section names, so it would find nothing to install." -f $m.Id, $base)
    } else {
        $addRegs = @(Get-Directive $inf $base "AddReg")
        $devLoader = $null
        $ntmp = $null
        foreach ($ar in $addRegs) {
            foreach ($e in (Get-Section $inf $ar)) {
                if ($e.Text -match '^\s*HKR\s*,\s*,\s*DevLoader\s*,[^,]*,\s*(.+)$') { $devLoader = $matches[1].Trim().Trim('"') }
                if ($e.Text -match '^\s*HKR\s*,\s*,\s*NTMPDriver\s*,[^,]*,\s*(.+)$') { $ntmp = $matches[1].Trim().Trim('"') }
            }
        }
        if ($null -eq $devLoader -or $devLoader.ToUpperInvariant() -ne "*NTKERN") {
            Add-Failure "PATH-W98" ("install section [{0}] does not set HKR,,DevLoader,,*NTKERN. Without it Win98 never loads a WDM driver for the device." -f $base)
        }
        if ($null -eq $ntmp) {
            Add-Failure "PATH-W98" ("install section [{0}] does not set HKR,,NTMPDriver. That value names the .sys ntkern loads; without it the device binds to nothing." -f $base)
        } else {
            foreach ($d in ($ntmp -split ',')) { [void]$driverBinaries.Add($d.Trim()) }
        }
    }

    # Win2000 half: the .NTx86 section, its .Services, and the AddService.
    $nt = "$base.NTx86"
    if (-not (Test-SectionExists $inf $nt)) {
        Add-Failure "PATH-NT" ("model '{0}' has no [{1}] section. Win2000 would fall back to the undecorated Win98 section, install no service, and leave the device with a driver-less devnode." -f $m.Id, $nt)
        continue
    }
    foreach ($ar in @(Get-Directive $inf $nt "AddReg")) {
        # Get-Section answers $null for a missing section and @($null) is a
        # one-element array, not an empty one. This guard was latent until task
        # 11-V.7 gave [<model>.NTx86] its first AddReg=: with no NT AddReg at
        # all the loop never ran, so the xref-addreg self-test walked straight
        # past it. A dangling AddReg= is BOTH-XREF's finding, not this one's.
        $arEntries = Get-Section $inf $ar
        if ($null -eq $arEntries) { continue }
        foreach ($e in $arEntries) {
            if ($e.Text -match '^\s*HKR\s*,\s*,\s*(DevLoader|NTMPDriver)\s*,') {
                Add-Warning2 "PATH-NT" ("[{0}] writes a 9x-only loader value ({1}) into the Win2000 device key via [{2}]." -f $nt, $matches[1], $ar)
            }
        }
    }

    $svcSection = "$nt.Services"
    if (-not (Test-SectionExists $inf $svcSection)) {
        Add-Failure "PATH-NT" ("[{0}] has no [{1}]. On Win2000 the device installs with no service: Device Manager shows it present and the driver never loads." -f $nt, $svcSection)
        continue
    }
    $addSvc = @(Get-DirectiveRaw $inf $svcSection "AddService")
    if ($addSvc.Count -ne 1) {
        Add-Failure "PATH-NT" ("[{0}] must contain exactly one AddService; found {1}." -f $svcSection, $addSvc.Count)
        continue
    }
    $svcFields = @($addSvc[0].Value -split ',' | ForEach-Object { $_.Trim() })
    if ($svcFields.Count -lt 3 -or $svcFields[0] -eq "" -or $svcFields[2] -eq "") {
        Add-Failure "PATH-NT" ("AddService in [{0}] needs a service name, flags, and an install section: '{1}'." -f $svcSection, $addSvc[0].Value)
        continue
    }
    if ($svcFields[1] -notmatch '^(0x0*2|2)$') {
        Add-Failure "PATH-NT" ("AddService in [{0}] must pass flag 0x00000002 (SPSVCINST_ASSOCSERVICE) so the service becomes the device's function driver; found '{1}'." -f $svcSection, $svcFields[1])
    }
    $svcInstall = $svcFields[2]
    if (-not (Test-SectionExists $inf $svcInstall)) {
        Add-Failure "BOTH-XREF" ("AddService in [{0}] names section [{1}], which does not exist." -f $svcSection, $svcInstall)
        continue
    }

    $binary = @(Get-Directive $inf $svcInstall "ServiceBinary")
    if ($binary.Count -ne 1) {
        Add-Failure "PATH-NT" ("[{0}] needs exactly one ServiceBinary." -f $svcInstall)
    } else {
        if ($binary[0] -notmatch '^%12%\\(.+)$') {
            Add-Failure "PATH-NT" ("ServiceBinary in [{0}] must be %12%\<driver>.sys; found '{1}'." -f $svcInstall, $binary[0])
        } else {
            $svcFile = $matches[1]
            [void]$driverBinaries.Add($svcFile)
            # The service binary has to be a file this INF actually delivers,
            # via the CopyFiles of the .NTx86 section that installed it.
            $ntCopy = @(Get-Directive $inf $nt "CopyFiles")
            $delivered = New-Object System.Collections.ArrayList
            foreach ($cf in $ntCopy) {
                foreach ($e in (Get-Section $inf $cf)) {
                    $dst = ($e.Text -split ',')[0].Trim()
                    if ($dst -ne "") { [void]$delivered.Add($dst.ToLowerInvariant()) }
                }
            }
            if (-not $delivered.Contains($svcFile.ToLowerInvariant())) {
                Add-Failure "PATH-NT" ("[{0}] ServiceBinary is '{1}' but the CopyFiles of [{2}] delivers ({3}). Win2000 would create a service pointing at a file the install never copied." -f $svcInstall, $svcFile, $nt, ($delivered -join ', '))
            }
        }
    }
    foreach ($req in @(
        @{ Name = "ServiceType"; Expected = 1; Meaning = "SERVICE_KERNEL_DRIVER" },
        @{ Name = "StartType"; Expected = 3; Meaning = "SERVICE_DEMAND_START" },
        @{ Name = "ErrorControl"; Expected = 1; Meaning = "SERVICE_ERROR_NORMAL" }
    )) {
        $value = @(Get-Directive $inf $svcInstall $req.Name)
        if ($value.Count -ne 1) {
            Add-Failure "PATH-NT" ("[{0}] needs exactly one {1}." -f $svcInstall, $req.Name)
        } elseif ($value[0] -notmatch ("^(0x0*{0}|{0})$" -f $req.Expected)) {
            Add-Failure "PATH-NT" ("[{0}] {1} must be {2} ({3}); found '{4}'." -f $svcInstall, $req.Name, $req.Expected, $req.Meaning, $value[0])
        }
    }
}

# ---- VAL-* : per-device values that must exist on BOTH paths -------
#
# Roadmap task 11-V.7 asked for this rule by name, and the reason is the same
# one the whole PATH-* family exists for: **a value present on only one install
# path is a value that silently does not exist on the other target**, and
# neither engine says so. The driver's log switch is read on Win98 and on
# Win2000 through the same usbport service, so it has to be written on both.
#
# The default is checked too, not only the presence. The 16 KB ring is in the
# extension whether or not a switch is set - that cost is unconditional and is
# stated in the release notes - but a switch that shipped enabled would add the
# per-record appends from DPC and ISR contexts, and a debug dump every time the
# device stops, on every machine whose owner never asked for one. "The default
# drifted" is not something a build can notice any other way, and task 13-L.1's
# whole finding is that a dangerous default is how an unmeasured facility
# reaches a published release unnoticed.
#
# **Task 13-L.2 made these three, and every one of them a DWORD defaulting to
# 0; the amendment below took the count to two.** `XhciLogFile` - a
# REG_SZ whose path was its own enable - is retired with
# the file sink, so the one FLG_ADDREG_TYPE_SZ row here is gone and with it an
# unmeasured question about how Win98's 16-bit engine writes an empty REG_SZ.
#
# **Three became two at stage L3**, when `XhciLogSnapshot` merged into the
# ladder: it was a pure consent bit for the read channel, and consent nests
# inside depth, so `XhciLogVerbosity = 0` is now the shut door. The rule this
# family enforces is untouched by that - both survivors are still written on
# both install paths and still checked for their default here.

$requiredValues = @(
    @{
        Name    = "XhciLogVerbosity"
        Type    = "0x00010001"
        Default = "0"
        Why     = "task 13-L.2's 0-4 ladder, and since `0.0.0.6` the WHOLE switch: 0 shuts the PassThru read channel (the miniport answers exactly MP_STATUS_NOT_SUPPORTED, the same as a binary built without it - and that channel is how a dump leaves the machine at all, the only route this driver has on Windows 98), 1 engages it with the note ring still off, and 2 is the recording switch. It ships at 0 because what the append sites cost at real interrupt rates on Windows 98 metal is unmeasured, and because a diagnostic nobody asked for should not be reachable"
    },
    @{
        Name    = "XhciLogDebugView"
        Type    = "0x00010001"
        Default = "0"
        Why     = "task 11-V.9's DebugView sink, read from the same key, and an EMISSION switch only since task 13-L.2. It hands the ring over from the PASSIVE flush - never live mirroring, which is what bugchecks Windows 98 on metal"
    }
)

function Get-AddRegValues {
    param($Inf, [string[]]$Sections, [string]$ValueName)

    $found = @()
    foreach ($ar in $Sections) {
        # Get-Section answers $null for a section that does not exist, which
        # @(...) turns into a one-element array of $null rather than an empty
        # one - the same trap Get-Directive guards against above. A dangling
        # AddReg= is BOTH-XREF's finding, not this rule's.
        $entries = Get-Section $Inf $ar
        if ($null -eq $entries) { continue }
        foreach ($e in $entries) {
            # HKR,<subkey>,<value>,<flags>,<data> - subkey empty for the
            # device's own key, which is the only form this rule allows.
            if ($e.Text -match ('^\s*HKR\s*,\s*([^,]*)\s*,\s*{0}\s*,\s*([^,]*)\s*,\s*(.*)$' -f [regex]::Escape($ValueName))) {
                $found += @{
                    Section = $ar
                    Line    = $e.Line
                    Subkey  = $matches[1].Trim()
                    Flags   = $matches[2].Trim()
                    Data    = $matches[3].Trim()
                }
            }
        }
    }
    # Not `,$found`: that wraps, so an empty result comes back as a one-element
    # array holding an empty array, `$hits.Count` reads 1 and VAL-MISSING can
    # never fire. The self-tests are what found it - the rule looked right and
    # was structurally incapable of firing.
    return $found
}

foreach ($m in $models) {
    $paths = @(
        @{ Name = "Windows 98"; Install = $m.Section },
        @{ Name = "Windows 2000"; Install = ("{0}.NTx86" -f $m.Section) }
    )
    foreach ($p in $paths) {
        if (-not (Test-SectionExists $inf $p.Install)) { continue }  # PATH-* said so
        $addRegs = @(Get-Directive $inf $p.Install "AddReg")
        foreach ($req in $requiredValues) {
            $hits = @(Get-AddRegValues $inf $addRegs $req.Name)
            if ($hits.Count -eq 0) {
                Add-Failure "VAL-MISSING" ("the {0} install path ([{1}]) writes no '{2}' value. {3}. A value on one path only is invisible on the other target and nothing reports it." -f $p.Name, $p.Install, $req.Name, $req.Why)
                continue
            }
            if ($hits.Count -gt 1) {
                Add-Failure "VAL-DUP" ("the {0} install path writes '{1}' {2} times (lines {3}). Which one wins is engine-dependent." -f $p.Name, $req.Name, $hits.Count, (($hits | ForEach-Object { $_.Line }) -join ', '))
            }
            $hit = $hits[0]
            if ($hit.Subkey -ne "") {
                Add-Failure "VAL-SUBKEY" ("[{0}] line {1} writes '{2}' under subkey '{3}'. The miniport reads the device's own key, not a subkey of it." -f $hit.Section, $hit.Line, $req.Name, $hit.Subkey)
            }
            if ($hit.Flags.ToLowerInvariant() -ne $req.Type.ToLowerInvariant()) {
                Add-Failure "VAL-TYPE" ("[{0}] line {1} writes '{2}' with flags '{3}', not {4} (FLG_ADDREG_TYPE_DWORD). The miniport asks usbport for four bytes; a string would be handed over as its characters." -f $hit.Section, $hit.Line, $req.Name, $hit.Flags, $req.Type)
            }
            if ($hit.Data -ne $req.Default) {
                Add-Failure "VAL-DEFAULT" ("[{0}] line {1} defaults '{2}' to '{3}', not '{4}'. {5} must ship off." -f $hit.Section, $hit.Line, $req.Name, $hit.Data, $req.Default, $req.Name)
            }
            #
            # **VAL-SZ stood here and was removed with
            # XhciLogFile** (task 13-L.2). It fired only for a required value
            # whose type was FLG_ADDREG_TYPE_SZ, and there is no longer one:
            # both surviving values are DWORDs. A check that cannot fire is worse
            # than no check, because it reads as coverage - this gate's own
            # self-tests caught exactly that shape in VAL-MISSING once.
            #
            # What it said, so a future REG_SZ value brings it back rather than
            # rediscovering it: a DWORD's data field is a number both engines
            # parse the same way, and a string's is text that Win98's 16-bit
            # engine and Windows 2000's setupapi may quote, trim or tokenise
            # differently - and this project has measured neither. It refused a
            # quote, a %token%, surrounding whitespace, and an empty flags
            # field. src\xhci98.inf carries the same reasoning beside the
            # values themselves.
        }
    }
}

# ---- BOTH-XREF: every referenced section must exist ----------------

$referencedCopyFiles = New-Object System.Collections.ArrayList
# CopyFiles=@file names one file with no section of its own. It is not a
# cross-reference, but it is still a copy, so the file-name and source rules
# below run over these as well as over the sections.
$referencedCopyAtFiles = New-Object System.Collections.ArrayList
foreach ($secName in @($inf.SectionOrder)) {
    foreach ($key in @("AddReg", "DelReg", "CopyFiles", "DelFiles", "RenFiles", "UpdateInis", "Include", "Needs")) {
        foreach ($target in (Get-Directive $inf $secName $key)) {
            if ($key -eq "Include" -or $key -eq "Needs") { continue }
            if ($key -eq "CopyFiles" -and $target.StartsWith("@")) {
                [void]$referencedCopyAtFiles.Add(@{ Section = $secName; Name = $target.Substring(1) })
                continue
            }
            if (-not (Test-SectionExists $inf $target)) {
                Add-Failure "BOTH-XREF" ("[{0}] {1}= names section [{2}], which does not exist." -f $secName, $key, $target)
            } elseif ($key -eq "CopyFiles") {
                [void]$referencedCopyFiles.Add($target)
            }
        }
    }
}

# ---- W98-DIRID12 / BOTH-DESTDIR ------------------------------------

$destDirs = @{}
if (-not (Test-SectionExists $inf "DestinationDirs")) {
    Add-Failure "BOTH-DESTDIR" "there is no [DestinationDirs]; every CopyFiles destination would be a guess."
} else {
    foreach ($e in (Get-Section $inf "DestinationDirs")) {
        if ($e.Text -notmatch '^\s*(.+?)\s*=\s*(.+)$') { continue }
        $who = $matches[1].Trim()
        $fields = @($matches[2] -split ',' | ForEach-Object { $_.Trim() })
        $dirid = $fields[0]
        $subdir = ""
        if ($fields.Count -gt 1) { $subdir = $fields[1] }
        $destDirs[$who.ToLowerInvariant()] = @{ Dirid = $dirid; Subdir = $subdir; Line = $e.Line }
        if ($dirid -eq "12") {
            Add-Failure "W98-DIRID12" ("[DestinationDirs] line {0} sends '{1}' to dirid 12. That is %windir%\system32\drivers on Win2000 but \Windows\System\Iosubsys on Win98 - the driver lands in the wrong directory with no error. Spell it '10, System32\Drivers'." -f $e.Line, $who)
        }
        foreach ($part in ($subdir -split '\\')) {
            if ($part -ne "" -and $part.Length -gt 8) {
                Add-Failure "W98-83PATH" ("[DestinationDirs] subdirectory component '{0}' (line {1}) is longer than 8 characters; Win98 setup reports a spurious 'cannot find the file' for paths that are not 8.3-clean." -f $part, $e.Line)
            }
        }
    }
}

foreach ($cf in ($referencedCopyFiles | Sort-Object -Unique)) {
    if (-not $destDirs.ContainsKey($cf.ToLowerInvariant())) {
        Add-Failure "BOTH-DESTDIR" ("CopyFiles section [{0}] has no [DestinationDirs] entry, so it falls back to DefaultDestDir. Name every CopyFiles section explicitly." -f $cf)
    }
}

# ---- the files the operating system supplies -----------------------
#
# usbd.sys is one destination name and two different binaries, usbhub.sys is
# Windows 98's composite parent and the NT targets' hub driver, and
# usbport.sys is the port driver this miniport imports; nothing on an
# xHCI-only machine ever places any of them (docs\contributing\lessons.md,
# "usbhub20.sys bugchecks Win2000" and the Phase 19 entry;
# docs\issues\03-usbhub-sys-composite-devices.md). The media carries none:
# [Version] names LayoutFile=layout.inf, none of them is in
# [SourceDisksFiles], and a CopyFiles entry the INF's own [SourceDisksFiles]
# does not cover is resolved through the OS's layout.inf and fetched from the
# OS's own install source. Each target therefore gets its own OS's build by
# construction. This table is what BOTH-SOURCE exempts and what the OS-*
# rules below hold the INF to: the paths that must copy each file, and the
# paths that must not. Off carries the rule id and the reason, because the
# one file with an Off path is refused for a reason of its own: Windows 98's
# layout.inf has no usbport.sys row, so its engine could not resolve the
# entry, and the file is placed there by NUSB or SweetLow's stack.
$osSupplied = @(
    @{ File = "usbport.sys"; On = @("Win2000"); Off = @("Win98"); OffRule = "OS-ONWIN98";
       Why = "xhci98.sys imports it, and an NT install that never had a USB controller does not have it (both NT targets' layout.inf give it the Setup disposition that does not copy it; a controller install pulls it from Driver Cache\i386), so the driver cannot load at all: Code 39 with nothing in the trace, measured on Windows XP on 2026-09-03";
       OffWhy = "Windows 98's layout.inf has no usbport.sys row, so its 16-bit engine has no source to resolve the entry from; NUSB or SweetLow's stack places the file there and the 9x path must not ask for it" },
    @{ File = "usbd.sys";    On = @("Win98", "Win2000"); Off = @();
       Why = "usbhub20.sys imports USBD.SYS on both targets and nothing else on an xHCI-only machine places it, so without it the root hub cannot load (Code 2 on Windows 98, a 0xc0000034 naming usbhub20.sys on Windows 2000)" },
    @{ File = "usbhub.sys";  On = @("Win98", "Win2000"); Off = @();
       Why = "it is Windows 98's composite parent and the NT targets' hub driver, which an xHCI-only machine never gets from setup (both NT targets' layout.inf give it the disposition that does not copy it, read 2026-09-03), so on Windows 98 every multi-interface device stops at 'USB Composite Device' with Code 2 without it" }
)
$osSuppliedNames = @($osSupplied | ForEach-Object { $_.File.ToLowerInvariant() })
# Files the OS places by itself when this driver's root hub appears, which
# this INF must therefore name on no path. usbhub20.sys: Windows 2000 SP4's
# USB.INF [ROOTHUB2.NT] copies it from the driver cache for USB\ROOT_HUB20
# (read from the SP4 CD, 2026-09-03), and Windows XP has no such file, so a
# row for it has no source on XP. The owner's decision of 2026-09-03.
$osNeverNamed = @(
    @{ File = "usbhub20.sys";
       Why = "Windows 2000's own USB.INF copies it from the driver cache when usbport creates USB\ROOT_HUB20, and Windows XP has no such file (its usbport.inf binds that PDO to usbhub.sys), so on XP a row for it has no source; the OS places it, this INF does not" }
)
# The 1.0.0.0 media names, refused wherever they reappear: a package that
# starts carrying a Microsoft file again must be refused whichever name it
# hides under.
$retiredMediaNames = @("usbd98.sys", "usbd2k.sys", "usbhub98.sys")

# ---- BOTH-SOURCE: files, SourceDisksFiles, SourceDisksNames --------

$sourceDisks = @{}
foreach ($e in (Get-Section $inf "SourceDisksNames")) {
    if ($e.Text -match '^\s*(\d+)\s*=\s*(.*)$') { $sourceDisks[$matches[1]] = $matches[2] }
}
if (-not (Test-SectionExists $inf "SourceDisksNames")) {
    Add-Failure "BOTH-SOURCE" "there is no [SourceDisksNames]; the disk ids in [SourceDisksFiles] have no source-media definition."
} elseif ($sourceDisks.Count -eq 0) {
    Add-Failure "BOTH-SOURCE" "[SourceDisksNames] contains no disk entries."
}

$sourceFiles = @{}
foreach ($e in (Get-Section $inf "SourceDisksFiles")) {
    if ($e.Text -notmatch '^\s*(.+?)\s*=\s*(.+)$') { continue }
    $file = $matches[1].Trim()
    $fields = @($matches[2] -split ',' | ForEach-Object { $_.Trim() })
    $disk = $fields[0]
    $subdir = ""
    if ($fields.Count -gt 1) { $subdir = $fields[1] }
    $unsafeSubdir = $false
    if ($subdir -ne "") {
        $parts = @($subdir -split '[\\/]')
        $unsafeSubdir = [System.IO.Path]::IsPathRooted($subdir) -or
            @($parts | Where-Object { $_ -eq "." -or $_ -eq ".." }).Count -gt 0
        if ($unsafeSubdir) {
            Add-Failure "BOTH-SOURCE" ("[SourceDisksFiles] subdirectory '{0}' (line {1}) must stay below the package root: use a relative path with no '.' or '..' components." -f $subdir, $e.Line)
        }
    }
    # Relative is where this file has to sit on the media. Everything that
    # needs that answer - the -PackageDir presence check, the per-target
    # identity check, and -EmitMediaLayout - reads it from here, so a package
    # cannot be staged at one path and authenticated at another. Do not emit
    # or join an unsafe path even after recording the failure above.
    $relative = if ($subdir -eq "" -or $unsafeSubdir) { $file } else { Join-Path $subdir $file }
    $sourceFiles[$file.ToLowerInvariant()] = @{
        Disk     = $disk
        Subdir   = $subdir
        Name     = $file
        Relative = $relative
        Line     = $e.Line
    }
    if (-not $sourceDisks.ContainsKey($disk)) {
        Add-Failure "BOTH-SOURCE" ("[SourceDisksFiles] '{0}' is on disk {1}, which [SourceDisksNames] does not define." -f $file, $disk)
    }
    if (-not (Test-Name83 $file)) {
        Add-Failure "W98-83PATH" ("source file name '{0}' is not 8.3-clean; Win98 setup can report it missing even when it is present." -f $file)
    }
    foreach ($part in ($subdir -split '[\\/]')) {
        if ($part -ne "" -and $part -ne "." -and $part.Length -gt 8) {
            Add-Failure "W98-83PATH" ("[SourceDisksFiles] subdirectory component '{0}' (line {1}) is longer than 8 characters." -f $part, $e.Line)
        }
    }
}

$copiedFiles = New-Object System.Collections.ArrayList
foreach ($cf in ($referencedCopyFiles | Sort-Object -Unique)) {
    foreach ($e in (Get-Section $inf $cf)) {
        $fields = @($e.Text -split ',' | ForEach-Object { $_.Trim() })
        $dst = $fields[0]
        if ($dst -eq "") { continue }
        $src = $dst
        if ($fields.Count -gt 1 -and $fields[1] -ne "") { $src = $fields[1] }
        $tmp = ""
        if ($fields.Count -gt 2) { $tmp = $fields[2] }
        [void]$copiedFiles.Add($dst)

        if ($driverBinaries | Where-Object { $_.ToLowerInvariant() -eq $dst.ToLowerInvariant() }) {
            $destKey = $cf.ToLowerInvariant()
            if ($destDirs.ContainsKey($destKey)) {
                $driverDest = $destDirs[$destKey]
                if ($driverDest.Dirid -ne "10" -or $driverDest.Subdir -ine "System32\Drivers") {
                    Add-Failure "BOTH-DESTDIR" ("[{0}] delivers driver binary '{1}' to '{2},{3}', not '10,System32\Drivers'. NTMPDriver and ServiceBinary both load it from the drivers directory." -f $cf, $dst, $driverDest.Dirid, $driverDest.Subdir)
                }
            }
        }

        foreach ($n in @($dst, $src, $tmp)) {
            if ($n -ne "" -and -not (Test-Name83 $n)) {
                Add-Failure "W98-83PATH" ("CopyFiles entry '{0}' in [{1}] (line {2}) is not an 8.3 name." -f $n, $cf, $e.Line)
            }
        }
        # An OS-supplied file is meant to be absent from [SourceDisksFiles]:
        # LayoutFile is its source. The OS-* rules check that wiring.
        if (-not $sourceFiles.ContainsKey($src.ToLowerInvariant()) -and -not ($osSuppliedNames -contains $src.ToLowerInvariant())) {
            Add-Failure "BOTH-SOURCE" ("[{0}] copies '{1}' (line {2}) but [SourceDisksFiles] does not list it; setup has no source to copy it from." -f $cf, $src, $e.Line)
        }
    }
}

foreach ($at in $referencedCopyAtFiles) {
    $n = $at.Name
    if ($n -eq "") { continue }
    [void]$copiedFiles.Add($n)
    if (-not (Test-Name83 $n)) {
        Add-Failure "W98-83PATH" ("CopyFiles=@{0} in [{1}] is not an 8.3 name." -f $n, $at.Section)
    }
    if (-not $sourceFiles.ContainsKey($n.ToLowerInvariant())) {
        Add-Failure "BOTH-SOURCE" ("[{0}] copies '{1}' through CopyFiles=@ but [SourceDisksFiles] does not list it; setup has no source to copy it from." -f $at.Section, $n)
    }
}

foreach ($d in ($driverBinaries | Sort-Object -Unique)) {
    if (-not ($copiedFiles | Where-Object { $_.ToLowerInvariant() -eq $d.ToLowerInvariant() })) {
        Add-Failure "BOTH-SOURCE" ("the INF names driver binary '{0}' but no CopyFiles section delivers it." -f $d)
    }
}

# ---- OS-* : the files the operating system supplies ---------------------
#
# What these rules check is that the LayoutFile route is wired, because every
# way of breaking it is silent on the target: the directive is present, the
# media names no Microsoft file, both device-install paths and both
# right-click paths copy usbd.sys and usbhub.sys, the NT paths alone copy
# usbport.sys, and every such copy is under its own name (a media-name field
# would send the engine back to this disk), with COPYFLG_NO_OVERWRITE and no
# overwrite flag, to System32\Drivers. The 1.0.0.0 media kept the two builds
# of usbd.sys apart by name (usbd98.sys / usbd2k.sys, the TGT-* family) and
# hashed them against a manifest; with the OS supplying the file there is no
# such split to check.

function Get-CopyEntriesFor {
    param($Inf, [string[]]$Sections, [string]$File)

    $out = New-Object System.Collections.ArrayList
    foreach ($cf in $Sections) {
        # Get-Section answers $null for a missing section, and @($null) is a
        # one-element array whose element has no .Text. A dangling CopyFiles=
        # is BOTH-XREF's finding; this rule only reads the sections that exist,
        # so the parse still reaches -EmitMediaLayout and -EmitFootprint.
        $entries = Get-Section $Inf $cf
        if ($null -eq $entries) { continue }
        foreach ($e in $entries) {
            $fields = @($e.Text -split ',' | ForEach-Object { $_.Trim() })
            $dst = $fields[0]
            if ($dst -eq "" -or $dst.ToLowerInvariant() -ne $File.ToLowerInvariant()) { continue }
            $src = $dst
            if ($fields.Count -gt 1 -and $fields[1] -ne "") { $src = $fields[1] }
            $flags = ""
            if ($fields.Count -gt 3) { $flags = $fields[3] }
            [void]$out.Add(@{ Section = $cf; Dest = $dst; Source = $src; Flags = $flags; Line = $e.Line })
        }
    }
    return @($out)
}

function ConvertTo-CopyFlags {
    # Returns $null for an unparseable field. Both engines accept decimal and
    # 0x-prefixed hex; the reference INFs use decimal.
    #
    # A value that does not fit a signed 32-bit integer comes back $null rather
    # than throwing. Every caller already has an "I cannot read this" path and
    # uses it to refuse or to report; an exception instead would abort the whole
    # run - including -EmitFootprint, whose stated policy is that an
    # unrecognised flags field produces a `review` row rather than no file.
    param([string]$Text)
    if ($Text -eq "") { return 0 }
    $raw = $null
    if ($Text -match '^0[xX]([0-9A-Fa-f]+)$') {
        try { $raw = [System.Convert]::ToUInt64($matches[1], 16) } catch { return $null }
    } elseif ($Text -match '^\d+$') {
        try { $raw = [System.UInt64]::Parse($Text) } catch { return $null }
    } else {
        return $null
    }
    if ($raw -gt [System.Int32]::MaxValue) { return $null }
    return [int]$raw
}

# From C:\NTDDK\inc\SETUPAPI.H, with that header's own descriptions.
$COPYFLG_NOVERSIONCHECK       = 0x00000004   # ignore versions and overwrite target
$COPYFLG_FORCE_FILE_IN_USE    = 0x00000008   # force file-in-use behavior
$COPYFLG_NO_OVERWRITE         = 0x00000010   # do not copy if file exists on target
$COPYFLG_OVERWRITE_OLDER_ONLY = 0x00000040   # leave target alone if version same as source

# The directive itself. Without it a CopyFiles entry outside [SourceDisksFiles]
# is asked for from this disk, which does not carry the file, and the install
# stops at a prompt for a file the package cannot supply.
$layoutFile = @(Get-Directive $inf "Version" "LayoutFile")
if ($layoutFile.Count -ne 1 -or $layoutFile[0] -ine "layout.inf") {
    Add-Failure "OS-LAYOUT" ("[Version] must carry exactly 'LayoutFile=layout.inf'; found '{0}'. It is what makes usbport.sys, usbd.sys and usbhub.sys come from the OS's own install source rather than from this disk, which does not carry them." -f ($layoutFile -join ','))
}

# No Microsoft file on the media, under its own name or a 1.0.0.0 media name.
foreach ($key in @($sourceFiles.Keys)) {
    if ($osSuppliedNames -contains $key -or $retiredMediaNames -contains $key) {
        Add-Failure "OS-MEDIA" ("[SourceDisksFiles] names '{0}' (line {1}). The media carries no Microsoft file since 1.0.0.1: usbport.sys, usbd.sys and usbhub.sys come from the OS through LayoutFile, and an entry here would send the engine back to this disk for them (docs\contributing\legal-provenance.md section 5)." -f $sourceFiles[$key].Name, $sourceFiles[$key].Line)
    }
}

# A file the OS places by itself, named anywhere: on any CopyFiles section
# any install path reaches, and on the media.
foreach ($never in $osNeverNamed) {
    foreach ($cf in ($referencedCopyFiles | Sort-Object -Unique)) {
        foreach ($entry in @(Get-CopyEntriesFor $inf @($cf) $never.File)) {
            Add-Failure "OS-NEVER" ("[{0}] copies '{1}' (line {2}). This INF names it on no path: {3}." -f $cf, $never.File, $entry.Line, $never.Why)
        }
    }
    if ($sourceFiles.ContainsKey($never.File.ToLowerInvariant())) {
        Add-Failure "OS-NEVER" ("[SourceDisksFiles] names '{0}' (line {1}). The media carries no Microsoft file, and this one the OS places by itself: {2}." -f $never.File, $sourceFiles[$never.File.ToLowerInvariant()].Line, $never.Why)
    }
}

# A [DefaultInstall] with no [DefaultInstall.NTx86] beside it is the one shape
# the per-path rules below cannot see: setupapi's decorated-section lookup
# falls back to the undecorated section, so a right-click Install on Windows
# 2000 would run the Windows 98 file list, which has no usbport.sys and
# copies the INF into %17%.
if ((Test-SectionExists $inf "DefaultInstall") -and -not (Test-SectionExists $inf "DefaultInstall.NTx86")) {
    Add-Failure "OS-DEFAULT" "[DefaultInstall] exists without [DefaultInstall.NTx86]. Windows 2000 falls back to the undecorated section on a right-click Install and runs the Windows 98 file list, which has no usbport.sys and copies the INF into %17%."
}

foreach ($m in $models) {
    $base = $m.Section
    $nt = "$base.NTx86"
    if (-not (Test-SectionExists $inf $base) -or -not (Test-SectionExists $inf $nt)) {
        continue    # already reported as PATH-W98 / PATH-NT
    }

    $paths = @(
        @{ Name = "Win98";   Install = $base; Sections = @(Get-Directive $inf $base "CopyFiles"); Default = "DefaultInstall" },
        @{ Name = "Win2000"; Install = $nt;   Sections = @(Get-Directive $inf $nt "CopyFiles");   Default = "DefaultInstall.NTx86" }
    )

    foreach ($os in $osSupplied) {
        $file = $os.File
        foreach ($p in $paths) {
            # Both routes into a target: the device install, and the
            # right-click Install that pre-stages with no device present.
            $routes = @(@{ Label = ("the {0} device install ([{1}])" -f $p.Name, $p.Install); Sections = $p.Sections })
            if (Test-SectionExists $inf $p.Default) {
                $routes += @{ Label = ("the {0} right-click Install ([{1}])" -f $p.Name, $p.Default); Sections = @(Get-Directive $inf $p.Default "CopyFiles") }
            }
            foreach ($route in $routes) {
                $entries = @(Get-CopyEntriesFor $inf $route.Sections $file)

                if ($os.Off -contains $p.Name) {
                    if ($entries.Count -gt 0) {
                        Add-Failure $os.OffRule ("{0} copies '{1}' (line {2}). That is deliberate to NOT do: {3}. The asymmetry is intended - do not make the two paths symmetrical." -f $route.Label, $file, $entries[0].Line, $os.OffWhy)
                    }
                    continue
                }
                if (-not ($os.On -contains $p.Name)) { continue }

                if ($entries.Count -eq 0) {
                    Add-Failure "OS-MISSING" ("{0} does not copy '{1}': {2}." -f $route.Label, $file, $os.Why)
                    continue
                }
                if ($entries.Count -gt 1) {
                    Add-Failure "OS-DUP" ("{0} copies '{1}' {2} times (lines {3}). Which copy wins is engine-dependent; name it once per path." -f $route.Label, $file, $entries.Count, (($entries | ForEach-Object { $_.Line }) -join ', '))
                }
                foreach ($entry in $entries) {
                    if ($entry.Source.ToLowerInvariant() -ne $entry.Dest.ToLowerInvariant()) {
                        Add-Failure "OS-SRCNAME" ("[{0}] line {1} copies '{2}' from a media file named '{3}'. An OS-supplied file is copied under its own name with no source-name field: a media name points the engine at this disk, which carries no Microsoft file." -f $entry.Section, $entry.Line, $file, $entry.Source)
                    }

                    $flags = ConvertTo-CopyFlags $entry.Flags
                    if ($null -eq $flags) {
                        Add-Failure "OS-FLAGS" ("[{0}] line {1}: '{2}' is not a copy-flag value." -f $entry.Section, $entry.Line, $entry.Flags)
                    } else {
                        if (($flags -band $COPYFLG_NO_OVERWRITE) -eq 0) {
                            Add-Failure "OS-FLAGS" ("[{0}] line {1} copies '{2}' without COPYFLG_NO_OVERWRITE (16). Presence is the whole requirement: a machine that already has the file, possibly a newer serviced build, must keep it, and is then not asked for its Windows CD at all." -f $entry.Section, $entry.Line, $file)
                        }
                        foreach ($bad in @(
                            @{ Bit = $COPYFLG_NOVERSIONCHECK; Name = "COPYFLG_NOVERSIONCHECK (4)"; Why = "it overwrites the target regardless of version" },
                            @{ Bit = $COPYFLG_FORCE_FILE_IN_USE; Name = "COPYFLG_FORCE_FILE_IN_USE (8)"; Why = "it schedules a replacement of a file that is in use" },
                            @{ Bit = $COPYFLG_OVERWRITE_OLDER_ONLY; Name = "COPYFLG_OVERWRITE_OLDER_ONLY (64)"; Why = "it still replaces an older target file for no benefit" }
                        )) {
                            if (($flags -band $bad.Bit) -ne 0) {
                                Add-Failure "OS-FLAGS" ("[{0}] line {1} sets {2} on '{3}': {4}." -f $entry.Section, $entry.Line, $bad.Name, $file, $bad.Why)
                            }
                        }
                    }

                    $destKey = $entry.Section.ToLowerInvariant()
                    if ($destDirs.ContainsKey($destKey)) {
                        $dd = $destDirs[$destKey]
                        if ($dd.Dirid -ne "10" -or $dd.Subdir -ine "System32\Drivers") {
                            Add-Failure "OS-DEST" ("[{0}] delivers '{1}' to '{2},{3}'. Both targets load it from System32\Drivers; spell it '10, System32\Drivers'." -f $entry.Section, $file, $dd.Dirid, $dd.Subdir)
                        }
                    }
                }
            }
        }
    }
}

# ---- SUSP-* : Services\USB\DisableSelectiveSuspend on every path ---------
#
# The one machine-wide value this package writes, and the only registry value
# it writes outside the device's own key. Every usbport build this driver has
# run under reads it (RtlQueryRegistryValues, RelativeTo = Services, "usb"),
# and two of them idle-suspend the controller without it: Windows 98's within
# half a second of the last transfer, Windows XP's within thirty seconds of a
# start with nothing attached; a halted xHC cannot report a port change, so a
# device plugged in afterwards is invisible until Refresh. Windows 2000's
# native build never idles this controller and the value changes nothing
# there. Until 1.0.0.2 the NT path omitted it for that reason and the
# self-tests pinned the omission; the XP reading of 2026-09-03 made the value
# an NT-path need too, so now every route must write it.
#
# Four routes, not two: the device install and the right-click Install on
# each target. The right-click route exists because on Windows 98 with NUSB an
# update over an existing install bugchecks before its registry phase, so a
# value carried only by the device install never reaches a machine that
# already had this driver. The value is pinned at 1 as a DWORD: a 0 here is a
# silently disabled fix that presence alone would pass.

$suspValue = "DisableSelectiveSuspend"
$suspKey = "System\CurrentControlSet\Services\USB"
foreach ($m in $models) {
    $base = $m.Section
    $nt = "$base.NTx86"
    $suspRoutes = @()
    foreach ($p in @(
        @{ Name = "Windows 98";   Install = $base; Default = "DefaultInstall" },
        @{ Name = "Windows 2000"; Install = $nt;   Default = "DefaultInstall.NTx86" }
    )) {
        if (Test-SectionExists $inf $p.Install) {
            $suspRoutes += @{ Label = ("the {0} device install ([{1}])" -f $p.Name, $p.Install); Section = $p.Install }
        }
        if (Test-SectionExists $inf $p.Default) {
            $suspRoutes += @{ Label = ("the {0} right-click Install ([{1}])" -f $p.Name, $p.Default); Section = $p.Default }
        }
    }
    foreach ($route in $suspRoutes) {
        $hits = @()
        foreach ($ar in @(Get-Directive $inf $route.Section "AddReg")) {
            $entries = Get-Section $inf $ar
            if ($null -eq $entries) { continue }
            foreach ($e in $entries) {
                if ($e.Text -match ('^\s*HKLM\s*,\s*([^,]*)\s*,\s*{0}\s*,\s*([^,]*)\s*,\s*(.*)$' -f [regex]::Escape($suspValue))) {
                    if ($matches[1].Trim() -ieq $suspKey) {
                        $hits += @{ Section = $ar; Line = $e.Line; Flags = $matches[2].Trim(); Data = $matches[3].Trim() }
                    }
                }
            }
        }
        if ($hits.Count -eq 0) {
            Add-Failure "SUSP-MISSING" ("{0} does not write HKLM,{1},{2}. Windows 98's and Windows XP's usbport idle-suspend the controller without it and a halted xHC cannot report a hot-plug; since 1.0.0.2 every install route on both targets writes it." -f $route.Label, $suspKey, $suspValue)
            continue
        }
        if ($hits.Count -gt 1) {
            Add-Failure "SUSP-DUP" ("{0} writes {1} {2} times (lines {3}). Which one wins is engine-dependent; name it once per route." -f $route.Label, $suspValue, $hits.Count, (($hits | ForEach-Object { $_.Line }) -join ', '))
        }
        foreach ($hit in $hits) {
            if ($hit.Flags.ToLowerInvariant() -ne "0x00010001" -or $hit.Data -ne "1") {
                Add-Failure "SUSP-VALUE" ("[{0}] line {1} writes {2} as flags '{3}' data '{4}', not 0x00010001 (FLG_ADDREG_TYPE_DWORD) and 1. usbport reads four bytes and acts on nonzero; anything else is the fix silently switched off." -f $hit.Section, $hit.Line, $suspValue, $hit.Flags, $hit.Data)
            }
        }
    }
}

# ---- W98-DIRID12, part two: %12% outside ServiceBinary -------------

$lineNo = 0
foreach ($raw in ($inf.Text -split "`r`n", 0, "SimpleMatch")) {
    $lineNo++
    $line = (Remove-InfComment $raw).Trim()
    if ($line -eq "") { continue }
    if ($line -match '%12%' -and $line -notmatch '^\s*ServiceBinary\s*=') {
        Add-Failure "W98-DIRID12" ("line {0} uses %12% outside a ServiceBinary. Only the NT-only service install may name dirid 12; anywhere Win98 reads it, 12 means \Windows\System\Iosubsys." -f $lineNo)
    }
}

# ---- BOTH-STRINGS --------------------------------------------------

$strings = @{}
foreach ($e in (Get-Section $inf "Strings")) {
    if ($e.Text -match '^\s*(.+?)\s*=\s*(.*)$') { $strings[$matches[1].Trim().ToLowerInvariant()] = $matches[2] }
}
foreach ($name in $inf.SectionOrder) {
    if ($name -match '^(?i)Strings\..+$') {
        Add-Warning2 "BOTH-STRINGS" ("[{0}] is a locale-decorated Strings section; Win98 ignores those entirely, so every token it defines must also be in the plain [Strings]." -f $name)
    }
}

$lineNo = 0
foreach ($raw in ($inf.Text -split "`r`n", 0, "SimpleMatch")) {
    $lineNo++
    $line = (Remove-InfComment $raw).Trim()
    if ($line -eq "" -or $line -match '^\[') { continue }
    foreach ($mt in [regex]::Matches($line, '%([^%\s]+)%')) {
        $tok = $mt.Groups[1].Value
        if ($tok -match '^\d+$') { continue }        # a dirid, not a string token
        if (-not $strings.ContainsKey($tok.ToLowerInvariant())) {
            Add-Failure "BOTH-STRINGS" ("line {0} uses %{1}%, which [Strings] does not define. Win98 substitutes nothing and the entry becomes garbage." -f $lineNo, $tok)
        }
    }
}

# ---- optional media-layout emit ------------------------------------
#
# Written from the parse above, before any verdict, so the packager and this
# gate can never be looking at two different [SourceDisksFiles] readings.

if ($EmitMediaLayout -ne "") {
    $layoutLines = @(
        "# scripts\inf-gate\check-inf.ps1 -EmitMediaLayout",
        "# sourcename=relative\path, from [SourceDisksFiles] of:",
        "# $InfPath"
    )
    foreach ($key in ($sourceFiles.Keys | Sort-Object)) {
        $layoutLines += ("{0}={1}" -f $sourceFiles[$key].Name, $sourceFiles[$key].Relative)
    }
    Write-AsciiFile -Path $EmitMediaLayout -Lines $layoutLines
    Write-Host ("media layout ({0} file(s)) written to {1}" -f $sourceFiles.Count, $EmitMediaLayout)
}

# ---- optional install-footprint emit --------------------------------
#
# What one install of this INF claims to place, per install path, with an
# uninstall verdict per row. See the -EmitFootprint help at the top of this
# file for why the INF is the only authority for that clause and why the
# engines' own residue is deliberately absent.
#
# Written from the parse above rather than from the verdict, exactly like
# -EmitMediaLayout, and every field comes from the same helpers the rules use -
# so a footprint cannot describe an INF the rules were not reading.

if ($EmitFootprint -ne "") {
    # Name the INF relative to the repository when it is inside one. This file
    # is tracked and diffed across machines, unlike -EmitMediaLayout's temp
    # file, so an absolute host path in it would be noise in every review.
    # The trailing separator is part of the test (repo audit D6). Without it,
    # `C:\work\xhci98` is a prefix of `C:\work\xhci98-scratch\...` too, and the
    # substring taken from such a path starts mid-name: the emitted header then
    # reads `-scratch\src\xhci98.inf`, which is not a path to anything.
    $repoPrefix = $repo.TrimEnd('\', '/') + '\'
    $infShown = $InfPath
    if ($InfPath.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        $infShown = $InfPath.Substring($repoPrefix.Length)
    }
    $fpLines = @(
        "# scripts\inf-gate\check-inf.ps1 -EmitFootprint",
        "# What ONE INSTALL of this INF claims to place, derived from the INF's",
        "# own sections. Roadmap tasks 11-B.3 and 11-V.3.",
        "# $infShown",
        "#",
        "# path|<os>|<kind>|<install section>",
        "# file|<os>|<copyfiles section>|<dirid>|<subdir>|<destination>|<media source>|<temp name>|<flags>|<verdict>",
        "# reg|<os>|<addreg section>|<root>|<subkey>|<value>|<flags>|<data>|<verdict>",
        "# service|<os>|<service name>|<service install section>|<AddService flags>|<verdict>",
        "# servicevalue|<os>|<service name>|<service install section>|<directive>|<value>",
        "# unmodelled|<os>|<section>|<directive>",
        "#",
        "# Verdicts. Each is derived from the flags field, and the derivation is",
        "# deliberately narrow - COPYFLG_*, FLG_ADDREG_* and SPSVCINST_* values",
        "# and their own descriptions are from C:\NTDDK\inc\SETUPAPI.H:",
        "#   remove - the flags say this install wrote UNCONDITIONALLY, so what",
        "#            is there now is what it put there.",
        "#   keep   - the flags make the write conditional on what the target",
        "#            already held, so this install may not have written it at",
        "#            all. COPYFLG_NO_OVERWRITE (0x10) skips an existing file and",
        "#            COPYFLG_REPLACEONLY (0x400) requires one; FLG_ADDREG_NOCLOBBER",
        "#            (0x02) and FLG_ADDREG_OVERWRITEONLY (0x20) are those same two",
        "#            conditions on the registry side. Removing usbd.sys, which is",
        "#            copied 0x10, would take a file this install never placed and",
        "#            leave usbhub20.sys unable to load.",
        "#   review - the INF alone cannot say. A version-conditional copy",
        "#            (COPYFLG_NO_VERSION_DIALOG 0x20, COPYFLG_OVERWRITE_OLDER_ONLY",
        "#            0x40); FLG_ADDREG_APPEND (0x08), which may leave",
        "#            pre-existing REG_MULTI_SZ elements in place;",
        "#            FLG_ADDREG_KEYONLY (0x10), which creates the key and ignores",
        "#            the value the row names; any SPSVCINST_NOCLOBBER_* bit, which",
        "#            makes a service-key value conditional the same way; an",
        "#            AddService carrying a DelService-only bit",
        "#            (DELETEEVENTLOGENTRY 0x04, STOPSERVICE 0x200); an AddService",
        "#            whose install section is missing; or an unparseable or",
        "#            unrecognised flags field.",
        "#",
        "# **Two things are emitted and are NOT part of any verdict, for the same",
        "# reason**: SPSVCINST_ASSOCSERVICE (0x02), which marks the service as the",
        "# device's function driver, and a registry root other than HKR, which is",
        "# a key that outlives the device's own. Both bear on what an UNINSTALL",
        "# does, which this derivation does not model and which nothing in this",
        "# repository establishes. Drafts made each of them a 'review' - the",
        "# service one, then the registry one a review round later - and each time",
        "# that answered a different question inside a column defined as",
        "# ownership. An unconditional write puts the value there whatever its",
        "# root or its association. What happens to it at uninstall is a",
        "# measurement; docs\contributing\runs\run-11v.md stage B4 takes it.",
        "#   none   - the row places nothing. FLG_ADDREG_DELVAL (0x04) deletes.",
        "#",
        "# COPYFLG_NOVERSIONCHECK (0x04) is deliberately NOT conditional: its own",
        "# description is 'ignore versions and overwrite target', so the copy",
        "# happens and the row is 'remove'. What it does not tell you is whether",
        "# it overwrote something - which is the limit stated below, and this is",
        "# the flag that makes that limit worth stating.",
        "#",
        "# **The limit of 'remove', stated rather than left implied**: it means",
        "# this install wrote the file or value that is there now. It does NOT mean",
        "# nothing was there before - a plain unflagged CopyFiles can replace an",
        "# existing file, and an INF cannot say whether it did. That question is",
        "# answered by the run, not by this derivation.",
        "#",
        "# The three verdicts sit on ONE axis - did this install's write happen -",
        "# and 'keep' is where the flags make that conditional on the target's",
        "# prior state. They deliberately do not answer a second question,",
        "# 'what does uninstall do to it', which depends on the key's lifetime and",
        "# on the service association and which this file models nowhere. Two",
        "# drafts mixed the second axis into the first; the review rounds that",
        "# caught them are why the distinction is spelled out here.",
        "#",
        "# NOT in here, because it is measured rather than claimed: what each",
        "# setup engine leaves behind of its own accord (Win98's registered",
        "# xhci98.tmp, Windows 2000's oemN.inf copy). Those live beside the run",
        "# that observes them, in docs\contributing\runs\run-11v.md.",
        "#",
        "# An 'unmodelled' row is this emitter saying a directive was added that",
        "# it does not translate - a loud gap rather than a silently short list.",
        "# Include= and Needs= appear as unmodelled rows because this emitter does",
        "# not follow them; an INF that grows one has a footprint reaching past",
        "# what is derived here. So does an engine-implied sub-section other than",
        "# .Services (.HW, .CoInstallers, .Interfaces, .LogConfigOverride)."
    )

    # Fixed order, so the output is diffable: every model's two device-install
    # paths, then the two right-click DefaultInstall paths. A path whose section
    # does not exist is skipped here and reported by PATH-*.
    $fpPaths = New-Object System.Collections.ArrayList
    foreach ($m in $models) {
        [void]$fpPaths.Add(@{ Os = "Windows 98";   Kind = "device install"; Install = $m.Section })
        [void]$fpPaths.Add(@{ Os = "Windows 2000"; Kind = "device install"; Install = ("{0}.NTx86" -f $m.Section) })
    }
    [void]$fpPaths.Add(@{ Os = "Windows 98";   Kind = "right-click Install"; Install = "DefaultInstall" })
    [void]$fpPaths.Add(@{ Os = "Windows 2000"; Kind = "right-click Install"; Install = "DefaultInstall.NTx86" })

    # ---- the two derivations, each narrow on purpose --------------------
    #
    # Values and their descriptions are C:\NTDDK\inc\SETUPAPI.H's own; the
    # COPYFLG_* constants above this block are already read from there.
    #
    # The rule is the same on both sides: a flag that makes the write
    # CONDITIONAL, or that says the target had to exist, means this install
    # cannot claim what is there. Only a flag whose meaning is known is acted
    # on - an unrecognised bit produces `review` rather than being treated as
    # zero, because the failure of guessing here is an uninstall that deletes
    # somebody else's file.
    $COPYFLG_REPLACEONLY       = 0x00000400   # copy only if file exists on target
    $COPYFLG_NO_VERSION_DIALOG = 0x00000020   # do not copy if target is newer
    # Bits that bear on ownership at all. Anything outside this mask is a flag
    # this emitter has not been taught, and is reported rather than ignored.
    $fpCopyKnown = $COPYFLG_NO_OVERWRITE -bor $COPYFLG_REPLACEONLY -bor `
                   $COPYFLG_NOVERSIONCHECK -bor $COPYFLG_NO_VERSION_DIALOG -bor `
                   $COPYFLG_OVERWRITE_OLDER_ONLY -bor $COPYFLG_FORCE_FILE_IN_USE -bor `
                   0x00000001 -bor 0x00000002 -bor `
                   0x00000800 -bor 0x00001000 -bor 0x00002000
    # "The target may already have held this file, and this install did not put
    # it there." COPYFLG_NO_OVERWRITE skips an existing file; COPYFLG_REPLACEONLY
    # copies ONLY onto one. Both mean the package cannot claim what is there.
    $fpCopyPreexisting = $COPYFLG_NO_OVERWRITE -bor $COPYFLG_REPLACEONLY
    # "Whether the copy happened at all depends on a version comparison this
    # derivation cannot make." COPYFLG_NOVERSIONCHECK is deliberately NOT in
    # here: its own description is "ignore versions and overwrite target", so
    # the copy is unconditional and the row is `remove` - subject to the stated
    # limit that `remove` does not mean nothing was there before, which for this
    # flag is exactly the interesting case.
    $fpCopyConditional = $COPYFLG_NO_VERSION_DIALOG -bor $COPYFLG_OVERWRITE_OLDER_ONLY

    $FLG_ADDREG_NOCLOBBER     = 0x00000002    # set only if not already present
    $FLG_ADDREG_DELVAL        = 0x00000004    # delete the value
    $FLG_ADDREG_APPEND        = 0x00000008    # append to an existing REG_MULTI_SZ
    $FLG_ADDREG_KEYONLY       = 0x00000010    # just create the key, ignore value
    $FLG_ADDREG_OVERWRITEONLY = 0x00000020    # set only if value already exists

    function Get-FootprintCopyVerdict {
        param([string]$FlagText)
        $v = ConvertTo-CopyFlags $FlagText
        if ($null -eq $v) { return "review" }
        if ($v -band (-bnot $fpCopyKnown)) { return "review" }
        if ($v -band $fpCopyPreexisting) { return "keep" }
        if ($v -band $fpCopyConditional) { return "review" }
        return "remove"
    }

    function Get-FootprintRegVerdict {
        param([string]$Root, [string]$FlagText)
        # **The root is emitted, and it is not part of the verdict.** A draft
        # returned `review` for every non-HKR root on the grounds that such a key
        # outlives the devnode - which is the same category error the service
        # verdict made with SPSVCINST_ASSOCSERVICE, caught one review round
        # later here: an unconditional HKLM write puts its value there exactly as
        # an unconditional HKR write does, and *what removing the device does to
        # it afterwards* is uninstall behaviour this file does not model. The
        # root is in the row; a row outside HKR is a row whose lifetime the run
        # has to look at, and `docs\contributing\runs\run-11v.md` stage B4 is where that is looked
        # at.
        $v = ConvertTo-CopyFlags $FlagText
        if ($null -eq $v) { return "review" }
        # The type lives in the high half (FLG_ADDREG_TYPE_MASK) plus
        # FLG_ADDREG_BINVALUETYPE (0x1); only the operation bits below matter to
        # ownership, and an unrecognised operation bit is reported.
        #
        # **Recognising a flag is not the same as modelling it**, and a first
        # draft admitted APPEND and KEYONLY into the "known" set and then let
        # them fall through to `remove`. APPEND may leave pre-existing
        # REG_MULTI_SZ elements in the value, so this install did not write all
        # of what is there; KEYONLY creates the key and ignores the value the row
        # names, so the row's own subject was never written. Neither is `remove`,
        # and neither is confidently anything else, so both are `review`.
        $op = $v -band 0x0000FFFE
        if ($op -band (-bnot ($FLG_ADDREG_NOCLOBBER -bor $FLG_ADDREG_DELVAL -bor `
                              $FLG_ADDREG_APPEND -bor $FLG_ADDREG_KEYONLY -bor `
                              $FLG_ADDREG_OVERWRITEONLY))) {
            return "review"
        }
        if ($op -band $FLG_ADDREG_DELVAL) { return "none" }
        # NOCLOBBER is the registry twin of COPYFLG_NO_OVERWRITE and
        # OVERWRITEONLY the twin of COPYFLG_REPLACEONLY: each writes only on one
        # answer to "was it already there", so on the other answer this install
        # did not write what is present. Both are `keep`. A draft had NOCLOBBER
        # as `review` while its file-side twin was `keep`, which is the same
        # condition classified two ways in one derivation.
        if ($op -band ($FLG_ADDREG_OVERWRITEONLY -bor $FLG_ADDREG_NOCLOBBER)) { return "keep" }
        # APPEND and KEYONLY are genuinely different: neither is conditional on
        # prior state in the same way. APPEND may leave pre-existing elements
        # inside a value it also wrote to, and KEYONLY never writes the value the
        # row names at all - so what is present is partly or wholly not what this
        # row describes, and neither `remove` nor `keep` states that.
        if ($op -band ($FLG_ADDREG_APPEND -bor $FLG_ADDREG_KEYONLY)) { return "review" }
        return "remove"
    }

    # AddService's own flags field, values from SETUPAPI.H.
    #
    # **The question this column answers is "did this install write what is
    # there now", and it is easy to drift off it.** A draft made a missing
    # `SPSVCINST_ASSOCSERVICE` a `review`, reasoning that an unassociated service
    # is not taken away by removing the devnode. The header defines that bit only
    # as marking the service the device's **function driver** - it states no
    # deletion rule at all - so the reasoning was both unsupported *and* about
    # *uninstall behaviour*, which this file says explicitly it does not model,
    # smuggled into a column defined as ownership. An unconditional `AddService`
    # naming a section that exists **wrote the service**, associated or not, so
    # it is `remove`; the flags field is emitted beside it so a reader can see
    # the association for themselves, and what devnode removal does to the
    # service is a **measurement** the run makes (`docs/contributing/runs/run-11v.md`, stage B4).
    #
    # `SPSVCINST_DELETEEVENTLOGENTRY` (0x04) and `SPSVCINST_STOPSERVICE` (0x200)
    # are `DelService` flags; on an `AddService` row they mean nothing this
    # derivation can act on, so they are reported rather than passed over. Every
    # `SPSVCINST_NOCLOBBER_*` bit makes one of the service-key values conditional
    # on what was already there - the service-side form of FLG_ADDREG_NOCLOBBER,
    # and unlike association that *is* an ownership question.
    $SPSVCINST_TAGTOFRONT          = 0x00000001
    $SPSVCINST_ASSOCSERVICE        = 0x00000002
    $SPSVCINST_DELETEEVENTLOGENTRY = 0x00000004
    $SPSVCINST_STOPSERVICE         = 0x00000200
    $fpSvcNoClobber = 0x00000008 -bor 0x00000010 -bor 0x00000020 -bor 0x00000040 -bor `
                      0x00000080 -bor 0x00000100
    # Bits this derivation acts on at all. Anything outside is untaught.
    $fpSvcKnown = $SPSVCINST_TAGTOFRONT -bor $SPSVCINST_ASSOCSERVICE -bor `
                  $SPSVCINST_DELETEEVENTLOGENTRY -bor $SPSVCINST_STOPSERVICE -bor `
                  $fpSvcNoClobber

    function Get-FootprintServiceVerdict {
        param([string]$FlagText, [bool]$InstallSectionPresent)
        if (-not $InstallSectionPresent) { return "review" }
        $v = ConvertTo-CopyFlags $FlagText
        if ($null -eq $v) { return "review" }
        if ($v -band (-bnot $fpSvcKnown)) { return "review" }
        if ($v -band ($SPSVCINST_DELETEEVENTLOGENTRY -bor $SPSVCINST_STOPSERVICE)) { return "review" }
        if ($v -band $fpSvcNoClobber) { return "review" }
        # SPSVCINST_ASSOCSERVICE is deliberately NOT tested. See the note above:
        # it marks the function driver, and any bearing it has is on what
        # uninstall does rather than on what this install wrote.
        return "remove"
    }

    # Where a CopyFiles section with no [DestinationDirs] entry of its own
    # lands, and where a CopyFiles=@file lands - both fall back to
    # DefaultDestDir. Resolving it here means the dirid column is always the
    # answer rather than sometimes the name of the question. (BOTH-DESTDIR
    # separately fails a *section* that relies on the fallback; a bare @file
    # legitimately has nowhere else to get its destination from.)
    $fpDefaultDirid = "?"
    $fpDefaultSubdir = ""
    if ($destDirs.ContainsKey("defaultdestdir")) {
        $fpDefaultDirid = $destDirs["defaultdestdir"].Dirid
        $fpDefaultSubdir = $destDirs["defaultdestdir"].Subdir
    }

    $fpFileRows = 0
    $fpRegRows = 0
    foreach ($p in $fpPaths) {
        if (-not (Test-SectionExists $inf $p.Install)) { continue }
        $fpLines += ("path|{0}|{1}|{2}" -f $p.Os, $p.Kind, $p.Install)

        foreach ($cf in @(Get-Directive $inf $p.Install "CopyFiles")) {
            # CopyFiles=@filename is a single file with no section of its own,
            # so it has no [DestinationDirs] entry either and lands in
            # DefaultDestDir. It carries no flags field at all, which is a
            # plain overwriting copy - the same verdict a zero flags field gets.
            if ($cf.StartsWith("@")) {
                $one = $cf.Substring(1)
                $fpLines += ("file|{0}|@|{1}|{2}|{3}|{3}||0|remove" -f `
                    $p.Os, $fpDefaultDirid, $fpDefaultSubdir, $one)
                $fpFileRows++
                continue
            }
            $ddKey = $cf.ToLowerInvariant()
            $dirid = $fpDefaultDirid
            $subdir = $fpDefaultSubdir
            if ($destDirs.ContainsKey($ddKey)) {
                $dirid = $destDirs[$ddKey].Dirid
                $subdir = $destDirs[$ddKey].Subdir
            }
            $entries = Get-Section $inf $cf
            if ($null -eq $entries) { continue }
            foreach ($e in $entries) {
                $f = @($e.Text -split ',' | ForEach-Object { $_.Trim() })
                $dst = $f[0]
                if ($dst -eq "") { continue }
                $src = $dst
                if ($f.Count -gt 1 -and $f[1] -ne "") { $src = $f[1] }
                $tmp = ""
                if ($f.Count -gt 2) { $tmp = $f[2] }
                $flagText = ""
                if ($f.Count -gt 3) { $flagText = $f[3] }
                $verdict = Get-FootprintCopyVerdict $flagText
                $shownFlags = if ($flagText -eq "") { "0" } else { $flagText }
                $fpLines += ("file|{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}" -f `
                    $p.Os, $cf, $dirid, $subdir, $dst, $src, $tmp, $shownFlags, $verdict)
                $fpFileRows++
            }
        }

        foreach ($ar in @(Get-Directive $inf $p.Install "AddReg")) {
            $entries = Get-Section $inf $ar
            if ($null -eq $entries) { continue }
            foreach ($e in $entries) {
                $f = @($e.Text -split ',')
                $root = $f[0].Trim()
                $subkey = ""
                $value = ""
                $flags = ""
                $data = ""
                if ($f.Count -gt 1) { $subkey = $f[1].Trim() }
                if ($f.Count -gt 2) { $value = $f[2].Trim() }
                if ($f.Count -gt 3) { $flags = $f[3].Trim() }
                # The data field is last and may itself contain commas
                # (REG_MULTI_SZ, REG_BINARY), so it is rejoined verbatim rather
                # than taken as one more split field.
                if ($f.Count -gt 4) { $data = (($f[4..($f.Count - 1)]) -join ',').Trim() }
                $verdict = Get-FootprintRegVerdict $root $flags
                $fpLines += ("reg|{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}" -f `
                    $p.Os, $ar, $root, $subkey, $value, $flags, $data, $verdict)
                $fpRegRows++
            }
        }

        # A service is a registry key with values in it, not one binary path.
        # An earlier draft emitted only ServiceBinary and called the footprint
        # complete, which it was not: the setup engine writes DisplayName,
        # ServiceType, StartType, ErrorControl and LoadOrderGroup into the
        # service key too, and a footprint an uninstall is checked against has
        # to name them.
        $svcSection = ("{0}.Services" -f $p.Install)
        if (Test-SectionExists $inf $svcSection) {
            foreach ($row in @(Get-DirectiveRaw $inf $svcSection "AddService")) {
                $f = @($row.Value -split ',' | ForEach-Object { $_.Trim() })
                $svcName = $f[0]
                $svcFlags = ""
                if ($f.Count -gt 1) { $svcFlags = $f[1] }
                $svcInstall = ""
                if ($f.Count -gt 2) { $svcInstall = $f[2] }
                $havePresent = ($svcInstall -ne "" -and (Test-SectionExists $inf $svcInstall))
                # An AddService whose install section does not exist is a gap in
                # this derivation, not a service it can describe - and the
                # emitter is written before the verdict, so the gate's own
                # cross-reference failure is not visible in this file.
                if (-not $havePresent) {
                    $fpLines += ("unmodelled|{0}|{1}|{2}" -f $p.Os, $svcSection, `
                        ("AddService " + $svcName + " -> missing section '" + $svcInstall + "'"))
                }
                $svcVerdict = Get-FootprintServiceVerdict $svcFlags $havePresent
                $shownSvcFlags = if ($svcFlags -eq "") { "0" } else { $svcFlags }
                $fpLines += ("service|{0}|{1}|{2}|{3}|{4}" -f `
                    $p.Os, $svcName, $svcInstall, $shownSvcFlags, $svcVerdict)
                if ($havePresent) {
                    foreach ($se in @(Get-Section $inf $svcInstall)) {
                        if ($se.Text -notmatch '^\s*([^=]+?)\s*=\s*(.*)$') { continue }
                        $fpLines += ("servicevalue|{0}|{1}|{2}|{3}|{4}" -f `
                            $p.Os, $svcName, $svcInstall, $matches[1].Trim(), $matches[2].Trim())
                    }
                }
            }
            # Anything in the .Services section that is not an AddService - a
            # DelService, a second directive entirely - is a gap in this
            # derivation and says so. The install section below gets the same
            # treatment, and an earlier draft scanned only that one.
            foreach ($se in @(Get-Section $inf $svcSection)) {
                if ($se.Text -notmatch '^\s*([^=]+?)\s*=') { continue }
                $key = $matches[1].Trim()
                if ($key -eq "AddService") { continue }
                $fpLines += ("unmodelled|{0}|{1}|{2}" -f $p.Os, $svcSection, $key)
            }
        }

        # Anything else the install section directs. Emitted rather than
        # ignored: a DelReg or DelFiles added later would otherwise vanish from
        # a document whose entire job is to be complete. Include= and Needs=
        # land here too - this emitter does not follow them, and an INF that
        # grows one has a footprint reaching past what is derived here.
        $installEntries = Get-Section $inf $p.Install
        if ($null -ne $installEntries) {
            foreach ($e in $installEntries) {
                if ($e.Text -notmatch '^\s*([^=]+?)\s*=') { continue }
                $key = $matches[1].Trim()
                if (@("AddReg", "CopyFiles") -contains $key) { continue }
                $fpLines += ("unmodelled|{0}|{1}|{2}" -f $p.Os, $p.Install, $key)
            }
        }

        # Sub-sections the engine runs by name rather than by directive:
        # .HW, .CoInstallers, .Interfaces, .LogConfigOverride and any other
        # `<install>.<suffix>`. Only .Services is derived above, and a platform
        # decoration (.NTx86) is another install path, not a sub-section. The
        # rest place registry entries this file cannot show, so they are
        # reported rather than left out of a document meant to be complete.
        $installPrefix = ($p.Install + ".").ToLowerInvariant()
        foreach ($secName in @($inf.SectionOrder)) {
            $lower = $secName.ToLowerInvariant()
            if (-not $lower.StartsWith($installPrefix)) { continue }
            $suffix = $secName.Substring($installPrefix.Length)
            if ($suffix -eq "" -or $suffix.Contains(".")) { continue }
            if ($suffix -ieq "Services" -or $suffix -match '^(?i)NT') { continue }
            $fpLines += ("unmodelled|{0}|{1}|{2}" -f $p.Os, $secName, "engine-implied sub-section")
        }
    }

    Write-AsciiFile -Path $EmitFootprint -Lines $fpLines
    Write-Host ("install footprint ({0} file row(s), {1} registry row(s)) written to {2}" -f `
        $fpFileRows, $fpRegRows, $EmitFootprint)
}

# ---- optional staged-package check ---------------------------------

if ($PackageDir -ne "") {
    if (-not (Test-Path -LiteralPath $PackageDir)) {
        Add-Failure "PKG-LAYOUT" ("-PackageDir '{0}' does not exist." -f $PackageDir)
    } else {
        $pkg = (Resolve-Path -LiteralPath $PackageDir).Path
        foreach ($file in $sourceFiles.Keys) {
            $full = Join-Path $pkg $sourceFiles[$file].Relative
            if (-not (Test-Path -LiteralPath $full)) {
                Add-Failure "PKG-LAYOUT" ("[SourceDisksFiles] lists '{0}' but '{1}' is not in the staged package." -f $file, $full)
            }
        }
        # Nothing in the package may be a Microsoft file, under its own name or
        # a 1.0.0.0 media name, at the root or below it. The rules above prove
        # the INF asks the OS for usbd.sys and usbhub.sys; this proves the
        # package has not started carrying them again (legal-provenance.md
        # section 5).
        foreach ($item in (Get-ChildItem -LiteralPath $pkg -File -Recurse)) {
            $n = $item.Name.ToLowerInvariant()
            if ($osSuppliedNames -contains $n -or $retiredMediaNames -contains $n) {
                Add-Failure "PKG-MSFILE" ("the staged package holds '{0}'. The media carries no Microsoft file since 1.0.0.1; usbd.sys and usbhub.sys come from the OS through LayoutFile, so take it out." -f $item.FullName.Substring($pkg.Length).TrimStart('\'))
            }
        }

        if ($script:failures.Count -eq 0) {
            Write-Ok ("staged package '{0}' has every file [SourceDisksFiles] names and no Microsoft file" -f $pkg)
        }
    }
}

# ---- summary -------------------------------------------------------

Write-Host ""
Write-Host ("sections: {0}   models: {1}   copied files: {2}" -f $inf.SectionOrder.Count, $models.Count, (@($copiedFiles | Sort-Object -Unique) -join ', '))

if ($script:failures.Count -gt 0) {
    Write-Err ("INF gate FAILED: {0} problem(s)." -f $script:failures.Count)
    Write-Host "Do not install this INF. Win98's setup engine has no log at all, and a"
    Write-Host "Win2000 install with no service looks the same in Device Manager as a"
    Write-Host "driver that loaded and failed (docs\contributing\build-and-test.md)."
    exit 1
}

if ($script:warnings.Count -gt 0) {
    Write-Warn ("INF gate passed with {0} warning(s) - read them." -f $script:warnings.Count)
} else {
    Write-Ok "INF gate PASSED"
}
exit 0
