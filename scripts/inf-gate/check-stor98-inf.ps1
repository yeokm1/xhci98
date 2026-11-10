<#
.SYNOPSIS
Hold Stor98's three INFs to the package's one version and date.

.DESCRIPTION
Since 3.1.0.0 (roadmap-usb4 task 42-A.30, owner 2026-10-19) every INF of the
package carries the same DriverVer, from src\xhci_version.h: XHCI_DRIVERVER_DATE
and XHCI_VER_STR. The project's own eight INFs are held to it by check-inf.ps1,
check-uas-inf.ps1 and check-cm-inf.ps1; this holds Stor98's three. The two
NT INFs, src\stor98\inf\stor98-x86.inf and stor98-x64.inf, are staged as
stor98.inf at the root of the flat x86 and amd64 packages and published as
nt-x86\stor98\stor98.inf and nt-x64\stor98\stor98.inf (design record 36).
Each must have exactly one DriverVer line, zero-padded MM/DD/YYYY, equal to
the header's date and version; its Signature must be $Windows NT$ for both.
The x86 INF is NT-only since 3.4.0.0 (design record 36 section 3.3, owner
2026-10-31): 98 SE and ME install from the 9x INF, so it
names stor98.sys alone in [SourceDisksFiles], has no undecorated install
section and writes nothing only 98 SE and ME read (no DevLoader, PortDriver,
stor98.mpd or 9x settings). The x64 INF names its catalog, CatalogFile=stor98.cat, once, which the
packager makes and test-signs (design record 19, owner 2026-10-22: Windows
8 to 10 x64 refuse a package without one in test-signing mode), and writes
no DmaRemappingCompatible (with it 1, Windows 10 x64 does not start Stor98:
docs/PLAN.md, 2026-10-23) and deletes one an earlier build wrote, in the
service's DelReg; the x86 INF names no catalog. Both NT install sections
have a .HW section writing MSISupported 0 (task 45.31: the SCSI port
driver takes no message-signaled interrupt, and a drive stornvme ran has 1). The models before NT
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
in the flat package, published as 98-me\stor98\stor98.inf, and installed
on 98 SE and ME alone), keeps the class match beside the id
(design record 31 section 5.2, owner 2026-10-25): one undecorated models
section with the tunnel id and PCI\CC_010802 as its compatible id, no
decoration, no NT section, stor98.mpd its one file, no catalog. The
[Manufacturer] section must hold exactly one line, with exactly those
decorations, so no other decoration can add a models section unchecked. scripts\build-driver.cmd
runs it with the other INF gates, after -SelfTest.

.PARAMETER Path
One INF to check instead of the three, under the rules its name says: a
name with -x64 or amd64 the x64 INF's, one with -9x or w9 the 9x INF's, any
other the x86 INF's.

