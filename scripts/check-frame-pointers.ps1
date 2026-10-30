<#
.SYNOPSIS
Assert that an x86 release build of xhci98.sys, xhciuas.sys or xhcicm.sys
was compiled with frame pointers kept (/Oy-).

.DESCRIPTION
Roadmap-usb4 task 45.45 retired the debug flavour (design record 08 section
14). What a user gave up by installing release instead was the checked
build's frame pointers on the MSVC 6.0 x86 builds, so the release flavour
keeps them: each of the three drivers' sources sets NTNOFPO=1 in its fre
branch, which the Windows 2000 DDK's i386mk.inc reads to compile /Oy-
where a free build would compile /Oy.

That is a statement about the build files, and this checks what the build
did: the DDK's build log prints each compiler command line, and every one
must carry /Oy- and none /Oy alone. A sources edit that loses the setting,
or a DDK that ignores it, fails here rather than shipping an image without
frame pointers. build-driver.cmd runs it on the x86 release build of each
of the three drivers; the amd64 builds (WDK 7.1) have no such switch.

.PARAMETER Log
The build log (src\buildfre.log, src\uas\buildfre.log, src\cm\buildfre.log).

.PARAMETER SelfTest
Check the rule on synthetic logs and exit.
#>

[CmdletBinding()]
param(
    [string]$Log = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

function Get-FramePointerFaults {
    # The reasons a log fails, one string each; none when it passes.
    param([string[]]$Lines)
    $faults = @()
    $compiles = 0
    foreach ($line in $Lines) {
        if ($line -notmatch '(^|\s|>)cl\s+-nologo\b') { continue }
        $compiles++
        $tokens = @($line -split '\s+')
        if ($tokens -notcontains '/Oy-') {
            $faults += "a compile without /Oy-: " + $line.Substring(0, [Math]::Min(160, $line.Length))
        }
        if ($tokens -contains '/Oy') {
            $faults += "a compile with /Oy (frame pointers omitted): " + $line.Substring(0, [Math]::Min(160, $line.Length))
        }
    }
    if ($compiles -eq 0) {
        $faults += "no compiler command line (cl -nologo ...) in the log, so nothing says how the image was compiled."
    }
    return $faults
}

if ($SelfTest) {
    $bad = 0
    $kept = 'cl -nologo -Ii386\ -I. /c /Zel /Zp8 /Gy /W3 /Gz /QIfdiv- /QIf /G5 /Oxs /Oy- -DFPO=0 /Zi .\hcd_ctl.c'
    $omit = 'cl -nologo -Ii386\ -I. /c /Zel /Zp8 /Gy /W3 /Gz /QIfdiv- /QIf /G5 /Oxs /Oy -DFPO=1 .\hcd_ctl.c'
    $both = 'cl -nologo -Ii386\ -I. /c /Oxs /Oy /Oy- .\hcd_ctl.c'
    $cases = @(
        @{ Name = "every compile /Oy-"; Lines = @("BUILD: Compiling", $kept, $kept); Pass = $true },
        @{ Name = "a compile with /Oy"; Lines = @($kept, $omit); Pass = $false },
        @{ Name = "a compile with neither"; Lines = @('cl -nologo /c /Oxs .\x.c'); Pass = $false },
        @{ Name = "a compile with both"; Lines = @($both); Pass = $false },
        @{ Name = "a log with no compile"; Lines = @("BUILD: Examining", "link -out:xhci98.sys"); Pass = $false },
        @{ Name = "a numbered build line"; Lines = @("1>" + $kept); Pass = $true }
    )
    foreach ($c in $cases) {
        $f = @(Get-FramePointerFaults -Lines $c.Lines)
        $passed = ($f.Count -eq 0)
        if ($passed -ne $c.Pass) {
            Write-Host ("  FAIL: {0}: expected {1}, got {2} ({3})" -f $c.Name, $c.Pass, $passed, ($f -join "; "))
            $bad++
        }
    }
    if ($bad -ne 0) {
        Write-Host "frame-pointer check self-test: FAILED"
        exit 1
    }
    Write-Host ("frame-pointer check self-test: OK ({0} cases)" -f $cases.Count)
    exit 0
}

if ($Log -eq "" -or -not (Test-Path -LiteralPath $Log -PathType Leaf)) {
    Write-Host "ERROR: no build log at '$Log'."
    exit 1
}
$faults = @(Get-FramePointerFaults -Lines @(Get-Content -LiteralPath $Log))
if ($faults.Count -eq 0) {
    Write-Host ("  frame pointers kept (/Oy-): {0}" -f $Log)
    exit 0
}
foreach ($f in $faults) { Write-Host ("  {0}" -f $f) }
Write-Host ("ERROR: '{0}' is not a release build with frame pointers kept. Each driver's sources sets NTNOFPO=1 in its fre branch (task 45.45, design record 08 section 14)." -f $Log)
exit 1
