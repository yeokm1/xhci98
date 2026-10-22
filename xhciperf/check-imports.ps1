#Requires -Version 2.0
<#
check-imports.ps1 - the import gate of XHCIPERF.EXE and XHCIPF64.EXE
(design record 29 section 10.2), xhcitr\check-imports.ps1 with this tool's
name: reads the PE image's import directory itself and lists every module
it names, then requires exactly KERNEL32, USER32, GDI32, ADVAPI32,
SHELL32, COMCTL32 and COMDLG32 (each present on Windows 98 SE) and no
delay-load imports. WNASPI32.DLL and OLE32.DLL are loaded at run time and
never imported. selftest.cmd runs it.

Exit code 0 = those seven modules alone; 1 = anything else.
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

$want = @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL', 'ADVAPI32.DLL',
          'SHELL32.DLL', 'COMCTL32.DLL', 'COMDLG32.DLL')
$got = @($names | ForEach-Object { $_.ToUpperInvariant() })
$bad = @($got | Where-Object { $want -notcontains $_ })
$missing = @($want | Where-Object { $got -notcontains $_ })
if ($bad.Count -ne 0 -or $missing.Count -ne 0 -or $got.Count -ne $want.Count) {
    Fail ("imports " + ($names -join ', ') + "; exactly " + ($want -join ', ') + " are allowed")
}
Write-Output "${Path}: imports the seven allowed modules alone"
exit 0
