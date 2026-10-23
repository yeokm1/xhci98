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
one. The x64 INF names its catalog, CatalogFile=stor98.cat, once, which the
packager makes and test-signs (design record 19, owner 2026-10-22: Windows
8 to 10 x64 refuse a package without one in test-signing mode), and writes
DmaRemappingCompatible=1 under its service's Parameters key (Kernel DMA
Protection, docs/PLAN.md); the x86 INF names no catalog. On Windows 8.1 and
10 (the models decorated NTx86.6.3 and NTamd64.6.3) each INF matches only
PCI\CC_0108, never PCI\CC_010802, so Windows' own stornvme outranks it and
Stor98 goes only where the installer puts it (roadmap-usb4 43.59, owner
2026-10-23); its other models keep PCI\CC_010802. scripts\build-driver.cmd
runs it with the other INF gates, after -SelfTest.

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
    $cat = @([regex]::Matches($t, '(?im)^[ \t]*CatalogFile(\.[^ \t=]*)?[ \t]*=[ \t]*([^\r\n;]*?)[ \t]*(;[^\r\n]*)?\r?$'))
    if ($Signature -eq '$Windows NT$') {
        if ($cat.Count -ne 1 -or $cat[0].Groups[1].Value -ne "" -or $cat[0].Groups[2].Value -cne "stor98.cat") {
            return "it does not name its catalog once as CatalogFile=stor98.cat (design record 19: the packager makes and signs it)"
        }
        if ([regex]::Matches($t, '(?im)^HKR,"Parameters","DmaRemappingCompatible",%REG_DWORD%,0x00000001\r?$').Count -ne 1) {
            return "it does not write DmaRemappingCompatible=1 once under the service's Parameters key (docs/PLAN.md, Kernel DMA Protection)"
        }
    } elseif ($cat.Count -ne 0) {
        return "it names a catalog, and the x86 package carries none (design record 19)"
    }
    # The NT 6.3 models: PCI\CC_0108 alone, and the earlier ones the class match.
    $dec = if ($Signature -eq '$Windows NT$') { 'NTamd64.6.3' } else { 'NTx86.6.3' }
    $mline = [regex]::Match($t, '(?im)^[ \t]*%Mfg%[ \t]*=[ \t]*([^\r\n;]*)')
    if (-not $mline.Success) { return "it has no %Mfg% line in [Manufacturer]" }
    $parts = @($mline.Groups[1].Value.Split(',') | ForEach-Object { $_.Trim() })
    if ($parts -notcontains $dec) { return "its [Manufacturer] line does not name the $dec models (8.1 and 10 keep stornvme)" }
    function Get-ModelIds([string]$Name) {
        $s = [regex]::Match($t, '(?ims)^[ \t]*\[' + [regex]::Escape($Name) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if (-not $s.Success) { return $null }
        $ids = @()
        foreach ($l in [regex]::Matches($s.Groups[1].Value, '(?m)^[ \t]*[^;\s][^=\r\n]*=([^\r\n;]*)')) {
            $f = @($l.Groups[1].Value.Split(',') | ForEach-Object { $_.Trim() })
            if ($f.Count -gt 1) { $ids += $f[1..($f.Count - 1)] }
        }
        return , $ids
    }
    $new = Get-ModelIds ($parts[0] + "." + $dec)
    if ($null -eq $new) { return "it has no [$($parts[0]).$dec] section" }
    if ($new.Count -ne 1 -or $new[0] -ne 'PCI\CC_0108') { return "its $dec models match '$($new -join ',')', not PCI\CC_0108 alone (8.1 and 10 keep stornvme)" }
    $old = Get-ModelIds $(if ($Signature -eq '$Windows NT$') { $parts[0] + ".NTamd64" } else { $parts[0] })
    if ($null -eq $old -or $old -notcontains 'PCI\CC_010802') { return "its models for Windows 8 and earlier do not match PCI\CC_010802" }
    if ($Signature -eq '$Windows NT$') {
        # The 64-bit INF: no undecorated models section, empty or not, for one
        # the [Manufacturer] line names - xhci98's installer refuses such an
        # INF (inst_inf_arch_ok), and with it every x64 install (2026-10-19).
        $mfg = [regex]::Match($t, '(?ims)^[ \t]*\[Manufacturer\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if (-not $mfg.Success) { return "it has no [Manufacturer] section" }
        foreach ($m in [regex]::Matches($mfg.Groups[1].Value, '(?m)^[ \t]*[^;\s][^=\r\n]*=[ \t]*([^,;\r\n]+)')) {
            $name = $m.Groups[1].Value.Trim()
            if ([regex]::IsMatch($t, '(?im)^[ \t]*\[' + [regex]::Escape($name) + '\][ \t]*\r?$')) {
                return "it has an undecorated models section [$name], which xhci98's 64-bit installer refuses"
            }
        }
    }
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
                @{ T = $base.Replace('"$CHICAGO$"', '"$Windows NT$"'); Ok = $false; Why = "the wrong Signature" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$want`r`nCatalogFile=stor98.cat"); Ok = $false; Why = "a catalog in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3", "%Mfg%=Stor98_Models"); Ok = $false; Why = "no NT 6.3 models in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_0108", "[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match on 8.1 and 10 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_0108", "[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_0108,PCI\CC_010802"); Ok = $false; Why = "a second id on 8.1 and 10 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_0108"); Ok = $false; Why = "no class match before 8.1 in the x86 INF" }
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
        $b64 = [System.IO.File]::ReadAllText($infs[1].File)
        $c64 = @(
            @{ T = $b64; Ok = $true; Why = "the committed x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64]", "[Stor98_Models]`r`n`r`n[Stor98_Models.NTamd64]"); Ok = $false; Why = "an empty undecorated models section in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat`r`n", ""); Ok = $false; Why = "no catalog in the x64 INF" },
            @{ T = $b64.Replace("%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3", "%Mfg%=Stor98_Models,NTamd64"); Ok = $false; Why = "no NT 6.3 models in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_0108", "[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match on 8.1 and 10 in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile=other.cat"); Ok = $false; Why = "another catalog name in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile.NTamd64=stor98.cat"); Ok = $false; Why = "a decorated catalog line in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile=stor98.cat`r`nCatalogFile=stor98.cat"); Ok = $false; Why = "two catalog lines in the x64 INF" },
            @{ T = $b64.Replace('HKR,"Parameters","DmaRemappingCompatible",%REG_DWORD%,0x00000001', 'HKR,"Parameters","DmaRemappingCompatible",%REG_DWORD%,0x00000002'); Ok = $false; Why = "DmaRemappingCompatible other than 1" },
            @{ T = $b64.Replace('HKR,"Parameters","DmaRemappingCompatible",%REG_DWORD%,0x00000001', ''); Ok = $false; Why = "no DmaRemappingCompatible" }
        )
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("stor98-inf-" + [System.IO.Path]::GetRandomFileName())
        New-Item -ItemType Directory -Path $tmp | Out-Null
        try {
            $i = 0
            foreach ($c in $c64) {
                $f = Join-Path $tmp ("x{0}.inf" -f $i); $i++
                [System.IO.File]::WriteAllText($f, $c.T, (New-Object System.Text.ASCIIEncoding))
                $why = Get-InfFault -File $f -Signature '$Windows NT$' -Want $want
                if (($why -eq "") -ne $c.Ok) { throw "self-test: $($c.Why) was $(if ($c.Ok) { 'refused: ' + $why } else { 'accepted' })." }
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "  check-stor98-inf.ps1 self-test: $($cases.Count + $c64.Count) cases"
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
