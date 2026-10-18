#Requires -Version 2.0
<#
stage-ddk.ps1 - find the four Windows 98 SE DDK files XHCIDI.EXE is compiled
and linked against, hold each to the SHA-256 docs\references\README.md
records for it, and copy them under the names the sources include into
obj\ beside this script (design record 27 sections 3.2 and 3.3):

  INC_WIN98_INC16_SETUPX.H   -> obj\inc\setupx.h
  INC_WIN98_INC16_PRSHT.H    -> obj\inc\prsht.h   (setupx.h includes it)
  INC_WIN98_INC16_WINDOWS.H  -> obj\inc\windows.h (the 98 one; Open Watcom's
                                h\win\windows.h is the 3.1 one, which has
                                neither HKEY nor NMHDR, both of which
                                setupx.h and prsht.h use)
  LIB_WIN98_SETUPX.LIB       -> obj\setupx.lib

The README is the one place the hashes are written; this reads each from
the table row naming the member, so a hash cannot be changed here alone.
The files are looked for where the DDK's extraction put them in this
repository's tools\win98ddk-extracted\ (all\INCS_DDK\ or incs\ for the
headers, all\BINS_DDK\ for the library); DDK98 names another root. Nothing
of the DDK is tracked, and nothing staged here reaches the package: the
image carries import thunks to SETUPX.DLL by ordinal, no DDK byte.

With -VerifyOnly nothing is copied: the four files, and any copy already
in obj\ (what the last build compiled against), are held to the hashes.
make-release.ps1 runs that before it stages XHCIDI.EXE (record 27
section 3.3).

Exit code 0 = all four staged (or verified); 1 = a file missing or at
another hash.
#>
param([string]$Root = "", [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
if ($Root -eq '') {
    if ($env:DDK98) { $Root = $env:DDK98 } else { $Root = Join-Path $repo 'tools\win98ddk-extracted' }
}
$readme = Join-Path $repo 'docs\references\README.md'

$want = @(
    @{ Member = 'INC_WIN98_INC16_SETUPX.H';  Dirs = @('all\INCS_DDK', 'incs'); To = 'inc\setupx.h' },
    @{ Member = 'INC_WIN98_INC16_PRSHT.H';   Dirs = @('all\INCS_DDK', 'incs'); To = 'inc\prsht.h' },
    @{ Member = 'INC_WIN98_INC16_WINDOWS.H'; Dirs = @('all\INCS_DDK', 'incs'); To = 'inc\windows.h' },
    @{ Member = 'LIB_WIN98_SETUPX.LIB';      Dirs = @('all\BINS_DDK', 'bins'); To = 'setupx.lib' }
)

function Fail([string]$why) {
    Write-Output "ERROR: $why"
    Write-Output '       The Windows 98 SE DDK is fetched and extracted as docs\references\README.md,'
    Write-Output '       "Windows 98 DDK", says; XHCIDI.EXE cannot be built without it.'
    exit 1
}

if (-not (Test-Path -LiteralPath $readme)) { Fail "no $readme to read the DDK's hashes from" }
$rows = @(Get-Content -LiteralPath $readme)

$obj = Join-Path $here 'obj'
foreach ($d in @($obj, (Join-Path $obj 'inc'))) {
    if ($VerifyOnly) { break }
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
}

foreach ($w in $want) {
    $row = @($rows | Where-Object { $_ -match ('`' + [regex]::Escape($w.Member) + '`') -and $_ -match '\b[0-9a-f]{64}\b' })
    if ($row.Count -ne 1) { Fail ("docs\references\README.md has {0} rows for {1}, not one" -f $row.Count, $w.Member) }
    $hash = ([regex]::Match($row[0], '\b([0-9a-f]{64})\b')).Groups[1].Value.ToUpperInvariant()
    $found = $null
    foreach ($d in $w.Dirs) {
        $p = Join-Path (Join-Path $Root $d) $w.Member
        if (Test-Path -LiteralPath $p -PathType Leaf) { $found = $p; break }
    }
    if ($null -eq $found) { Fail ("{0} is not under {1}" -f $w.Member, $Root) }
    $got = (Get-FileHash -LiteralPath $found -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($got -ne $hash) { Fail ("{0} hashes to {1}, not the {2} the README records" -f $found, $got, $hash) }
    $copy = Join-Path $obj $w.To
    if ($VerifyOnly) {
        if ((Test-Path -LiteralPath $copy) -and
            (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToUpperInvariant() -ne $hash) {
            Fail ("{0} is not the {1} the README records; rebuild (xhciinst\di\build.cmd)" -f $copy, $w.Member)
        }
        Write-Output ("verified {0} (SHA-256 as recorded)" -f $w.Member)
        continue
    }
    Copy-Item -LiteralPath $found -Destination $copy -Force
    Write-Output ("staged {0} as obj\{1} (SHA-256 as recorded)" -f $w.Member, $w.To)
}
exit 0
