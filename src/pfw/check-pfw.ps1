#Requires -Version 2.0
<#
check-pfw.ps1 <XHCIPFW.VXD> [-Qemu] - the gate over XHCIPFW.VXD (design record
32 section 8, record 30 section 6.4's checks), run by src\pfw\build.cmd after a link:

  - an LE image whose one entry point is the DDB named "XHCIPFW ";
  - every VxD service it calls (an "int 20h" followed by the service's dword
    in its code object) a row of pfw-services.allow, and every row called;
  - the stamp "XHCIPFW.VXD <version>" at src\xhci_version.h's XHCI_VER_STR;
  - in the release image (no -Qemu), no write to port 0E9h: neither
    "out 0E9h, al" (E6 E9) nor "mov dx, 0E9h" (66 BA E9 00 / BA E9 00 00 00).

  - every LE fixup an internal 32-bit offset (no import, no additive) whose
    target object is one of the image's and whose target offset lies inside
    that object, its source inside the object of its page (design record 30
    section 6.1: the DDK's linker once resolved fixups to FFFFF0xxh).

-SelfTest runs that fixup check over synthetic fixup tables, one that passes
per shape and one per refusal, and reads no image.

The service scan reads the code object's bytes as they are in the file:
VMM patches each dynalink into a direct call only when it first runs.

Exit code 0 = the image passes; 1 = it does not (each reason printed).
#>
param([string]$Image, [switch]$Qemu, [switch]$SelfTest)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
$bad = 0
function Bad([string]$why) { Write-Output "check-pfw: $why"; $script:bad++ }

# Every LE fixup an internal 32-bit offset inside its target object (design
# record 30 section 6.1, 2026-11-03): the DDK's LINK 5.12 once resolved an
# object's fixups to its own segments to FFFFF0xxh, which passed every other
# check. Returns the reasons a fixup table fails; the image is $b, its LE
# header at $l. A fixup's four bytes lie inside the object of its page; one
# that crosses into the next page is listed on both pages (the second time
# at a negative offset) with one target, as the linker writes it.
function Get-LeFixupFaults([byte[]]$b, [int]$l) {
    $why = New-Object System.Collections.ArrayList
    $r32 = { param($o) [BitConverter]::ToUInt32($b, $o) }
    if ($l -lt 0 -or $l + 0x70 -gt $b.Length) { [void]$why.Add('the LE header lies past the file end'); return $why }
    $ps = [int64](& $r32 ($l + 0x28)); $np = [int64](& $r32 ($l + 0x14)); $no = [int64](& $r32 ($l + 0x44))
    $ot = $l + [int64](& $r32 ($l + 0x40)); $fp = $l + [int64](& $r32 ($l + 0x68)); $fr = $l + [int64](& $r32 ($l + 0x6C))
    if ($ps -le 4 -or $no -lt 1 -or $fp + 4 * ($np + 1) -gt $b.Length -or $ot + 24 * $no -gt $b.Length) {
        [void]$why.Add('the fixup page table or the object table lies past the file end'); return $why
    }
    $vs = @(0); $first = @(0); $count = @(0)
    for ($o = 0; $o -lt $no; $o++) {
        $vs += [int64](& $r32 ($ot + 24 * $o)); $first += [int64](& $r32 ($ot + 24 * $o + 12)); $count += [int64](& $r32 ($ot + 24 * $o + 16))
    }
    $seen = @{}; $all = New-Object System.Collections.ArrayList
    for ($pg = 0; $pg -lt $np; $pg++) {
        $p = $fr + [int64](& $r32 ($fp + 4 * $pg)); $e = $fr + [int64](& $r32 ($fp + 4 * $pg + 4))
        if ($e -lt $p -or $e -gt $b.Length) { [void]$why.Add(('page {0}: its fixup records lie outside the table' -f ($pg + 1))); continue }
        # The object page $pg + 1 belongs to.
        $so = 0
        for ($o = 1; $o -le $no; $o++) { if ($pg + 1 -ge $first[$o] -and $pg + 1 -lt $first[$o] + $count[$o]) { $so = $o } }
        while ($p -lt $e) {
            if ($p + 2 -gt $e) { [void]$why.Add(('page {0}: a cut fixup record' -f ($pg + 1))); break }
            $src = $b[$p]; $flg = $b[$p + 1]; $p += 2
            if (($src -band 0xDF) -ne 7 -or ($flg -band 0xAF) -ne 0) {
                [void]$why.Add(('page {0}: a fixup of source type 0x{1:X2} flags 0x{2:X2}, not an internal 32-bit offset' -f ($pg + 1), $src, $flg)); break
            }
            # The record's length first: none of it may lie past the page's end.
            $need = $(if ($src -band 0x20) { 1 } else { 2 }) + 1 + $(if ($flg -band 0x40) { 1 } else { 0 }) + $(if ($flg -band 0x10) { 4 } else { 2 })
            if (($src -band 0x20) -and $p -lt $e) { $need += 2 * [int]$b[$p] }
            if ($p + $need -gt $e) { [void]$why.Add(('page {0}: a cut fixup record' -f ($pg + 1))); break }
            $srcs = @(); $cnt = 1
            if ($src -band 0x20) { $cnt = [int]$b[$p]; $p += 1 } else { $srcs += [int][BitConverter]::ToInt16($b, $p); $p += 2 }
            $to = [int]$b[$p]; $p += 1
            if ($flg -band 0x40) { $to = $to -bor ([int]$b[$p] -shl 8); $p += 1 }
            if ($flg -band 0x10) { $tg = [int64](& $r32 $p); $p += 4 } else { $tg = [int64][BitConverter]::ToUInt16($b, $p); $p += 2 }
            if ($src -band 0x20) { for ($k = 0; $k -lt $cnt; $k++) { $srcs += [int][BitConverter]::ToInt16($b, $p); $p += 2 } }
            foreach ($s in $srcs) {
                $at = ($pg + 1 - $first[$so]) * $ps + $s
                if ($so -eq 0 -or $s -lt -3 -or $s -ge $ps -or $at -lt 0 -or $at + 4 -gt $vs[$so]) {
                    [void]$why.Add(('page {0}: a fixup at 0x{1:X} whose four bytes lie outside the page''s object' -f ($pg + 1), $s))
                    continue
                }
                if ($to -lt 1 -or $to -gt $no) {
                    [void]$why.Add(('page {0} offset 0x{1:X}: a fixup to object {2}, not one of the {3}' -f ($pg + 1), $s, $to, $no))
                } elseif ($tg -ge $vs[$to]) {
                    [void]$why.Add(('page {0} offset 0x{1:X}: a fixup to object {2} offset 0x{3:X}, past its 0x{4:X} bytes' -f ($pg + 1), $s, $to, $tg, $vs[$to]))
                }
                $seen[('{0}|{1}|{2}|{3}' -f $pg, $at, $to, $tg)] = 1
                [void]$all.Add(@($pg, $s, $at, $to, $tg))
            }
        }
    }
    foreach ($f in $all) {
        if ($f[1] -lt 0 -and -not $seen.ContainsKey(('{0}|{1}|{2}|{3}' -f ($f[0] - 1), $f[2], $f[3], $f[4]))) {
            [void]$why.Add(('page {0} offset 0x{1:X}: the tail of a fixup that page {2} does not list' -f ($f[0] + 1), $f[1], $f[0]))
        }
        if ($f[1] + 4 -gt $ps -and -not $seen.ContainsKey(('{0}|{1}|{2}|{3}' -f ($f[0] + 1), $f[2], $f[3], $f[4]))) {
            [void]$why.Add(('page {0} offset 0x{1:X}: a fixup into page {2} that page {2} does not list' -f ($f[0] + 1), $f[1], ($f[0] + 2)))
        }
    }
    if ($all.Count -eq 0) { [void]$why.Add('no fixup at all') }
    return $why
}

# The self-test: a synthetic LE header, one object of 300h bytes in two pages
# of 200h, and the two pages' fixup records per case. Returns a line per
# failing case.
function Test-LeFixupCheck {
    $cases = @(
        @('one internal 32-bit offset', 0, @(0x07, 0x00, 0x10, 0x00, 0x01, 0x40, 0x00), @()),
        @('a list of three', 0, @(0x27, 0x00, 0x03, 0x01, 0x40, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30, 0x00), @()),
        @('a 32-bit target offset inside the object', 0, @(0x07, 0x10, 0x10, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00), @()),
        @('a fixup across the pages, on both', 0, @(0x07, 0x00, 0xFE, 0x01, 0x01, 0x40, 0x00), @(0x07, 0x00, 0xFE, 0xFF, 0x01, 0x40, 0x00)),
        @('the last four bytes of the object', 0, @(), @(0x07, 0x00, 0xFC, 0x00, 0x01, 0x40, 0x00)),
        @('the mislink: target FFFFF000h', 1, @(0x07, 0x10, 0x10, 0x00, 0x01, 0x00, 0xF0, 0xFF, 0xFF), @()),
        @('a target past the object', 1, @(0x07, 0x00, 0x10, 0x00, 0x01, 0x00, 0x03), @()),
        @('a target object 2 of 1', 1, @(0x07, 0x00, 0x10, 0x00, 0x02, 0x40, 0x00), @()),
        @('a source past the object', 1, @(), @(0x07, 0x00, 0x00, 0x01, 0x01, 0x40, 0x00)),
        @('a source whose four bytes end past the object', 1, @(), @(0x07, 0x00, 0xFD, 0x00, 0x01, 0x40, 0x00)),
        @('a fixup across the pages, on the first alone', 1, @(0x07, 0x00, 0xFE, 0x01, 0x01, 0x40, 0x00), @()),
        @('a fixup across the pages, on the second alone', 1, @(), @(0x07, 0x00, 0xFE, 0xFF, 0x01, 0x40, 0x00)),
        @('a fixup across the pages, two targets', 1, @(0x07, 0x00, 0xFE, 0x01, 0x01, 0x40, 0x00), @(0x07, 0x00, 0xFE, 0xFF, 0x01, 0x44, 0x00)),
        @('an additive fixup', 1, @(0x07, 0x04, 0x10, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00), @()),
        @('an import by ordinal', 1, @(0x07, 0x01, 0x10, 0x00, 0x01, 0x01, 0x00), @()),
        @('a 16-bit offset', 1, @(0x05, 0x00, 0x10, 0x00, 0x01, 0x40, 0x00), @()),
        @('a cut record', 1, @(0x07, 0x00, 0x10, 0x00, 0x01, 0x40), @()),
        @('no fixup', 1, @(), @())
    )
    $fails = @()
    foreach ($c in $cases) {
        $r1 = [byte[]]$c[2]; $r2 = [byte[]]$c[3]
        $img = New-Object byte[] (0xE8 + $r1.Length + $r2.Length)
        [BitConverter]::GetBytes([uint32]2).CopyTo($img, 0x14)
        [BitConverter]::GetBytes([uint32]0x200).CopyTo($img, 0x28)
        [BitConverter]::GetBytes([uint32]0xC4).CopyTo($img, 0x40)
        [BitConverter]::GetBytes([uint32]1).CopyTo($img, 0x44)
        [BitConverter]::GetBytes([uint32]0xDC).CopyTo($img, 0x68)
        [BitConverter]::GetBytes([uint32]0xE8).CopyTo($img, 0x6C)
        [BitConverter]::GetBytes([uint32]0x300).CopyTo($img, 0xC4)
        [BitConverter]::GetBytes([uint32]1).CopyTo($img, 0xD0)
        [BitConverter]::GetBytes([uint32]2).CopyTo($img, 0xD4)
        [BitConverter]::GetBytes([uint32]$r1.Length).CopyTo($img, 0xE0)
        [BitConverter]::GetBytes([uint32]($r1.Length + $r2.Length)).CopyTo($img, 0xE4)
        $r1.CopyTo($img, 0xE8); $r2.CopyTo($img, 0xE8 + $r1.Length)
        $got = @(Get-LeFixupFaults $img 0)
        if (($got.Count -ne 0) -ne ($c[1] -eq 1)) {
            $fails += ("self-test: {0}: {1} fault(s), expected {2}: {3}" -f $c[0], $got.Count, $(if ($c[1] -eq 1) { 'some' } else { 'none' }), ($got -join '; '))
        }
    }
    return $fails
}

if ($SelfTest) {
    $f = @(Test-LeFixupCheck)
    foreach ($l in $f) { Write-Output "check-pfw $l" }
    if ($f.Count -ne 0) { exit 1 }
    Write-Output 'check-pfw self-test: the fixup check passes and refuses as it should'
    exit 0
}
if (-not $Image) { Write-Output 'check-pfw: usage: check-pfw.ps1 <image> [-Qemu] | -SelfTest'; exit 1 }

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

$fixups = @(Get-LeFixupFaults $d $le)
foreach ($w in $fixups) { Bad "LE fixups: $w" }

# The entry table: one bundle of one 32-bit entry, the DDB, then the end
# of the table (a zero count) - no other export.
$et = $le + [int](U32 ($le + 0x5C))
if ($et + 10 -ge $d.Length) { Bad 'the entry table lies past the file end'; exit 1 }
if ($d[$et + 9] -ne 0) { Bad 'the entry table has more than the one entry' }
if ($d[$et] -ne 1 -or $d[$et + 1] -ne 3) { Bad ('the entry table is not one 32-bit entry (count {0}, type {1})' -f $d[$et], $d[$et + 1]) }
else {
    $eobj = [int](U16 ($et + 2)); $eoff = [int](U32 ($et + 5))
    $name = [System.Text.Encoding]::ASCII.GetString($mem[$eobj], $eoff + 12, 8)
    if ($name -cne 'XHCIPFW ') { Bad "the DDB's name is '$name', not 'XHCIPFW '" }
}

# The services.
$allowPath = Join-Path $here 'pfw-services.allow'
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
            if (-not $allow.ContainsKey($svc)) { Bad ('object {0} offset 0x{1:X}: int 20h service 0x{2:X8} is not in pfw-services.allow' -f $o, $i, $svc) }
            $seen[$svc] = 1
        }
    }
}
foreach ($k in $allow.Keys) { if (-not $seen.ContainsKey($k)) { Bad ('pfw-services.allow row never called: {0}' -f $allow[$k]) } }

