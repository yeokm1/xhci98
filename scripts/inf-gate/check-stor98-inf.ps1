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
no DmaRemappingCompatible (with it 1, Windows 10 x64 does not start Stor98:
docs/PLAN.md, 2026-10-23) and deletes one an earlier build wrote, in the
service's DelReg; the x86 INF names no catalog. The models before NT
6.3 (the x86 INF's undecorated section, which Windows 2000 reads too; the
x64 INF's NTamd64) match the tunnel id PCI\XHCI98_TUNNEL&CC_010802 alone,
as the hardware id, which xhci98's connection manager gives a drive behind
a Thunderbolt or USB4 tunnel (design record 31 section 4.7), so Windows
2000 to 8 never offer Stor98 for an internal NVMe drive. The NT 6.3 models
(8.1 and 10) are 3.2.0.0's: PCI\CC_010802 as the compatible id, hardware id
empty, below stornvme on an internal drive (roadmap-usb4 43.59; record 31
section 4.8, owner 2026-10-25: those systems restart a device with its ids
read again, which loses the tunnel id). No PCI\CC_ id may appear in any
other model line. The third INF, src\stor98\inf\stor98-9x.inf (staged as stor98w9.inf
and installed on 98 SE and ME alone), keeps the class match beside the id
(design record 31 section 5.2, owner 2026-10-25): one undecorated models
section with the tunnel id and PCI\CC_010802 as its compatible id, no
decoration, no NT section, stor98.mpd its one file, no catalog. The
[Manufacturer] section must hold exactly one line, with exactly those
decorations, so no other decoration can add a models section unchecked. scripts\build-driver.cmd
runs it with the other INF gates, after -SelfTest.

.PARAMETER Path
One INF to check instead of both (the self-test's mutants).

.PARAMETER SelfTest
Check that the gate refuses a wrong date, a wrong version, an unpadded date,
a second DriverVer line and a wrong model shape, and accepts the committed
INFs.
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
        # DmaRemappingCompatible (with it Windows 10 x64 does not start Stor98,
        # docs/PLAN.md, 2026-10-23) may be named once only, %tokens% of
        # [Strings] resolved and comments dropped, whatever the root or the
        # form: by the DelReg entry of the service-install section, which
        # removes a value an earlier 3.2.0.0 build wrote.
        $strings = @{}
        $sm = [regex]::Match($t, '(?ims)^[ \t]*\[Strings\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if ($sm.Success) {
            foreach ($m in [regex]::Matches($sm.Groups[1].Value, '(?m)^[ \t]*([^;=\s][^=\r\n]*?)[ \t]*=[ \t]*"?([^"\r\n]*)"?')) { $strings[$m.Groups[1].Value.Trim().ToLowerInvariant()] = $m.Groups[2].Value }
        }
        $section = ''; $named = @()
        foreach ($raw in ($t -split '\r?\n')) {
            $line = ($raw -replace ';.*$', '').Trim()
            if ($line -match '^\[([^\]]+)\]$') { $section = $Matches[1].Trim(); continue }
            $res = [regex]::Replace($line, '%([^%]+)%', { param($x) $k = $x.Groups[1].Value.ToLowerInvariant(); if ($strings.ContainsKey($k)) { $strings[$k] } else { $x.Value } })
            if ($res -match '(?i)DmaRemappingCompatible') { $named += , @($section, $res) }
        }
        if ($named.Count -ne 1 -or $named[0][0] -ne 'Stor98_ServiceDelReg' -or $named[0][1] -cnotmatch '^HKR[ \t]*,[ \t]*"Parameters"[ \t]*,[ \t]*"DmaRemappingCompatible"$') {
            return "it names DmaRemappingCompatible other than once, in [Stor98_ServiceDelReg] as HKR,`"Parameters`",`"DmaRemappingCompatible`" (with the value Windows 10 x64 does not start Stor98, docs/PLAN.md 2026-10-23)"
        }
        # And that DelReg is the service-install section's own: anywhere else
        # its HKR is another key and the old value stays.
        $svc = [regex]::Match($t, '(?ims)^[ \t]*\[Stor98_Service\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if (-not $svc.Success -or -not [regex]::IsMatch($svc.Groups[1].Value, '(?im)^[ \t]*DelReg[ \t]*=[ \t]*Stor98_ServiceDelReg[ \t]*\r?$') -or
            [regex]::Matches($t, '(?im)^[ \t]*DelReg[ \t]*=[ \t]*[^\r\n]*Stor98_ServiceDelReg').Count -ne 1) {
            return "its [Stor98_Service] section (the AddService one) does not carry DelReg=Stor98_ServiceDelReg alone, so an update would keep DmaRemappingCompatible"
        }
    } elseif ($cat.Count -ne 0) {
        return "it names a catalog, and the x86 package carries none (design record 19)"
    }
    # The models (record 31 sections 4.7 and 4.8): one [Manufacturer] line,
    # exactly these decorations, and each section exactly one model line -
    # the tunnel id as the hardware id before NT 6.3, the class as the
    # compatible id (hardware id empty) from it; no class match elsewhere.
    # Windows merges sections of one name, so a second one would escape the
    # checks below: no section name may appear twice.
    $seen = @{}
    foreach ($h in [regex]::Matches($t, '(?m)^[ \t]*\[([^\]\r\n]+)\][ \t]*(;[^\r\n]*)?\r?$')) {
        $k = $h.Groups[1].Value.Trim().ToLowerInvariant()
        if ($seen.ContainsKey($k)) { return "its section [$($h.Groups[1].Value.Trim())] appears twice (Windows merges them)" }
        $seen[$k] = 1
    }
    $mfgSec = [regex]::Match($t, '(?ims)^[ \t]*\[Manufacturer\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    if (-not $mfgSec.Success) { return "it has no [Manufacturer] section" }
    $mlines = @([regex]::Matches($mfgSec.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value })
    if ($mlines.Count -ne 1 -or $mlines[0] -notmatch '^%Mfg%[ \t]*=[ \t]*(.+)$') { return "its [Manufacturer] section does not hold exactly one %Mfg% line" }
    $parts = @($Matches[1].Split(',') | ForEach-Object { $_.Trim() })
    $want = if ($Signature -eq '$Windows NT$') { 'Stor98_Models,NTamd64,NTamd64.6.3' } else { 'Stor98_Models,NTx86.6.3' }
    if (($parts -join ',') -cne $want) { return "its [Manufacturer] line is '$($parts -join ',')', not '$want' (record 31 section 4.7)" }
    $sec63 = if ($Signature -eq '$Windows NT$') { 'Stor98_Models.NTamd64.6.3' } else { 'Stor98_Models.NTx86.6.3' }
    foreach ($h in [regex]::Matches($t, '(?ims)^[ \t]*\[([^\]\r\n]+)\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')) {
        if ($h.Groups[1].Value.Trim() -ne $sec63 -and [regex]::IsMatch($h.Groups[2].Value, '(?im)^[ \t]*%Stor98\.DeviceDesc%[^\r\n;]*PCI\\CC_')) { return "a model line outside [$sec63] matches a PCI class id, which would offer Stor98 for an internal drive" }
    }
    $sections = if ($Signature -eq '$Windows NT$') { @(@('Stor98_Models.NTamd64', 0), @('Stor98_Models.NTamd64.6.3', 3)) } else { @(@('Stor98_Models', 0), @('Stor98_Models.NTx86.6.3', 3)) }
    foreach ($s in $sections) {
        $sm = [regex]::Match($t, '(?ims)^[ \t]*\[' + [regex]::Escape($s[0]) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if (-not $sm.Success) { return "it has no [$($s[0])] section" }
        $ml = @([regex]::Matches($sm.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' })
        $line = switch ($s[1]) { 3 { '%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802' } default { '%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802' } }
        if ($ml.Count -ne 1 -or $ml[0] -cne $line) { return "its [$($s[0])] section is not the one line '$line'" }
    }
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

function Get-Inf9xFault {
    # The 98 SE and ME INF: "" when right, else why not.
    param([string]$File, [string]$Want)
    $t = [System.IO.File]::ReadAllText($File)
    $dv = [regex]::Matches($t, '(?im)^[ \t]*DriverVer[ \t]*=[ \t]*([^\r\n;]*?)[ \t]*(;[^\r\n]*)?\r?$')
    if ($dv.Count -ne 1) { return "it has $($dv.Count) DriverVer lines, not one" }
    $got = ($dv[0].Groups[1].Value -replace '\s', '')
    if ($got -cne $Want) { return "its DriverVer is '$got', and src\xhci_version.h says '$Want'" }
    $sig = [regex]::Match($t, '(?im)^[ \t]*Signature[ \t]*=[ \t]*"([^"]*)"')
    if (-not $sig.Success -or $sig.Groups[1].Value -ne '$CHICAGO$') { return "its Signature is not `"`$CHICAGO`$`"" }
    if ([regex]::IsMatch($t, '(?im)^[ \t]*CatalogFile')) { return "it names a catalog" }
    $seen = @{}
    foreach ($h in [regex]::Matches($t, '(?m)^[ \t]*\[([^\]\r\n]+)\][ \t]*(;[^\r\n]*)?\r?$')) {
        $k = $h.Groups[1].Value.Trim().ToLowerInvariant()
        if ($seen.ContainsKey($k)) { return "its section [$($h.Groups[1].Value.Trim())] appears twice (Windows merges them)" }
        if ($k -match '\.nt') { return "it has a Windows NT section [$($h.Groups[1].Value.Trim())]; 2000 and later install from stor98.inf" }
        $seen[$k] = 1
    }
    $mfgSec = [regex]::Match($t, '(?ims)^[ \t]*\[Manufacturer\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    if (-not $mfgSec.Success) { return "it has no [Manufacturer] section" }
    $mlines = @([regex]::Matches($mfgSec.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' })
    if ($mlines.Count -ne 1 -or $mlines[0] -cne '%Mfg%=Stor98_Models') { return "its [Manufacturer] section is not the one undecorated line %Mfg%=Stor98_Models" }
    $sm = [regex]::Match($t, '(?ims)^[ \t]*\[Stor98_Models\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    $ml = @()
    if ($sm.Success) { $ml = @([regex]::Matches($sm.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' }) }
    $line = '%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802'
    if ($ml.Count -ne 1 -or $ml[0] -cne $line) { return "its [Stor98_Models] section is not the one line '$line'" }
    $sf = [regex]::Match($t, '(?ims)^[ \t]*\[SourceDisksFiles\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    $fl = @()
    if ($sf.Success) { $fl = @([regex]::Matches($sf.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' }) }
    if ($fl.Count -ne 1 -or $fl[0] -cne 'stor98.mpd=1') { return "its [SourceDisksFiles] is not stor98.mpd alone" }
    return ""
}

try {
    $want = Get-Want
    $infs = @(
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x86.inf"); Sig = '$CHICAGO$' },
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x64.inf"); Sig = '$Windows NT$' }
    )
    $inf9x = Join-Path $repo "src\stor98\inf\stor98-9x.inf"
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
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3", "%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0"); Ok = $false; Why = "a later decoration in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3", "%Mfg%=Stor98_Models,NTx86,NTx86.6.3"); Ok = $false; Why = "an NTx86 decoration in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3", "%Mfg%=Stor98_Models,NTx86.6.3`r`n%Mfg%=Other_Models"); Ok = $false; Why = "a second manufacturer line in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match back for 2000 to 8 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802"); Ok = $false; Why = "the class as a compatible id for 2000 to 8 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL"); Ok = $false; Why = "another id in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class as the hardware id on 8.1 and 10 in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_0108"); Ok = $false; Why = "the whole subclass on 8.1 and 10 in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802"); Ok = $false; Why = "the tunnel id on 8.1 and 10 in the x86 INF" }
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
            @{ T = $b64.Replace("%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3", "%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0"); Ok = $false; Why = "a later decoration in the x64 INF" },
            @{ T = $b64 + "`r`n[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802`r`n"; Ok = $false; Why = "a second NT 6.3 models section in the x64 INF" },
            @{ T = $b64 + "`r`n[Manufacturer]`r`n%Mfg%=Stor98_Models,NTamd64.10.0`r`n"; Ok = $false; Why = "a second [Manufacturer] section in the x64 INF" },
            @{ T = $b64 + "`r`n[Stor98_Models.NTamd64.6.3] ; more models`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802`r`n"; Ok = $false; Why = "a second NT 6.3 models section with a comment on its header" },
            @{ T = $b64 + "`r`n[Manufacturer] ; more`r`n%Mfg%=Stor98_Models,NTamd64.10.0`r`n"; Ok = $false; Why = "a second [Manufacturer] with a comment on its header" },
            @{ T = $b64.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class as the hardware id on 8.1 and 10 in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models.NTamd64]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match back in the x64 INF" },
            @{ T = $b64.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802"); Ok = $false; Why = "the tunnel id on 8.1 and 10 in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile=other.cat"); Ok = $false; Why = "another catalog name in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile.NTamd64=stor98.cat"); Ok = $false; Why = "a decorated catalog line in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat", "CatalogFile=stor98.cat`r`nCatalogFile=stor98.cat"); Ok = $false; Why = "two catalog lines in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_EventLog]", "HKR,`"Parameters`",`"DmaRemappingCompatible`",%REG_DWORD%,0x00000001`r`n`r`n[Stor98_EventLog]"); Ok = $false; Why = "DmaRemappingCompatible 1" },
            @{ T = $b64.Replace("[Stor98_EventLog]", "HKR, Parameters, DmaRemappingCompatible, 0x00010001, 2`r`n`r`n[Stor98_EventLog]"); Ok = $false; Why = "DmaRemappingCompatible 2, spaced" },
            @{ T = $b64.Replace("DelReg=Stor98_ServiceDelReg`r`n", ""); Ok = $false; Why = "no DelReg for DmaRemappingCompatible" },
            @{ T = $b64.Replace("HKR,`"Parameters`",`"DmaRemappingCompatible`"", "HKR,`"Para meters`",`"DmaRemappingCompatible`""); Ok = $false; Why = "a DelReg of another subkey (a space inside the quotes)" },
            @{ T = $b64.Replace("HKR,`"Parameters`",`"DmaRemappingCompatible`"", "HKR, `"Parameters`" , `"DmaRemappingCompatible`""); Ok = $true; Why = "spaces around the DelReg's commas" },
            @{ T = $b64.Replace("[Stor98_EventLog]", "HKLM,System\CurrentControlSet\Services\stor98\Parameters,DmaRemappingCompatible,0x00010001,1`r`n`r`n[Stor98_EventLog]"); Ok = $false; Why = "DmaRemappingCompatible through HKLM" },
            @{ T = $b64.Replace("[Stor98_EventLog]", "HKR,`"Parameters`",%Dmr%,%REG_DWORD%,1`r`n`r`n[Stor98_EventLog]").Replace("[Strings]", "[Strings]`r`nDmr=`"DmaRemappingCompatible`""); Ok = $false; Why = "DmaRemappingCompatible through a [Strings] token" },
            @{ T = $b64.Replace("AddReg=Stor98_ServiceReg`r`nDelReg=Stor98_ServiceDelReg`r`n", "AddReg=Stor98_ServiceReg`r`n").Replace("CopyFiles=Stor98_CopyFiles", "CopyFiles=Stor98_CopyFiles`r`nDelReg=Stor98_ServiceDelReg"); Ok = $false; Why = "the DelReg in the device's install section, not the service's" },
            @{ T = $b64.Replace("HKR,`"Parameters`",`"DmaRemappingCompatible`"`r`n", "HKR,`"Parameters`",`"Other`"`r`n"); Ok = $false; Why = "a DelReg of another value" }
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
        $b9 = [System.IO.File]::ReadAllText($inf9x)
        $c9 = @(
            @{ T = $b9; Ok = $true; Why = "the committed 9x INF" },
            @{ T = $b9.Replace("%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802"); Ok = $false; Why = "no class match in the 9x INF" },
            @{ T = $b9.Replace("%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "no tunnel id in the 9x INF" },
            @{ T = $b9.Replace("%Mfg%=Stor98_Models", "%Mfg%=Stor98_Models,NTx86"); Ok = $false; Why = "a decoration in the 9x INF" },
            @{ T = $b9 + "`r`n[Stor98_Inst.NTx86]`r`nCopyFiles=Stor98_Files_9x`r`n"; Ok = $false; Why = "an NT section in the 9x INF" },
            @{ T = $b9.Replace("stor98.mpd=1", "stor98.mpd=1`r`nstor98.sys=1"); Ok = $false; Why = "a second file in the 9x INF" },
            @{ T = $b9.Replace('"$CHICAGO$"', '"$Windows NT$"'); Ok = $false; Why = "the NT Signature in the 9x INF" },
            @{ T = $b9.Replace("DriverVer=$want", "DriverVer=$want`r`nCatalogFile=stor98.cat"); Ok = $false; Why = "a catalog in the 9x INF" },
            @{ T = $b9.Replace("DriverVer=$want", "DriverVer=01/01/2000," + ($want -split ',')[1]); Ok = $false; Why = "another date in the 9x INF" }
        )
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("stor98-inf-" + [System.IO.Path]::GetRandomFileName())
        New-Item -ItemType Directory -Path $tmp | Out-Null
        try {
            $i = 0
            foreach ($c in $c9) {
                $f = Join-Path $tmp ("n{0}.inf" -f $i); $i++
                [System.IO.File]::WriteAllText($f, $c.T, (New-Object System.Text.ASCIIEncoding))
                $why = Get-Inf9xFault -File $f -Want $want
                if (($why -eq "") -ne $c.Ok) { throw "self-test: $($c.Why) was $(if ($c.Ok) { 'refused: ' + $why } else { 'accepted' })." }
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "  check-stor98-inf.ps1 self-test: $($cases.Count + $c64.Count + $c9.Count) cases"
        exit 0
    }
    if ($Path -ne "") { $infs = @(@{ File = $Path; Sig = '$CHICAGO$' }) }
    $bad = 0
    foreach ($inf in $infs) {
        $why = Get-InfFault -File $inf.File -Signature $inf.Sig -Want $want
        if ($why -ne "") { Write-Host ("  {0}: {1}" -f $inf.File, $why); $bad++ }
        else { Write-Host ("  {0}: DriverVer={1}" -f (Split-Path -Leaf $inf.File), $want) }
    }
    if ($Path -eq "") {
        $why = Get-Inf9xFault -File $inf9x -Want $want
        if ($why -ne "") { Write-Host ("  {0}: {1}" -f $inf9x, $why); $bad++ }
        else { Write-Host ("  {0}: DriverVer={1}" -f (Split-Path -Leaf $inf9x), $want) }
    }
    if ($bad -gt 0) { exit 1 }
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message)
    exit 1
}
