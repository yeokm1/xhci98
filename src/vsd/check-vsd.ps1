#Requires -Version 2.0
<#
check-vsd.ps1 <STOR98.VXD> [-Qemu] - the gate over STOR98.VXD (design record
30 section 6.4), run by src\vsd\build.cmd after every link:

  - an LE image whose one entry point is the DDB named "S98VSD  ";
  - every VxD service it calls (an "int 20h" followed by the service's dword
    in its code object) a row of vsd-services.allow, and every row called;
  - the stamp "STOR98.VXD <version>" at src\xhci_version.h's XHCI_VER_STR;
  - in the release image (no -Qemu), no write to port 0E9h: neither
    "out 0E9h, al" (E6 E9) nor "mov dx, 0E9h" (66 BA E9 00 / BA E9 00 00 00).

The service scan reads the code object's bytes as they are in the file:
VMM patches each dynalink into a direct call only when it first runs.

Exit code 0 = the image passes; 1 = it does not (each reason printed).
#>
param([Parameter(Mandatory = $true)][string]$Image, [switch]$Qemu)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
$bad = 0
function Bad([string]$why) { Write-Output "check-vsd: $why"; $script:bad++ }

$d = [System.IO.File]::ReadAllBytes($Image)
function U32([int]$o) { return [BitConverter]::ToUInt32($d, $o) }
function U16([int]$o) { return [BitConverter]::ToUInt16($d, $o) }
if ($d.Length -lt 0x40 -or $d[0] -ne 0x4D -or $d[1] -ne 0x5A) { Bad 'not an MZ image'; exit 1 }
$le = [int](U32 0x3C)
if ($d[$le] -ne 0x4C -or $d[$le + 1] -ne 0x45) { Bad 'not an LE image'; exit 1 }

# The objects mapped as the loader maps them (page map, data pages).
$ps = [int](U32 ($le + 0x28)); $dp = [int](U32 ($le + 0x80))
$nobj = [int](U32 ($le + 0x44)); $ot = $le + [int](U32 ($le + 0x40)); $om = $le + [int](U32 ($le + 0x48))
# Every page an ordinary file page (flags 0), its number within the module's
# page count and its bytes within the file: any other page type is refused,
# since what the loader maps for it is not the file's bytes these checks read.
$npages = [int](U32 ($le + 0x14))
$mem = @{}
for ($o = 0; $o -lt $nobj; $o++) {
    $vs = [int](U32 ($ot + 24 * $o)); $pti = [int](U32 ($ot + 24 * $o + 12)); $npe = [int](U32 ($ot + 24 * $o + 16))
    $buf = New-Object byte[] ($vs + $ps)
    for ($k = 0; $k -lt $npe; $k++) {
        $e = $om + 4 * ($pti + $k - 1)
        $pn = ($d[$e] -shl 16) -bor ($d[$e + 1] -shl 8) -bor $d[$e + 2]
        if ($d[$e + 3] -ne 0) { Bad ('object {0} page {1}: page type {2}, not a file page' -f ($o + 1), $k, $d[$e + 3]); continue }
        if ($pn -lt 1 -or $pn -gt $npages) { Bad ('object {0} page {1}: page number {2} outside 1..{3}' -f ($o + 1), $k, $pn, $npages); continue }
        $src = $dp + ($pn - 1) * $ps
        # A whole page, but the module's last, whose size the header gives.
        $len = $(if ($pn -eq $npages) { [int](U32 ($le + 0x2C)) } else { $ps })
        if ($src + $len -gt $d.Length) { Bad ('object {0} page {1}: its {2} bytes run past the file end' -f ($o + 1), $k, $len); continue }
        [Array]::Copy($d, $src, $buf, $k * $ps, $len)
    }
    $mem[$o + 1] = $buf
}

# The entry table: one bundle of one 32-bit entry, the DDB, then the end
# of the table (a zero count) - no other export.
$et = $le + [int](U32 ($le + 0x5C))
if ($et + 10 -ge $d.Length) { Bad 'the entry table lies past the file end'; exit 1 }
if ($d[$et + 9] -ne 0) { Bad 'the entry table has more than the one entry' }
if ($d[$et] -ne 1 -or $d[$et + 1] -ne 3) { Bad ('the entry table is not one 32-bit entry (count {0}, type {1})' -f $d[$et], $d[$et + 1]) }
else {
    $eobj = [int](U16 ($et + 2)); $eoff = [int](U32 ($et + 5))
    $name = [System.Text.Encoding]::ASCII.GetString($mem[$eobj], $eoff + 12, 8)
    if ($name -cne 'S98VSD  ') { Bad "the DDB's name is '$name', not 'S98VSD  '" }
}

# The services.
$allowPath = Join-Path $here 'vsd-services.allow'
$allow = @{}
foreach ($l in (Get-Content -LiteralPath $allowPath)) {
    if ($l -match '^\s*(0x[0-9A-Fa-f]{8})\s') { $allow[[Convert]::ToUInt32($l.Substring($l.IndexOf('0x') + 2, 8), 16)] = $l.Trim() }
}
$seen = @{}
foreach ($o in $mem.Keys) {
    $b = $mem[$o]
    for ($i = 0; $i -lt $b.Length - 6; $i++) {
        if ($b[$i] -eq 0xCD -and $b[$i + 1] -eq 0x20) {
            $svc = [BitConverter]::ToUInt32($b, $i + 2)
            if (-not $allow.ContainsKey($svc)) { Bad ('object {0} offset 0x{1:X}: int 20h service 0x{2:X8} is not in vsd-services.allow' -f $o, $i, $svc) }
            $seen[$svc] = 1
        }
    }
}
foreach ($k in $allow.Keys) { if (-not $seen.ContainsKey($k)) { Bad ('vsd-services.allow row never called: {0}' -f $allow[$k]) } }

# The stamp.
$ver = ''
foreach ($l in (Get-Content -LiteralPath (Join-Path $repo 'src\xhci_version.h'))) {
    if ($l -match '^#define\s+XHCI_VER_STR\s+"([^"]+)"') { $ver = $Matches[1] }
}
if ($ver -eq '') { Bad 'no XHCI_VER_STR in src\xhci_version.h' }
$text = [System.Text.Encoding]::ASCII.GetString($d)
if ($text.IndexOf("STOR98.VXD $ver") -lt 0) { Bad "no stamp 'STOR98.VXD $ver' in the image" }

# The release image writes to no debug port.
if (-not $Qemu) {
    for ($i = 0; $i -lt $d.Length - 5; $i++) {
        # Byte patterns only; src\vsd\build.cmd also disassembles every
        # release object and refuses any OUT-family instruction.
        if (($d[$i] -eq 0xE6 -and $d[$i + 1] -eq 0xE9) -or ($d[$i] -eq 0xE7 -and $d[$i + 1] -eq 0xE9) -or
            ($d[$i] -eq 0x66 -and $d[$i + 1] -eq 0xBA -and $d[$i + 2] -eq 0xE9 -and $d[$i + 3] -eq 0) -or
            ($d[$i] -eq 0xBA -and $d[$i + 1] -eq 0xE9 -and $d[$i + 2] -eq 0 -and $d[$i + 3] -eq 0 -and $d[$i + 4] -eq 0)) {
            Bad ('file offset 0x{0:X}: a write to port 0E9h in the release image' -f $i)
        }
    }
}

if ($bad -ne 0) { exit 1 }
Write-Output ("check-vsd: {0} passes (DDB S98VSD, {1} service(s), stamp {2}{3})" -f $Image, $seen.Count, $ver, $(if ($Qemu) { ', qemu image' } else { ', no debug port' }))
exit 0
