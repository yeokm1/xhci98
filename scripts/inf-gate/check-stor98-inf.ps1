<#
.SYNOPSIS
Hold Stor98's two INFs to the package's one version and date.

.DESCRIPTION
Since 3.1.0.0 (roadmap-usb4 task 42-A.30, owner 2026-10-19) every INF of the
package carries the same DriverVer, from src\xhci_version.h: XHCI_DRIVERVER_DATE
and XHCI_VER_STR. The project's own eight INFs are held to it by check-inf.ps1,
check-uas-inf.ps1 and check-cm-inf.ps1; this holds Stor98's two,
src\stor98\inf\stor98-x86.inf and stor98-x64.inf, which the package stages as
stor98.inf. Each must have exactly one DriverVer line, zero-padded
MM/DD/YYYY, equal to the header's date and version; its Signature must be
$CHICAGO$ for the x86 INF (98 SE and ME read it) and $Windows NT$ for the x64
one. scripts\build-driver.cmd runs it with the other INF gates, after
-SelfTest.

.PARAMETER Path
One INF to check instead of both (the self-test's mutants).

.PARAMETER SelfTest
Check that the gate refuses a wrong date, a wrong version, an unpadded date
and a second DriverVer line, and accepts the committed INFs.
#>

[CmdletBinding()]
param(
    [string]$Path = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Get-Want {
    $h = [System.IO.File]::ReadAllText((Join-Path $repo "src\xhci_version.h"))
    $v = [regex]::Match($h, '(?m)^#define\s+XHCI_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    $d = [regex]::Match($h, '(?m)^#define\s+XHCI_DRIVERVER_DATE\s+"(\d\d/\d\d/\d{4})"')
    if (-not $v.Success -or -not $d.Success) { throw "src\xhci_version.h has no XHCI_VER_STR or no padded XHCI_DRIVERVER_DATE." }
    return "{0},{1}" -f $d.Groups[1].Value, $v.Groups[1].Value
}

function Get-InfFault {
    # "" when the INF is right, else why not.
    param([string]$File, [string]$Signature, [string]$Want)
    $t = [System.IO.File]::ReadAllText($File)
    $dv = [regex]::Matches($t, '(?im)^[ \t]*DriverVer[ \t]*=[ \t]*([^\r\n;]*?)[ \t]*(;[^\r\n]*)?\r?$')
    if ($dv.Count -ne 1) { return "it has $($dv.Count) DriverVer lines, not one" }
    $got = ($dv[0].Groups[1].Value -replace '\s', '')
    if ($got -cne $Want) { return "its DriverVer is '$got', and src\xhci_version.h says '$Want'" }
    $sig = [regex]::Match($t, '(?im)^[ \t]*Signature[ \t]*=[ \t]*"([^"]*)"')
    if (-not $sig.Success -or $sig.Groups[1].Value -ne $Signature) { return "its Signature is not `"$Signature`"" }
    return ""
}

try {
    $want = Get-Want
    $infs = @(
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x86.inf"); Sig = '$CHICAGO$' },
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x64.inf"); Sig = '$Windows NT$' }
    )
    if ($SelfTest) {
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("stor98-inf-" + [System.IO.Path]::GetRandomFileName())
        New-Item -ItemType Directory -Path $tmp | Out-Null
        try {
            $base = [System.IO.File]::ReadAllText($infs[0].File)
            $parts = $want -split ','
            $d = $parts[0] -split '/'
            $unpadded = "{0}/{1}/{2}" -f [int]$d[0], [int]$d[1], $d[2]
            $cases = @(
                @{ T = $base; Ok = $true; Why = "the committed x86 INF" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=01/01/2000," + $parts[1]); Ok = $false; Why = "another date" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=" + $parts[0] + ",0.4.0.0"); Ok = $false; Why = "another version" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$unpadded," + $parts[1]); Ok = ($unpadded -eq $parts[0]); Why = "an unpadded date" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$want`r`nDriverVer=$want"); Ok = $false; Why = "two DriverVer lines" },
                @{ T = $base.Replace('"$CHICAGO$"', '"$Windows NT$"'); Ok = $false; Why = "the wrong Signature" }
            )
            $i = 0
            foreach ($c in $cases) {
                $f = Join-Path $tmp ("m{0}.inf" -f $i); $i++
                [System.IO.File]::WriteAllText($f, $c.T, (New-Object System.Text.ASCIIEncoding))
                $why = Get-InfFault -File $f -Signature '$CHICAGO$' -Want $want
                if (($why -eq "") -ne $c.Ok) { throw "self-test: $($c.Why) was $(if ($c.Ok) { 'refused: ' + $why } else { 'accepted' })." }
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "  check-stor98-inf.ps1 self-test: $($cases.Count) cases"
        exit 0
    }
    if ($Path -ne "") { $infs = @(@{ File = $Path; Sig = '$CHICAGO$' }) }
    $bad = 0
    foreach ($inf in $infs) {
        $why = Get-InfFault -File $inf.File -Signature $inf.Sig -Want $want
        if ($why -ne "") { Write-Host ("  {0}: {1}" -f $inf.File, $why); $bad++ }
        else { Write-Host ("  {0}: DriverVer={1}" -f (Split-Path -Leaf $inf.File), $want) }
    }
    if ($bad -gt 0) { exit 1 }
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message)
    exit 1
}
