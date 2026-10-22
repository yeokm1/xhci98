#Requires -Version 2.0
<#
stage-ddk.ps1 - find the Windows 98 SE DDK members S98VSD.VXD is compiled
against and linked with, hold each to the SHA-256 docs\references\README.md
records for it, and copy them under the names the sources include into obj\
beside this script (design record 30 section 6.2):

  INC_WIN98_BASEDEF.H, _SGD.H, _BLOCKDEV.H, _IOR.H, _ISP.H, _AEP.H, _ILB.H,
  _DRP.H (incs\)              -> obj\inc\basedef.h ... drp.h
  SRC_BLOCK_INC_IOP.H, _DCB.H (all\BLCK_DDK\) -> obj\inc\iop.h, dcb.h
  BIN_LINK.EXE, BIN_MSPDB50.DLL (all\BINS_DDK\) -> obj\bin\link.exe,
                                                    mspdb50.dll

The README is the one place the hashes are written; this reads each from
the table row naming the member, so a hash cannot be changed here alone.
DDK98 names another root than tools\win98ddk-extracted\. Nothing of the DDK
is tracked, and nothing staged here reaches the package.

Two changes are made to the staged copies of the headers, never to a
tracked file: everything from the first Ctrl-Z (end-of-file mark) on is
cut, since Open Watcom reads past it; and AEP.H's three
"} AEP_x; *PAEP_x;" typedef tails, which define an implicit-int global in
every object, become "} AEP_x, *PAEP_x;". The second must match exactly
three times or the staging fails.

With -VerifyOnly nothing is copied: the members, and the linker pair
already in obj\bin\, are held to the hashes.

Exit code 0 = staged (or verified); 1 = a member missing or at another hash.
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
    @{ Member = 'INC_WIN98_BASEDEF.H';  Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\basedef.h'; Header = $true },
    @{ Member = 'INC_WIN98_SGD.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\sgd.h'; Header = $true },
    @{ Member = 'INC_WIN98_BLOCKDEV.H'; Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\blockdev.h'; Header = $true },
    @{ Member = 'INC_WIN98_IOR.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\ior.h'; Header = $true },
    @{ Member = 'INC_WIN98_ISP.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\isp.h'; Header = $true },
    @{ Member = 'INC_WIN98_AEP.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\aep.h'; Header = $true },
    @{ Member = 'INC_WIN98_ILB.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\ilb.h'; Header = $true },
    @{ Member = 'INC_WIN98_DRP.H';      Dirs = @('incs', 'all\INCS_DDK'); To = 'inc\drp.h'; Header = $true },
    @{ Member = 'SRC_BLOCK_INC_IOP.H';  Dirs = @('all\BLCK_DDK');         To = 'inc\iop.h'; Header = $true },
    @{ Member = 'SRC_BLOCK_INC_DCB.H';  Dirs = @('all\BLCK_DDK');         To = 'inc\dcb.h'; Header = $true },
    @{ Member = 'BIN_LINK.EXE';         Dirs = @('all\BINS_DDK');         To = 'bin\link.exe'; Header = $false },
    @{ Member = 'BIN_MSPDB50.DLL';      Dirs = @('all\BINS_DDK');         To = 'bin\mspdb50.dll'; Header = $false }
)

function Fail([string]$why) {
    Write-Output "ERROR: $why"
    Write-Output '       The Windows 98 SE DDK is fetched and extracted as docs\references\README.md,'
    Write-Output '       "Windows 98 DDK", says; S98VSD.VXD cannot be built without it.'
    exit 1
}

if (-not (Test-Path -LiteralPath $readme)) { Fail "no $readme to read the DDK's hashes from" }
$rows = @(Get-Content -LiteralPath $readme)

$obj = Join-Path $here 'obj'
foreach ($d in @($obj, (Join-Path $obj 'inc'), (Join-Path $obj 'bin'))) {
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
        if (-not $w.Header -and (Test-Path -LiteralPath $copy) -and
            (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToUpperInvariant() -ne $hash) {
            Fail ("{0} is not the {1} the README records; rebuild (src\vsd\build.cmd)" -f $copy, $w.Member)
        }
        Write-Output ("verified {0} (SHA-256 as recorded)" -f $w.Member)
        continue
    }
    if (-not $w.Header) {
        Copy-Item -LiteralPath $found -Destination $copy -Force
        Write-Output ("staged {0} as obj\{1} (SHA-256 as recorded)" -f $w.Member, $w.To)
        continue
    }
    $bytes = [System.IO.File]::ReadAllBytes($found)
    $cut = [Array]::IndexOf($bytes, [byte]26)
    if ($cut -ge 0) { $bytes = $bytes[0..($cut - 1)] }
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    if ($w.Member -eq 'INC_WIN98_AEP.H') {
        $re = '\} (AEP_\w+); \*(PAEP_\w+);'
        $n = ([regex]::Matches($text, $re)).Count
        if ($n -ne 3) { Fail ("{0}: {1} typedef tails of the form '}} AEP_x; *PAEP_x;', not the 3 this script mends" -f $found, $n) }
        $text = [regex]::Replace($text, $re, '} $1, *$2;')
    }
    [System.IO.File]::WriteAllBytes($copy, [System.Text.Encoding]::ASCII.GetBytes($text))
    Write-Output ("staged {0} as obj\{1} (SHA-256 as recorded; staged copy mended)" -f $w.Member, $w.To)
}
exit 0
