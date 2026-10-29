#Requires -Version 2.0
<#
check-pfwsys.ps1 <xhcipfw.sys> [-Qemu] - the gate over XHCIPFW.SYS (design
record 32 section 11.2), run by src\pfw\nt\build.cmd after a link:

  - a PE32 image for i386, native subsystem, a driver (no DLL flag);
  - every import a row of pfw-sys-imports.allow (module!name), and no
    module but ntoskrnl.exe and HAL.dll: each row an export of every
    kernel and HAL from Windows 2000 on, so the one image loads on 2000
    and on the 32-bit Vista, 7, 8, 8.1 and 10 (KeIpiGenericCall, which
    2000 lacks, is looked up at run time, never imported);
  - the stamp "XHCIPFW.SYS <version>" at src\xhci_version.h's XHCI_VER_STR;
  - exactly one flavour marker, XHCI98_FLAVOUR_RELEASE or (-Qemu)
    XHCI98_FLAVOUR_QEMU;
  - in the release image no HAL.dll!WRITE_PORT_UCHAR (the qemu image's
    debug-console writer): its only port writes are the configuration
    pair's WRITE_PORT_ULONG.

Exit code 0 = the image passes; 1 = it does not (each reason printed).
#>
param([Parameter(Mandatory = $true)][string]$Image, [switch]$Qemu)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path
$bad = 0
function Bad([string]$why) { Write-Output "check-pfwsys: $why"; $script:bad++ }

$d = [System.IO.File]::ReadAllBytes($Image)
function U32([int]$o) { return [BitConverter]::ToUInt32($d, $o) }
function U16([int]$o) { return [BitConverter]::ToUInt16($d, $o) }
if ($d.Length -lt 0x40 -or $d[0] -ne 0x4D -or $d[1] -ne 0x5A) { Bad 'not an MZ image'; exit 1 }
$pe = [int](U32 0x3C)
if ($pe -le 0 -or $pe + 0xF8 -gt $d.Length -or (U32 $pe) -ne 0x00004550) { Bad 'not a PE image'; exit 1 }
if ((U16 ($pe + 4)) -ne 0x14C) { Bad 'not an i386 image' }
$chars = U16 ($pe + 0x16)
if (($chars -band 0x2000) -ne 0) { Bad 'the DLL flag is set' }
$opt = $pe + 0x18
if ((U16 $opt) -ne 0x10B) { Bad 'not a PE32 optional header'; exit 1 }
if ((U16 ($opt + 0x44)) -ne 1) { Bad 'the subsystem is not native (1)' }
$nsec = [int](U16 ($pe + 6))
$sect = $opt + [int](U16 ($pe + 0x14))
function RvaToOff([uint32]$rva) {
    for ($i = 0; $i -lt $nsec; $i++) {
        $s = $sect + 40 * $i
        $va = U32 ($s + 12); $vs = U32 ($s + 8); $raw = U32 ($s + 20); $rs = U32 ($s + 16)
        $size = [Math]::Max($vs, $rs)
        if ($rva -ge $va -and $rva -lt $va + $size) { return [int]($raw + ($rva - $va)) }
    }
    return -1
}
function CStr([int]$o) {
    $e = $o; while ($e -lt $d.Length -and $d[$e] -ne 0) { $e++ }
    return [System.Text.Encoding]::ASCII.GetString($d, $o, $e - $o)
}

# The imports.
$imports = @()
$irva = U32 ($opt + 0x68)
if ($irva -ne 0) {
    $io = RvaToOff $irva
    for ($k = 0; ; $k++) {
        $desc = $io + 20 * $k
        $ilt = U32 $desc; $nameRva = U32 ($desc + 12); $iat = U32 ($desc + 16)
        if ($nameRva -eq 0) { break }
        $mod = CStr (RvaToOff $nameRva)
        $t = RvaToOff $(if ($ilt -ne 0) { $ilt } else { $iat })
        for ($j = 0; ; $j++) {
            $v = U32 ($t + 4 * $j)
            if ($v -eq 0) { break }
            if (($v -band 0x80000000) -ne 0) { $imports += "$mod!#$($v -band 0xFFFF)"; continue }
            $imports += "$mod!" + (CStr ((RvaToOff $v) + 2))
        }
    }
}
$allow = @{}
foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $here 'pfw-sys-imports.allow'))) {
    $l = $line.Trim()
    if ($l -eq '' -or $l.StartsWith('#')) { continue }
    $allow[($l -split '\s+')[0].ToLowerInvariant()] = $true
}
foreach ($i in $imports) {
    if (-not $allow.ContainsKey($i.ToLowerInvariant())) { Bad "import $i is not in pfw-sys-imports.allow" }
}
if (-not $Qemu -and ($imports -contains 'HAL.dll!WRITE_PORT_UCHAR')) {
    Bad 'the release image imports HAL.dll!WRITE_PORT_UCHAR (the debug-console writer)'
}
if ($imports.Count -eq 0) { Bad 'no imports read' }

# The stamp and the flavour marker.
$text = [System.Text.Encoding]::ASCII.GetString($d)
$ver = $null
foreach ($l in [System.IO.File]::ReadAllLines((Join-Path $repo 'src\xhci_version.h'))) {
    if ($l -match '^#define\s+XHCI_VER_STR\s+"([^"]+)"') { $ver = $Matches[1] }
}
if (-not $ver) { Bad 'no XHCI_VER_STR in src\xhci_version.h' }
elseif (-not $text.Contains("XHCIPFW.SYS $ver")) { Bad "no stamp `"XHCIPFW.SYS $ver`"" }
$markers = @([regex]::Matches($text, 'XHCI98_FLAVOUR_[A-Z]+') | ForEach-Object { $_.Value })
$want = if ($Qemu) { 'XHCI98_FLAVOUR_QEMU' } else { 'XHCI98_FLAVOUR_RELEASE' }
if ($markers.Count -ne 1 -or $markers[0] -ne $want) { Bad ("flavour markers [{0}], want exactly {1}" -f ($markers -join ', '), $want) }

if ($bad -ne 0) { exit 1 }
Write-Output ("check-pfwsys: {0} passes ({1} imports, stamp {2}, {3})" -f $Image, $imports.Count, $ver, $want)
exit 0
