#Requires -Version 2.0
<#
check-imports.ps1 - the import gate of XHCIMARK.EXE and XHCIMK64.EXE (design
record 21 section 7.4, revision 3, built and gated as XHCIINST is):
reads the PE image's import directory itself and
lists every module it names, then requires exactly KERNEL32.dll and
ADVAPI32.dll and no delay-load imports. Any other name - whatever its
characters (api-ms-win-*.dll, a name with dots) - fails, and so does an
image whose import table cannot be read. selftest.cmd runs it.

Exit code 0 = the two modules alone; 1 = anything else.
#>
param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'

function Fail([string]$why) {
    Write-Output "ERROR: ${Path}: $why"
    exit 1
}

try {
    $b = [System.IO.File]::ReadAllBytes($Path)
} catch {
    Fail 'the image cannot be read'
}
function U16([int]$o) { if ($o + 2 -gt $b.Length) { Fail 'truncated image' }; return [BitConverter]::ToUInt16($b, $o) }
function U32([int]$o) { if ($o + 4 -gt $b.Length) { Fail 'truncated image' }; return [BitConverter]::ToUInt32($b, $o) }

if ($b.Length -lt 64 -or (U16 0) -ne 0x5A4D) { Fail 'not an MZ image' }
$pe = [int](U32 0x3C)
if ((U32 $pe) -ne 0x00004550) { Fail 'not a PE image' }
$nsec = [int](U16 ($pe + 6))
$optSize = [int](U16 ($pe + 20))
$opt = $pe + 24
$magic = U16 $opt
if ($magic -eq 0x10B) { $dd = $opt + 96 } elseif ($magic -eq 0x20B) { $dd = $opt + 112 } else { Fail 'unknown optional header' }
$ndirs = [int](U32 ($dd - 4))
$secs = $opt + $optSize

function RvaToOff([uint32]$rva) {
    for ($i = 0; $i -lt $nsec; $i++) {
        $s = $secs + 40 * $i
        $va = U32 ($s + 12)
        $vs = U32 ($s + 8)
        $rs = U32 ($s + 16)
        $size = [Math]::Max([uint64]$vs, [uint64]$rs)
        if ([uint64]$rva -ge $va -and [uint64]$rva -lt [uint64]$va + $size) {
            return [int]($rva - $va + (U32 ($s + 20)))
        }
    }
    Fail ("an RVA (0x{0:X8}) outside every section" -f $rva)
}

function CString([int]$o) {
    $e = $o
    while ($e -lt $b.Length -and $b[$e] -ne 0) { $e++ }
    if ($e -ge $b.Length) { Fail 'an unterminated name' }
    return [System.Text.Encoding]::ASCII.GetString($b, $o, $e - $o)
}

if ($ndirs -lt 2) { Fail 'no import directory' }
$impRva = U32 ($dd + 8)
if ($impRva -eq 0) { Fail 'no import directory' }
if ($ndirs -gt 13 -and (U32 ($dd + 13 * 8)) -ne 0) { Fail 'it has delay-load imports' }

$names = @()
$d = RvaToOff $impRva
for ($n = 0; ; $n++) {
    if ($n -gt 256) { Fail 'an import table without its end' }
    $e = $d + 20 * $n
    $nameRva = U32 ($e + 12)
    $first = U32 ($e + 16)
    $orig = U32 $e
    if ($nameRva -eq 0 -and $first -eq 0 -and $orig -eq 0) { break }
    if ($nameRva -eq 0) { Fail 'an import entry without a name' }
    $names += (CString (RvaToOff $nameRva))
}

$want = @('KERNEL32.DLL', 'ADVAPI32.DLL')
$got = @($names | ForEach-Object { $_.ToUpperInvariant() })
$bad = @($got | Where-Object { $want -notcontains $_ })
if ($bad.Count -ne 0 -or $got.Count -ne 2 -or
    ($got -notcontains 'KERNEL32.DLL') -or ($got -notcontains 'ADVAPI32.DLL')) {
    Fail ("imports " + ($names -join ', ') + "; only KERNEL32.dll and ADVAPI32.dll are allowed")
}
Write-Output "${Path}: imports KERNEL32 and ADVAPI32 alone"
exit 0