.PARAMETER SelfTest
Check that the gate refuses a wrong date, a wrong version, an unpadded date,
a second DriverVer line, a wrong model shape and a 9x part in the x86 INF,
and accepts the committed INFs. A refusal case that leaves its INF unchanged
fails the self-test.
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
    param([string]$File, [string]$Arch, [string]$Want)
    $t = [System.IO.File]::ReadAllText($File)
    $x64 = ($Arch -eq 'x64')
    $Signature = '$Windows NT$'
    $dv = [regex]::Matches($t, '(?im)^[ \t]*DriverVer[ \t]*=[ \t]*([^\r\n;]*?)[ \t]*(;[^\r\n]*)?\r?$')
    if ($dv.Count -ne 1) { return "it has $($dv.Count) DriverVer lines, not one" }
    $got = ($dv[0].Groups[1].Value -replace '\s', '')
    if ($got -cne $Want) { return "its DriverVer is '$got', and src\xhci_version.h says '$Want'" }
    $sig = [regex]::Match($t, '(?im)^[ \t]*Signature[ \t]*=[ \t]*"([^"]*)"')
    if (-not $sig.Success -or $sig.Groups[1].Value -ne $Signature) { return "its Signature is not `"$Signature`"" }
    # Line-based interrupts (task 45.31): the NT install section's .HW
    # section writes MSISupported 0, so a drive stornvme ran (MSISupported
    # 1) starts on Stor98, which the SCSI port driver serves with no
    # message-signaled interrupt.
    $hwName = if ($x64) { 'Stor98_Inst.NTamd64.HW' } else { 'Stor98_Inst.NTx86.HW' }
    $hw = [regex]::Match($t, '(?ims)^[ \t]*\[' + [regex]::Escape($hwName) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    $hwl = @()
    if ($hw.Success) { $hwl = @([regex]::Matches($hw.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' }) }
    if ($hwl.Count -ne 1 -or $hwl[0] -notmatch '^(?i)AddReg=([A-Za-z0-9_]+)$') { return "its [$hwName] section is not the one line AddReg=<section> (MSISupported 0, task 45.31)" }
    $hrName = $Matches[1]
    $hr = [regex]::Match($t, '(?ims)^[ \t]*\[' + [regex]::Escape($hrName) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    $hrl = @()
    if ($hr.Success) { $hrl = @([regex]::Matches($hr.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value }) }
    # Space allowed around the commas alone: the subkey's one space is part
    # of its name.
    if ($hrl.Count -ne 1 -or $hrl[0] -cnotmatch '^HKR[ \t]*,[ \t]*"Interrupt Management\\MessageSignaledInterruptProperties"[ \t]*,[ \t]*MSISupported[ \t]*,[ \t]*%REG_DWORD%[ \t]*,[ \t]*0$') { return "its [$hrName] section is not the one line writing MSISupported 0 (task 45.31)" }
    $cat = @([regex]::Matches($t, '(?im)^[ \t]*CatalogFile(\.[^ \t=]*)?[ \t]*=[ \t]*([^\r\n;]*?)[ \t]*(;[^\r\n]*)?\r?$'))
    if ($x64) {
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
    # The models (record 31 sections 4.7, 4.8 and 4.10): one [Manufacturer]
    # line, exactly these decorations, and each section exactly one model
    # line - the tunnel id as the hardware id up to NT 6.3 (8.1, the kept
    # child of section 4.10), the class as the compatible id (hardware id
    # empty) on 10 (NT 10.0); no class match elsewhere.
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
    $want = if ($x64) { 'Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0' } else { 'Stor98_Models,NTx86.6.3,NTx86.10.0' }
    if (($parts -join ',') -cne $want) { return "its [Manufacturer] line is '$($parts -join ',')', not '$want' (record 31 sections 4.7 and 4.10)" }
    $sec10 = if ($x64) { 'Stor98_Models.NTamd64.10.0' } else { 'Stor98_Models.NTx86.10.0' }
    foreach ($h in [regex]::Matches($t, '(?ims)^[ \t]*\[([^\]\r\n]+)\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')) {
        if ($h.Groups[1].Value.Trim() -ne $sec10 -and [regex]::IsMatch($h.Groups[2].Value, '(?im)^[ \t]*%Stor98\.DeviceDesc%[^\r\n;]*PCI\\CC_')) { return "a model line outside [$sec10] matches a PCI class id, which would offer Stor98 for an internal drive" }
    }
    $sections = if ($x64) { @(@('Stor98_Models.NTamd64', 0), @('Stor98_Models.NTamd64.6.3', 0), @('Stor98_Models.NTamd64.10.0', 3)) } else { @(@('Stor98_Models', 0), @('Stor98_Models.NTx86.6.3', 0), @('Stor98_Models.NTx86.10.0', 3)) }
    foreach ($s in $sections) {
        $sm = [regex]::Match($t, '(?ims)^[ \t]*\[' + [regex]::Escape($s[0]) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
        if (-not $sm.Success) { return "it has no [$($s[0])] section" }
        $ml = @([regex]::Matches($sm.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]', '' })
        $line = switch ($s[1]) { 3 { '%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802' } default { '%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802' } }
        if ($ml.Count -ne 1 -or $ml[0] -cne $line) { return "its [$($s[0])] section is not the one line '$line'" }
    }
    if ($x64) {
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
    if (-not $x64) {
        # NT-only (design record 36 section 3.3): stor98.sys its one file, no
        # undecorated install section (98 SE and ME install from the 9x INF),
        # and nothing only their engine reads, outside comments.
        $sf = Get-SectionLines $t 'SourceDisksFiles'
        if ($null -eq $sf -or @($sf).Count -ne 1 -or ($sf[0] -replace '[ \t]', '') -cne 'stor98.sys=1') { return "its [SourceDisksFiles] is not stor98.sys alone (the x86 INF is NT-only, design record 36 section 3.3)" }
        foreach ($h in [regex]::Matches($t, '(?m)^[ \t]*\[([^\]\r\n]+)\]')) {
            $name = $h.Groups[1].Value.Trim()
            if ($name -match '^(?i)Stor98_Inst(\.|$)' -and $name -notmatch '^(?i)Stor98_Inst\.NTx86(\.|$)') { return "it has the install section [$name], which is not an NTx86 one (the x86 INF is NT-only, design record 36 section 3.3)" }
        }
        foreach ($raw in ($t -split '\r?\n')) {
            $line = ($raw -replace ';.*$', '')
            if ($line -match '(?i)(stor98\.mpd|DevLoader|PortDriver|IOSUBSYS|HKLM[ \t]*,[ \t]*"?System\\CurrentControlSet\\Services\\stor98)') { return "it names '$($Matches[1])', which only 98 SE and ME read (the x86 INF is NT-only, design record 36 section 3.3)" }
        }
    }
    return (Get-SettingsFault $t $false $true)
}

# The settings (xhci98 roadmap-usb4 45.50, owner 2026-10-30: every value a
# released driver reads is written by the install at its default, never
# over a value already there): on 98 SE and ME each value in the key
# platform.c reads (PLAT_WT_KEY), from the install section's AddReg; on
# 2000 to 10 one DriverParameter string with every product key, from the
# service's AddReg. Whitespace around the commas is ignored.
$settings9x = @(
    'HKLM,"System\CurrentControlSet\Services\stor98",StorMaxTransferKB,0x00010003,2048',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorWriteThrough,0x00000002,"auto"',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorPollMode,0x00010003,0',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorPollBusyUs,0x00010003,1000',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorPollIdleMs,0x00010003,1000',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorPollSpinUs,0x00010003,0',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorIntWatchdog,0x00010003,1',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorGoneMs,0x00010003,5000',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorWriteIdleMs,0x00010003,1000',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorHold9xMs,0x00010003,4000',
    'HKLM,"System\CurrentControlSet\Services\stor98",StorTakeFirmware,0x00010003,0'
)
$settingsNt = 'HKR,"Parameters\Device","DriverParameter",0x00000002,"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000"'

function Get-SectionLines {
    param([string]$Text, [string]$Name)
    $m = [regex]::Match($Text, '(?ims)^[ \t]*\[' + [regex]::Escape($Name) + '\][ \t]*\r?$(.*?)(?=^[ \t]*\[|\z)')
    if (-not $m.Success) { return $null }
    return , @([regex]::Matches($m.Groups[1].Value, '(?m)^[ \t]*([^;\s\r\n][^\r\n]*?)[ \t]*(;[^\r\n;"]*)?\r?$') | ForEach-Object { $_.Groups[1].Value -replace '[ \t]*,[ \t]*', ',' })
}

function Get-SettingsFault {
    # "" when the settings are written as above; $Nine the 98 SE and ME
    # install, $Nt the 2000 to 10 one.
    param([string]$Text, [bool]$Nine, [bool]$Nt)
    if ($Nine) {
        $inst = Get-SectionLines $Text 'Stor98_Inst'
        $add = @($inst | Where-Object { $_ -match '^(?i)AddReg=' })
        if ($null -eq $inst -or $add.Count -ne 1 -or @(($add[0] -replace '^(?i)AddReg=', '').Split(',') | ForEach-Object { $_.Trim() }) -notcontains 'Stor98_Settings_9x') { return "its [Stor98_Inst] does not write [Stor98_Settings_9x] (the 98 SE and ME settings, roadmap-usb4 45.50)" }
        $got = Get-SectionLines $Text 'Stor98_Settings_9x'
        if ($null -eq $got -or (@($got) -join "`n") -cne ($settings9x -join "`n")) { return "its [Stor98_Settings_9x] is not the eleven settings at their 98 SE and ME defaults, each written only where missing (roadmap-usb4 45.50)" }
    }
    $dp = @([regex]::Matches($Text, '(?im)DriverParameter'))
    if ($Nt) {
        $got = Get-SectionLines $Text 'Stor98_ServiceReg'
        $hits = @($got | Where-Object { $_ -match '(?i)DriverParameter' })
        if ($null -eq $got -or $hits.Count -ne 1 -or $hits[0] -cne $settingsNt -or ($dp.Count - @([regex]::Matches($Text, '(?im)^[ \t]*;[^\r\n]*DriverParameter')).Count) -ne 1) { return "its [Stor98_ServiceReg] does not write DriverParameter once, with every product key at its Windows 2000 to 10 default and only where missing (roadmap-usb4 45.50)" }
    } elseif (($dp.Count - @([regex]::Matches($Text, '(?im)^[ \t]*;[^\r\n]*DriverParameter')).Count) -ne 0) {
        return "it writes DriverParameter, which 98 SE and ME never read"
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
    return (Get-SettingsFault $t $true $false)
}

try {
    $want = Get-Want
    $infs = @(
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x86.inf"); Arch = 'x86' },
        @{ File = (Join-Path $repo "src\stor98\inf\stor98-x64.inf"); Arch = 'x64' }
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
                @{ T = $base.Replace("[SourceDisksFiles]`r`nstor98.sys=1", "[SourceDisksFiles]`r`nstor98.mpd=1`r`nstor98.sys=1"); Ok = $false; Why = "stor98.mpd back in the x86 INF's file list (record 36 section 3.3)" },
                @{ T = $base.Replace("[SourceDisksFiles]`r`nstor98.sys=1", "[SourceDisksFiles]`r`nstor98.sys=1`r`nother.sys=1"); Ok = $false; Why = "a second file in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Inst.NTx86]", "[Stor98_Inst]`r`nCopyFiles=Stor98_Files_NT`r`n`r`n[Stor98_Inst.NTx86]"); Ok = $false; Why = "an undecorated install section back in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Inst.NTx86]", "[Stor98_Inst.NT]`r`nCopyFiles=Stor98_Files_NT`r`n`r`n[Stor98_Inst.NTx86]"); Ok = $false; Why = "an .NT install section in the x86 INF" },
                @{ T = $base.Replace("[Stor98_HwReg]", "[Stor98_IOS]`r`nHKR,,DevLoader,,*IOS`r`n`r`n[Stor98_HwReg]"); Ok = $false; Why = "a DevLoader line in the x86 INF" },
                @{ T = $base.Replace("[Stor98_HwReg]", "[Stor98_AddReg_9x]`r`nHKR,,PortDriver,,stor98.mpd`r`n`r`n[Stor98_HwReg]"); Ok = $false; Why = "a PortDriver line in the x86 INF" },
                @{ T = $base.Replace("[Stor98_HwReg]", "[Stor98_Settings_9x]`r`nHKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010003,0`r`n`r`n[Stor98_HwReg]"); Ok = $false; Why = "a 98 SE and ME setting in the x86 INF" },
                @{ T = $base.Replace("[Stor98_HwReg]", "; stor98.mpd, DevLoader and PortDriver in a comment`r`n[Stor98_HwReg]"); Ok = $true; Why = "the 9x names in a comment of the x86 INF" },
                @{ T = $base.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000000,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter written over a user's (45.50)" },
                @{ T = $base.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter missing a key (45.50)" },
                @{ T = $base.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"`r`n", ""); Ok = $false; Why = "no DriverParameter (45.50)" },
                @{ T = $base.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=0;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter off a default (45.50)" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=01/01/2000," + $parts[1]); Ok = $false; Why = "another date" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=" + $parts[0] + ",0.4.0.0"); Ok = $false; Why = "another version" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$unpadded," + $parts[1]); Ok = ($unpadded -eq $parts[0]); Why = "an unpadded date" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$want`r`nDriverVer=$want"); Ok = $false; Why = "two DriverVer lines" },
                @{ T = $base.Replace('"$Windows NT$"', '"$CHICAGO$"'); Ok = $false; Why = "the 9x Signature in the x86 INF" },
                @{ T = $base.Replace("DriverVer=$want", "DriverVer=$want`r`nCatalogFile=stor98.cat"); Ok = $false; Why = "a catalog in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0", "%Mfg%=Stor98_Models"); Ok = $false; Why = "no NT 6.3 and 10.0 models in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0", "%Mfg%=Stor98_Models,NTx86.6.3"); Ok = $false; Why = "no 10.0 models in the x86 INF (the class model back on 10 alone, record 31 section 4.10)" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0", "%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0,NTx86.10.0.22000"); Ok = $false; Why = "a later decoration in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0", "%Mfg%=Stor98_Models,NTx86,NTx86.6.3,NTx86.10.0"); Ok = $false; Why = "an NTx86 decoration in the x86 INF" },
                @{ T = $base.Replace("%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0", "%Mfg%=Stor98_Models,NTx86.6.3,NTx86.10.0`r`n%Mfg%=Other_Models"); Ok = $false; Why = "a second manufacturer line in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802"); Ok = $false; Why = "the class model back on 8.1 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models.NTx86.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802"); Ok = $false; Why = "the class as a compatible id on 8.1 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match back for 2000 to 8 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802,PCI\CC_010802"); Ok = $false; Why = "the class as a compatible id for 2000 to 8 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL"); Ok = $false; Why = "another id in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class as the hardware id on 10 in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_0108"); Ok = $false; Why = "the whole subclass on 10 in the x86 INF" },
                @{ T = $base.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802"); Ok = $false; Why = "the tunnel id on 10 in the x86 INF" },
                @{ T = $base.Replace("[Stor98_Inst.NTx86.HW]", "[Stor98_Inst.NTx86.HWX]"); Ok = $false; Why = "no .HW section in the x86 INF" },
                @{ T = $base.Replace("MSISupported,%REG_DWORD%,0", "MSISupported,%REG_DWORD%,1"); Ok = $false; Why = "MSISupported 1 in the x86 INF" },
                @{ T = $base.Replace("Interrupt Management\MessageSignaled", "InterruptManagement\MessageSignaled"); Ok = $false; Why = "the subkey without its space in the x86 INF" },
                @{ T = $base.Replace("Interrupt Management\MessageSignaled", "Interrupt  Management\MessageSignaled"); Ok = $false; Why = "the subkey with two spaces in the x86 INF" },
                @{ T = $base.Replace("MSISupported,%REG_DWORD%,0", "MSISupported , %REG_DWORD% , 0"); Ok = $true; Why = "spaces around the commas in the x86 INF" },
                @{ T = $base.Replace("AddReg=Stor98_HwReg", "AddReg=Stor98_Other"); Ok = $false; Why = "the .HW section naming another section" }
            )
            $i = 0
            foreach ($c in $cases) {
                if (-not $c.Ok -and $c.T -ceq $base) { throw "self-test: $($c.Why) changes nothing in the x86 INF." }
                $f = Join-Path $tmp ("m{0}.inf" -f $i); $i++
                [System.IO.File]::WriteAllText($f, $c.T, (New-Object System.Text.ASCIIEncoding))
                $why = Get-InfFault -File $f -Arch 'x86' -Want $want
                if (($why -eq "") -ne $c.Ok) { throw "self-test: $($c.Why) was $(if ($c.Ok) { 'refused: ' + $why } else { 'accepted' })." }
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        $b64 = [System.IO.File]::ReadAllText($infs[1].File)
        $c64 = @(
            @{ T = $b64; Ok = $true; Why = "the committed x64 INF" },
            @{ T = $b64.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000000,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter written over a user's (45.50)" },
            @{ T = $b64.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter missing a key (45.50)" },
            @{ T = $b64.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"`r`n", ""); Ok = $false; Why = "no DriverParameter (45.50)" },
            @{ T = $b64.Replace("HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"", "HKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=0;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`""); Ok = $false; Why = "DriverParameter off a default (45.50)" },
            @{ T = $b64.Replace("[Stor98_Inst.NTamd64.HW]", "[Stor98_Inst.NTamd64.HWX]"); Ok = $false; Why = "no .HW section in the x64 INF" },
            @{ T = $b64.Replace("MSISupported,%REG_DWORD%,0", "MSISupported,%REG_DWORD%,1"); Ok = $false; Why = "MSISupported 1 in the x64 INF" },
            @{ T = $b64.Replace("Interrupt Management\MessageSignaled", "InterruptManagement\MessageSignaled"); Ok = $false; Why = "the subkey without its space in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64]", "[Stor98_Models]`r`n`r`n[Stor98_Models.NTamd64]"); Ok = $false; Why = "an empty undecorated models section in the x64 INF" },
            @{ T = $b64.Replace("CatalogFile=stor98.cat`r`n", ""); Ok = $false; Why = "no catalog in the x64 INF" },
            @{ T = $b64.Replace("%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0", "%Mfg%=Stor98_Models,NTamd64"); Ok = $false; Why = "no NT 6.3 and 10.0 models in the x64 INF" },
            @{ T = $b64.Replace("%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0", "%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3"); Ok = $false; Why = "no 10.0 models in the x64 INF" },
            @{ T = $b64.Replace("%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0", "%Mfg%=Stor98_Models,NTamd64,NTamd64.6.3,NTamd64.10.0,NTamd64.10.0.22000"); Ok = $false; Why = "a later decoration in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802"); Ok = $false; Why = "the class model back on 8.1 in the x64 INF" },
            @{ T = $b64 + "`r`n[Stor98_Models.NTamd64.6.3]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802`r`n"; Ok = $false; Why = "a second NT 6.3 models section in the x64 INF" },
            @{ T = $b64 + "`r`n[Manufacturer]`r`n%Mfg%=Stor98_Models,NTamd64.10.0`r`n"; Ok = $false; Why = "a second [Manufacturer] section in the x64 INF" },
            @{ T = $b64 + "`r`n[Stor98_Models.NTamd64.6.3] ; more models`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802`r`n"; Ok = $false; Why = "a second NT 6.3 models section with a comment on its header" },
            @{ T = $b64 + "`r`n[Manufacturer] ; more`r`n%Mfg%=Stor98_Models,NTamd64.10.0`r`n"; Ok = $false; Why = "a second [Manufacturer] with a comment on its header" },
            @{ T = $b64.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class as the hardware id on 10 in the x64 INF" },
            @{ T = $b64.Replace("[Stor98_Models.NTamd64]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802", "[Stor98_Models.NTamd64]`r`n%Stor98.DeviceDesc%=Stor98_Inst,PCI\CC_010802"); Ok = $false; Why = "the class match back in the x64 INF" },
            @{ T = $b64.Replace("%Stor98.DeviceDesc%=Stor98_Inst,,PCI\CC_010802", "%Stor98.DeviceDesc%=Stor98_Inst,PCI\XHCI98_TUNNEL&CC_010802"); Ok = $false; Why = "the tunnel id on 10 in the x64 INF" },
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
                if (-not $c.Ok -and $c.T -ceq $b64) { throw "self-test: $($c.Why) changes nothing in the x64 INF." }
                $f = Join-Path $tmp ("x{0}.inf" -f $i); $i++
                [System.IO.File]::WriteAllText($f, $c.T, (New-Object System.Text.ASCIIEncoding))
                $why = Get-InfFault -File $f -Arch 'x64' -Want $want
                if (($why -eq "") -ne $c.Ok) { throw "self-test: $($c.Why) was $(if ($c.Ok) { 'refused: ' + $why } else { 'accepted' })." }
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        $b9 = [System.IO.File]::ReadAllText($inf9x)
        $c9 = @(
            @{ T = $b9; Ok = $true; Why = "the committed 9x INF" },
            @{ T = $b9.Replace("AddReg=Stor98_IOS,Stor98_AddReg_9x,Stor98_Settings_9x", "AddReg=Stor98_IOS,Stor98_AddReg_9x"); Ok = $false; Why = "the 98 SE and ME settings not written (45.50)" },
            @{ T = $b9.Replace("HKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010003,0", "HKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010003,1"); Ok = $false; Why = "a 98 SE and ME setting off its default (45.50)" },
            @{ T = $b9.Replace("HKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010003,0", "HKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010001,0"); Ok = $false; Why = "a 98 SE and ME setting written over a user's (45.50)" },
            @{ T = $b9.Replace("HKLM,`"System\CurrentControlSet\Services\stor98`",StorPollMode,0x00010003,0`r`n", ""); Ok = $false; Why = "a 98 SE and ME setting missing (45.50)" },
            @{ T = $b9.Replace("HKLM,`"System\CurrentControlSet\Services\stor98`",StorWriteThrough,0x00000002,`"auto`"", "HKLM,`"System\CurrentControlSet\Services\stor98`",StorWriteThrough,0x00000002,`"off`""); Ok = $false; Why = "write-through off its default auto (45.50)" },
            @{ T = $b9.Replace("[Strings]", "[Stor98_More]`r`nHKR,`"Parameters\Device`",`"DriverParameter`",0x00000002,`"stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`"`r`n`r`n[Strings]"); Ok = $false; Why = "DriverParameter in the 9x INF (45.50)" },
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
                if (-not $c.Ok -and $c.T -ceq $b9) { throw "self-test: $($c.Why) changes nothing in the 9x INF." }
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
    if ($Path -ne "") {
        # The rules follow the INF's name: until 2026-10-29 a -Path INF was
        # always held to the x86 INF's rules.
        $leaf = Split-Path -Leaf $Path
        if ($leaf -match '(-9x|w9)') {
            $inf9x = $Path
            $infs = @()
        } elseif ($leaf -match '(-x64|amd64)') {
            $infs = @(@{ File = $Path; Arch = 'x64' })
        } else {
            $infs = @(@{ File = $Path; Arch = 'x86' })
        }
    }
    $bad = 0
    foreach ($inf in $infs) {
        $why = Get-InfFault -File $inf.File -Arch $inf.Arch -Want $want
        if ($why -ne "") { Write-Host ("  {0}: {1}" -f $inf.File, $why); $bad++ }
        else { Write-Host ("  {0}: DriverVer={1}" -f (Split-Path -Leaf $inf.File), $want) }
    }
    if ($Path -eq "" -or $infs.Count -eq 0) {
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