# The stamp.
$ver = ''
foreach ($l in (Get-Content -LiteralPath (Join-Path $repo 'src\xhci_version.h'))) {
    if ($l -match '^#define\s+XHCI_VER_STR\s+"([^"]+)"') { $ver = $Matches[1] }
}
if ($ver -eq '') { Bad 'no XHCI_VER_STR in src\xhci_version.h' }
# In what the loader maps, not anywhere in the file.
$found = $false
foreach ($o in $mem.Keys) {
    if ([System.Text.Encoding]::ASCII.GetString($mem[$o]).IndexOf("XHCIPFW.VXD $ver") -ge 0) { $found = $true }
}
if (-not $found) { Bad "no stamp 'XHCIPFW.VXD $ver' in the image's mapped objects" }

# The release image writes to no debug port.
if (-not $Qemu) {
    for ($i = 0; $i -lt $d.Length - 5; $i++) {
        # Byte patterns only; src\pfw\build.cmd also disassembles every
        # release object and refuses a port instruction outside pfw_io.obj.
        if (($d[$i] -eq 0xE6 -and $d[$i + 1] -eq 0xE9) -or ($d[$i] -eq 0xE7 -and $d[$i + 1] -eq 0xE9) -or
            ($d[$i] -eq 0x66 -and $d[$i + 1] -eq 0xBA -and $d[$i + 2] -eq 0xE9 -and $d[$i + 3] -eq 0) -or
            ($d[$i] -eq 0xBA -and $d[$i + 1] -eq 0xE9 -and $d[$i + 2] -eq 0 -and $d[$i + 3] -eq 0 -and $d[$i + 4] -eq 0)) {
            Bad ('file offset 0x{0:X}: a write to port 0E9h in the release image' -f $i)
        }
    }
}

if ($bad -ne 0) { exit 1 }
Write-Output ("check-pfw: {0} passes (DDB XHCIPFW, {1} service(s), stamp {2}{3})" -f $Image, $seen.Count, $ver, $(if ($Qemu) { ', qemu image' } else { ', no debug port' }))
exit 0
