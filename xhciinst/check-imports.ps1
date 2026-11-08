#Requires -Version 2.0
<#
check-imports.ps1 - the import gate of the installer's executables (design
record 23 section 3, record 26 section 4): reads the PE image's import
directory itself, every module it names and every function it imports
from each, and requires:

  - exactly the profile's modules, and no delay-load imports:
      -Profile console  XHCIINST.EXE, XHCIIN64.EXE: KERNEL32.dll,
                        ADVAPI32.dll
      -Profile window   XHCIWIZ.EXE, XHCIWZ64.EXE: KERNEL32, USER32, GDI32,
                        ADVAPI32, COMCTL32, COMDLG32 and SHELL32, each
                        present on Windows 98 SE
      -Profile setup    SETUP.EXE: KERNEL32, USER32 and SHELL32; and, since
                        the program exists to stay unelevated, a windows-
                        subsystem image whose embedded manifest asks for
                        asInvoker (Vista's installer detection elevates a
                        SETUP.EXE that does not say so) and declares
                        Windows 7 (whose compatibility assistant questions
                        it otherwise)
  - each import, by name (or by ordinal, as "#<n>"), a row of
    xhciinst-imports.allow for the profile and the image's architecture
    (x86 or amd64, from the image's own header), and every such row
    imported (the list is the image's, not a superset of it). A module
    name alone would let through a function that only NT exports, and the
    32-bit image would then not start on Windows 98 SE; the x86 rows are
    therefore the 98 SE baseline (the list's header says where they come
    from), the amd64 rows XP x64's.

Any other module name - whatever its characters (api-ms-win-*.dll, a name
with dots) - fails, and so does an image whose import table cannot be read.
selftest.cmd runs it.

  check-imports.ps1 -Path XHCIINST.EXE -Profile console     the gate
  check-imports.ps1 -Path XHCIINST.EXE -Profile console -List
                                         print each import as a list row

Exit code 0 = the profile's modules and the list's functions alone; 1 =
anything else.
#>
param([Parameter(Mandatory = $true)][string]$Path,
      [ValidateSet('console', 'window', 'setup')][string]$Profile = 'console',
      [string]$Allow = '',
      [switch]$List)
$ErrorActionPreference = 'Stop'
if ($Allow -eq '') {
    $Allow = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'xhciinst-imports.allow'
}

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
$machine = U16 ($pe + 4)
$nsec = [int](U16 ($pe + 6))
$optSize = [int](U16 ($pe + 20))
$opt = $pe + 24
$magic = U16 $opt
if ($magic -eq 0x10B -and $machine -eq 0x14C) {
    $dd = $opt + 96; $arch = 'x86'; $thunk = 4
} elseif ($magic -eq 0x20B -and $machine -eq 0x8664) {
    $dd = $opt + 112; $arch = 'amd64'; $thunk = 8
} else {
    Fail ("an unknown machine or optional header (0x{0:X4}, 0x{1:X4})" -f $machine, $magic)
}
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
$pairs = @()
$d = RvaToOff $impRva
for ($n = 0; ; $n++) {
    if ($n -gt 256) { Fail 'an import table without its end' }
    $e = $d + 20 * $n
    $nameRva = U32 ($e + 12)
    $first = U32 ($e + 16)
    $orig = U32 $e
    if ($nameRva -eq 0 -and $first -eq 0 -and $orig -eq 0) { break }
    if ($nameRva -eq 0) { Fail 'an import entry without a name' }
    $mod = (CString (RvaToOff $nameRva)).ToUpperInvariant()
    $names += $mod
    # The lookup table, or the address table where a linker gave none (the
    # two are equal in an unbound image).
    $t = $orig
    if ($t -eq 0) { $t = $first }
    if ($t -eq 0) { Fail "$mod has no import lookup table" }
    $to = RvaToOff $t
    for ($k = 0; ; $k++) {
        if ($k -gt 4096) { Fail "$mod's lookup table has no end" }
        $lo = U32 ($to + $thunk * $k)
        $hi = 0
        if ($thunk -eq 8) { $hi = U32 ($to + $thunk * $k + 4) }
        if ($lo -eq 0 -and $hi -eq 0) { break }
        if (($thunk -eq 4 -and $lo -ge 2147483648) -or
            ($thunk -eq 8 -and $hi -ge 2147483648)) {
            # By ordinal (COMCTL32's import library binds InitCommonControls
            # as #17): listed as "#<ordinal>".
            if (($thunk -eq 4 -and $lo -ge 2147549184) -or
                ($thunk -eq 8 -and ($hi -ne 2147483648 -or $lo -ge 65536))) {
                Fail "$mod has a malformed ordinal entry"
            }
            $pairs += ("{0} #{1}" -f $mod, ($lo % 65536))
            continue
        }
        if ($thunk -eq 8 -and $hi -ne 0) { Fail "$mod has a malformed lookup entry" }
        $pairs += ("{0} {1}" -f $mod, (CString ((RvaToOff $lo) + 2)))
    }
}

# The pairs once each, compared exactly: the loader matches a function's
# name case for case, so a hashtable's default (case-blind) comparer would
# let a misspelt import through.
$uniq = New-Object System.Collections.Hashtable ([StringComparer]::Ordinal)
foreach ($p in $pairs) { $uniq[$p] = 1 }
$pairs = @($uniq.Keys | Sort-Object -CaseSensitive)

if ($List) {
    foreach ($p in $pairs) {
        $m, $f = $p.Split(' ')
        Write-Output ("{0,-8} {1,-6} {2,-13} {3}" -f $Profile, $arch, $m, $f)
    }
    exit 0
}

if ($Profile -eq 'window') {
    $want = @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL', 'ADVAPI32.DLL',
              'COMCTL32.DLL', 'COMDLG32.DLL', 'SHELL32.DLL')
} elseif ($Profile -eq 'setup') {
    $want = @('KERNEL32.DLL', 'USER32.DLL', 'SHELL32.DLL')
    if ((U16 ($opt + 68)) -ne 2) { Fail 'not a windows-subsystem image' }
    $text = [System.Text.Encoding]::ASCII.GetString($b)
    if (-not $text.Contains('<requestedExecutionLevel level="asInvoker"')) {
        Fail 'its manifest does not ask for asInvoker'
    }
    if ($text.Contains('requireAdministrator') -or $text.Contains('highestAvailable')) {
        Fail 'it names an elevated execution level'
    }
    # Windows 7's supportedOS entry, without which its Program Compatibility
    # Assistant questions SETUP.EXE once the window has closed.
    if (-not $text.Contains('<supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/>')) {
        Fail 'its manifest does not declare Windows 7 (the Program Compatibility Assistant would question it)'
    }
} else {
    $want = @('KERNEL32.DLL', 'ADVAPI32.DLL')
}
$got = @($names)
$bad = @($got | Where-Object { $want -notcontains $_ })
$missing = @($want | Where-Object { $got -notcontains $_ })
if ($bad.Count -ne 0 -or $missing.Count -ne 0 -or $got.Count -ne $want.Count) {
    Fail ("imports " + ($names -join ', ') + "; exactly " + ($want -join ', ') + " are allowed")
}

# The list's rows for this profile and architecture: "profile arch MODULE
# function"; a line whose first character is '#' is a comment.
if (-not (Test-Path -LiteralPath $Allow -PathType Leaf)) { Fail "no allowlist $Allow" }
$rows = New-Object System.Collections.Hashtable ([StringComparer]::Ordinal)
$ln = 0
foreach ($line in (Get-Content -LiteralPath $Allow)) {
    $ln++
    $t = $line.Trim()
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    $f = @($t -split '\s+')
    if ($f.Count -ne 4 -or @('console', 'window', 'setup') -notcontains $f[0] -or
        @('x86', 'amd64') -notcontains $f[1]) {
        Fail "${Allow} line ${ln} is not 'profile arch MODULE function'"
    }
    if ($f[0] -ne $Profile -or $f[1] -ne $arch) { continue }
    $key = "{0} {1}" -f $f[2].ToUpperInvariant(), $f[3]
    if ($rows.ContainsKey($key)) { Fail "${Allow} lists $Profile $arch $key twice" }
    $rows[$key] = 0
}
if ($rows.Count -eq 0) { Fail "${Allow} has no row for $Profile $arch" }
$unlisted = @()
foreach ($p in $pairs) {
    if ($rows.ContainsKey($p)) { $rows[$p] = 1 } else { $unlisted += $p }
}
$stale = @($rows.Keys | Where-Object { $rows[$_] -eq 0 } | Sort-Object -CaseSensitive)
if ($unlisted.Count -ne 0) {
    Fail ("imports what $Allow does not list for $Profile ${arch}: " + ($unlisted -join ', '))
}
if ($stale.Count -ne 0) {
    Fail ("$Allow lists for $Profile $arch what the image no longer imports: " + ($stale -join ', '))
}
Write-Output ("${Path}: imports " + ($want -join ', ') + " alone, " + $pairs.Count + " functions, each listed")
exit 0
