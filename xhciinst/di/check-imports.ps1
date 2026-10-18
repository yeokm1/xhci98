#Requires -Version 2.0
<#
check-imports.ps1 - the import gate of XHCIDI.EXE (design record 27 sections
2 and 7). A 16-bit Windows image imports by module and ordinal, and
Windows 98 SE and ME refuse to load it when the running system's module
lacks one, so the gate reads the NE image itself: its module reference
table and every segment's relocation records, the imported-by-ordinal and
imported-by-name entries both. It requires:

  - the modules to be KERNEL, USER and SETUPX exactly;
  - every import to be by ordinal, and each (module, ordinal) to be a row
    of xhcidi-imports.allow, and every row to be imported (the list is the
    image's, not a superset of it);
  - each row's name to be what the import library that bound it says that
    ordinal is: obj\setupx.lib (the DDK's, staged by stage-ddk.ps1) for
    SETUPX, Open Watcom's lib286\win\windows.lib for KERNEL and USER. The
    libraries' IMPDEF records (an OMF COMENT of class A0, subtype 01) carry
    the name, the module and the ordinal. A library that is not on this
    host is said and that half skipped, so a host without the DDK can still
    gate the module and ordinal set.

The SETUPX rows are the routines record 27 section 2 lists; a new one is a
change to that record first.

  check-imports.ps1 -Path XHCIDI.EXE           the gate
  check-imports.ps1 -Path XHCIDI.EXE -List     print each import with its name

Exit code 0 = the image's imports are the list's; 1 = anything else.
#>
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Allow = "",
    [string]$SetupxLib = "",
    [string]$WindowsLib = "",
    [switch]$List
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Allow -eq '') { $Allow = Join-Path $here 'xhcidi-imports.allow' }
if ($SetupxLib -eq '') { $SetupxLib = Join-Path $here 'obj\setupx.lib' }
if ($WindowsLib -eq '') {
    $w = $env:WATCOM
    if (-not $w) { $w = 'C:\WATCOM' }
    $WindowsLib = Join-Path $w 'lib286\win\windows.lib'
}

function Fail([string]$why) {
    Write-Output "ERROR: ${Path}: $why"
    exit 1
}

try { $b = [System.IO.File]::ReadAllBytes($Path) } catch { Fail 'the image cannot be read' }
function U8([int]$o) { if ($o + 1 -gt $b.Length) { Fail 'truncated image' }; return [int]$b[$o] }
function U16([int]$o) { if ($o + 2 -gt $b.Length) { Fail 'truncated image' }; return [int][BitConverter]::ToUInt16($b, $o) }
function U32([int]$o) { if ($o + 4 -gt $b.Length) { Fail 'truncated image' }; return [BitConverter]::ToUInt32($b, $o) }

if ($b.Length -lt 64 -or (U16 0) -ne 0x5A4D) { Fail 'not an MZ image' }
$ne = [int](U32 0x3C)
if ((U16 $ne) -ne 0x454E) { Fail 'not an NE image' }
$segCount = U16 ($ne + 0x1C)
$modCount = U16 ($ne + 0x1E)
$segTab = $ne + (U16 ($ne + 0x22))
$modTab = $ne + (U16 ($ne + 0x28))
$impTab = $ne + (U16 ($ne + 0x2A))
$align = U16 ($ne + 0x32)
if ($align -eq 0) { $align = 9 }

function PString([int]$o) {
    $n = U8 $o
    if ($o + 1 + $n -gt $b.Length) { Fail 'a name past the end of the image' }
    return [System.Text.Encoding]::ASCII.GetString($b, $o + 1, $n)
}

$modules = @()
for ($i = 0; $i -lt $modCount; $i++) {
    $modules += (PString ($impTab + (U16 ($modTab + 2 * $i)))).ToUpperInvariant()
}

$imports = @{}
for ($s = 0; $s -lt $segCount; $s++) {
    $e = $segTab + 8 * $s
    $sector = U16 $e
    $len = U16 ($e + 2)
    $flags = U16 ($e + 4)
    if ($len -eq 0) { $len = 0x10000 }
    if (($flags -band 0x0100) -eq 0 -or $sector -eq 0) { continue }
    $rel = ($sector -shl $align) + $len
    $count = U16 $rel
    if ($rel + 2 + 8 * $count -gt $b.Length) { Fail ("segment {0}'s relocation table runs past the end of the image" -f ($s + 1)) }
    for ($r = 0; $r -lt $count; $r++) {
        $o = $rel + 2 + 8 * $r
        $kind = (U8 ($o + 1)) -band 3
        if ($kind -eq 1) {
            $m = U16 ($o + 4)
            $ord = U16 ($o + 6)
            if ($m -lt 1 -or $m -gt $modCount) { Fail "a relocation naming module $m of $modCount" }
            $imports[("{0}.{1}" -f $modules[$m - 1], $ord)] = 1
        } elseif ($kind -eq 2) {
            $m = U16 ($o + 4)
            $nm = PString ($impTab + (U16 ($o + 6)))
            Fail ("an import by name, {0}!{1}: every import must be by ordinal" -f $modules[$m - 1], $nm)
        }
    }
}

# The IMPDEF records of an OMF import library: name -> (module, ordinal).
function Read-Impdefs([string]$lib) {
    $map = @{}
    if (-not (Test-Path -LiteralPath $lib -PathType Leaf)) { return $null }
    $l = [System.IO.File]::ReadAllBytes($lib)
    for ($i = 0; $i + 8 -lt $l.Length; $i++) {
        if ($l[$i] -ne 0x88 -or $l[$i + 4] -ne 0xA0 -or $l[$i + 5] -ne 0x01) { continue }
        $rlen = [int][BitConverter]::ToUInt16($l, $i + 1)
        $p = $i + 6
        $byOrd = $l[$p]; $p++
        $n = $l[$p]; $name = [System.Text.Encoding]::ASCII.GetString($l, $p + 1, $n); $p += 1 + $n
        $n = $l[$p]; $mod = [System.Text.Encoding]::ASCII.GetString($l, $p + 1, $n); $p += 1 + $n
        if ($byOrd -eq 0) { continue }
        $ord = [int][BitConverter]::ToUInt16($l, $p); $p += 2
        if ($p + 1 -ne $i + 3 + $rlen) { continue }
        $map[$name.ToUpperInvariant()] = @{ Module = $mod.ToUpperInvariant(); Ordinal = $ord; Name = $name }
    }
    return $map
}

$setupx = Read-Impdefs $SetupxLib
$windows = Read-Impdefs $WindowsLib

if ($List) {
    foreach ($k in ($imports.Keys | Sort-Object)) {
        $mod, $ord = $k.Split('.')
        $lib = if ($mod -eq 'SETUPX') { $setupx } else { $windows }
        $nm = '?'
        if ($null -ne $lib) {
            foreach ($v in $lib.Values) { if ($v.Module -eq $mod -and $v.Ordinal -eq [int]$ord) { $nm = $v.Name } }
        }
        Write-Output ("{0} {1} {2}" -f $mod, $ord, $nm)
    }
    exit 0
}

$want = @('KERNEL', 'SETUPX', 'USER')
if ((($modules | Sort-Object) -join ',') -ne ($want -join ',')) {
    Fail ("imports from " + ($modules -join ', ') + "; only KERNEL, USER and SETUPX are allowed")
}

if (-not (Test-Path -LiteralPath $Allow)) { Fail "no allowlist $Allow" }
$rows = @{}
foreach ($line in (Get-Content -LiteralPath $Allow)) {
    $t = $line.Trim()
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    $f = @($t -split '\s+')
    if ($f.Count -lt 3 -or $f[1] -notmatch '^[0-9]+$') { Fail "a malformed allowlist row: $t" }
    $key = "{0}.{1}" -f $f[0].ToUpperInvariant(), [int]$f[1]
    if ($rows.ContainsKey($key)) { Fail "the allowlist names $key twice" }
    $rows[$key] = $f[2]
}

$bad = @($imports.Keys | Where-Object { -not $rows.ContainsKey($_) } | Sort-Object)
if ($bad.Count -ne 0) { Fail ("imports not in the allowlist: " + ($bad -join ', ')) }
$unused = @($rows.Keys | Where-Object { -not $imports.ContainsKey($_) } | Sort-Object)
if ($unused.Count -ne 0) { Fail ("allowlist rows the image does not import: " + ($unused -join ', ')) }

foreach ($k in $rows.Keys) {
    $mod, $ord = $k.Split('.')
    $lib = if ($mod -eq 'SETUPX') { $setupx } else { $windows }
    $libName = if ($mod -eq 'SETUPX') { $SetupxLib } else { $WindowsLib }
    if ($null -eq $lib) { continue }
    $v = $lib[$rows[$k].ToUpperInvariant()]
    if ($null -eq $v) { Fail ("{0} has no import named {1} (allowlist row {2})" -f $libName, $rows[$k], $k) }
    if ($v.Module -ne $mod -or $v.Ordinal -ne [int]$ord) {
        Fail ("{0} binds {1} to {2}.{3}, not {4}" -f $libName, $rows[$k], $v.Module, $v.Ordinal, $k)
    }
}
if ($null -eq $setupx) { Write-Output "note: $SetupxLib is not here; the SETUPX names were not checked against it" }
if ($null -eq $windows) { Write-Output "note: $WindowsLib is not here; the KERNEL and USER names were not checked against it" }
Write-Output ("${Path}: {0} imports by ordinal from KERNEL, USER and SETUPX, each the allowlist's" -f $imports.Count)
exit 0
