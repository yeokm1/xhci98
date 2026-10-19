<#
.SYNOPSIS
Gate one built Stor98 image (src\stor98) in the gated build: its version,
its flavour marker and its imports.

.DESCRIPTION
Since 3.1.0.0 (roadmap-usb4 task 42-A.30, owner 2026-10-19) the package
bundles the Stor98 that scripts\build-driver.cmd builds from src\stor98, not
a pinned release, and every binary of the package carries one version. This
holds a Stor98 image to the checks the project's own binaries get at the
same point of the gated build:

  - Version: the version resource's fixed FileVersion and ProductVersion and
    its FileVersion string are XHCI_VER_STR of src\xhci_version.h (Stor98's
    src\stor98ver.h takes them from there).
  - Flavour marker: exactly one STOR98_FLAVOUR_* string, and it is
    STOR98_FLAVOUR_FREE (src\stor98\src\nvme2k.c): the package ships the
    free build only, never a checked or a test-only debug-port one, and the
    image carries none of xhci98's own XHCI98_FLAVOUR_* markers.
  - Machine: i386 for win9x and x86, AMD64 for amd64.
  - Imports: every DLL!function the image imports by name is listed for its
    target in scripts\import-gate\stor98-imports.allow, and it imports
    nothing by ordinal. Stor98 has no import gate of its own; this is the
    allowlist half of xhci98's. The target-evidence half (each function read
    in each target's kernel) is not applied to Stor98: its imports are the
    ScsiPort and kernel calls listed there, reviewed when one is added.

The source stamp is written by scripts\source-stamp.ps1 -Driver stor98 /
stor98mpd beside the image, as for the project's own binaries.

.PARAMETER Image
The built stor98.mpd or stor98.sys.

.PARAMETER Target
win9x (stor98.mpd), x86 or amd64 (stor98.sys).

.PARAMETER List
Print the image's imports in the allowlist's form and stop.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][ValidateSet("win9x", "x86", "amd64")][string]$Target,
    [string]$AllowPath = "",
    [switch]$List
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "common.ps1")

$repo = Split-Path -Parent $PSScriptRoot
if ($AllowPath -eq "") { $AllowPath = Join-Path $PSScriptRoot "import-gate\stor98-imports.allow" }

function Get-PeImports {
    # DLL!function for every by-name import of a PE32 or PE32+ image, and the
    # machine; a by-ordinal import is returned as DLL!#<n>.
    param([byte[]]$B)
    $pe = [BitConverter]::ToInt32($B, 0x3C)
    if ([BitConverter]::ToUInt32($B, $pe) -ne 0x00004550) { throw "not a PE image" }
    $machine = [BitConverter]::ToUInt16($B, $pe + 4)
    $nsec = [BitConverter]::ToUInt16($B, $pe + 6)
    $optSize = [BitConverter]::ToUInt16($B, $pe + 20)
    $opt = $pe + 24
    $magic = [BitConverter]::ToUInt16($B, $opt)
    $is64 = ($magic -eq 0x20B)
    $ddir = if ($is64) { $opt + 112 } else { $opt + 96 }
    $impRva = [BitConverter]::ToUInt32($B, $ddir + 8)
    $secs = @()
    $s = $opt + $optSize
    for ($i = 0; $i -lt $nsec; $i++) {
        $secs += [pscustomobject]@{
            Va = [BitConverter]::ToUInt32($B, $s + 12); Size = [Math]::Max([BitConverter]::ToUInt32($B, $s + 8), [BitConverter]::ToUInt32($B, $s + 16))
            Raw = [BitConverter]::ToUInt32($B, $s + 20)
        }
        $s += 40
    }
    $toOff = {
        param([uint32]$rva)
        foreach ($x in $secs) { if ($rva -ge $x.Va -and $rva -lt ($x.Va + $x.Size)) { return [int]($rva - $x.Va + $x.Raw) } }
        throw ("RVA 0x{0:X} is in no section" -f $rva)
    }
    $readZ = {
        param([int]$o)
        $e = $o; while ($B[$e] -ne 0) { $e++ }
        return [System.Text.Encoding]::ASCII.GetString($B, $o, $e - $o)
    }
    $out = New-Object System.Collections.Generic.List[string]
    if ($impRva -ne 0) {
        $d = & $toOff $impRva
        while ($true) {
            $ilt = [BitConverter]::ToUInt32($B, $d); $nameRva = [BitConverter]::ToUInt32($B, $d + 12)
            $iat = [BitConverter]::ToUInt32($B, $d + 16)
            if ($nameRva -eq 0) { break }
            $dll = (& $readZ (& $toOff $nameRva)).ToUpperInvariant()
            $t = & $toOff $(if ($ilt -ne 0) { $ilt } else { $iat })
            while ($true) {
                if ($is64) {
                    $v = [BitConverter]::ToUInt64($B, $t); $t += 8
                    if ($v -eq 0) { break }
                    if (($v -band 0x8000000000000000) -ne 0) { $out.Add("$dll!#" + ($v -band 0xFFFF)); continue }
                    $hint = [uint32]($v -band 0x7FFFFFFF)
                } else {
                    $v = [BitConverter]::ToUInt32($B, $t); $t += 4
                    if ($v -eq 0) { break }
                    if (($v -band 0x80000000) -ne 0) { $out.Add("$dll!#" + ($v -band 0xFFFF)); continue }
                    $hint = [uint32]$v
                }
                $out.Add("$dll!" + (& $readZ ((& $toOff $hint) + 2)))
            }
            $d += 20
        }
    }
    return [pscustomobject]@{ Machine = $machine; Imports = @($out | Sort-Object -Unique) }
}

function Get-XhciVersion {
    $h = [System.IO.File]::ReadAllText((Join-Path $repo "src\xhci_version.h"))
    $m = [regex]::Match($h, '(?m)^#define\s+XHCI_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"')
    if (-not $m.Success) { throw "src\xhci_version.h has no XHCI_VER_STR." }
    return $m.Groups[1].Value
}

try {
    if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw "no image at '$Image'." }
    $bytes = [System.IO.File]::ReadAllBytes($Image)
    $pe = Get-PeImports -B $bytes
    if ($List) {
        foreach ($i in $pe.Imports) { Write-Host ("{0} {1}" -f $Target, $i) }
        exit 0
    }
    $faults = New-Object System.Collections.Generic.List[string]

    # Version.
    $want = Get-XhciVersion
    $vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $Image).ProviderPath)
    $fixed = "{0}.{1}.{2}.{3}" -f $vi.FileMajorPart, $vi.FileMinorPart, $vi.FileBuildPart, $vi.FilePrivatePart
    $prod = "{0}.{1}.{2}.{3}" -f $vi.ProductMajorPart, $vi.ProductMinorPart, $vi.ProductBuildPart, $vi.ProductPrivatePart
    if ($fixed -ne $want) { $faults.Add("its fixed FileVersion is $fixed, and src\xhci_version.h says $want") }
    if ($prod -ne $want) { $faults.Add("its fixed ProductVersion is $prod, and src\xhci_version.h says $want") }
    if (("" + $vi.FileVersion).Trim() -ne $want) { $faults.Add("its FileVersion string is '$($vi.FileVersion)', and src\xhci_version.h says $want") }

    # Flavour marker.
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    $marks = @([regex]::Matches($text, 'STOR98_FLAVOUR_[A-Z]+') | ForEach-Object { $_.Value } | Sort-Object -Unique)
    if ($marks.Count -ne 1 -or $marks[0] -ne "STOR98_FLAVOUR_FREE") {
        $faults.Add("it carries the flavour marker(s) '" + ($(if ($marks.Count) { $marks -join ", " } else { "(none)" })) + "', not exactly STOR98_FLAVOUR_FREE: the package ships the free build only")
    }
    $xm = @(Get-XhciFlavourMarkers -Path $Image)
    if ($xm.Count -ne 0) { $faults.Add("it carries xhci98's own flavour marker(s) " + ($xm -join ", ")) }

    # Machine.
    $wantMachine = if ($Target -eq "amd64") { 0x8664 } else { 0x14C }
    if ($pe.Machine -ne $wantMachine) { $faults.Add(("its machine is 0x{0:X4}, and a {1} image is 0x{2:X4}" -f $pe.Machine, $Target, $wantMachine)) }

    # Imports.
    if (-not (Test-Path -LiteralPath $AllowPath -PathType Leaf)) { throw "no allowlist at '$AllowPath'." }
    $allowed = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($AllowPath)) {
        $l = $line.Trim()
        if ($l -eq "" -or $l.StartsWith("#")) { continue }
        $p = $l -split '\s+'
        if ($p.Count -ne 2) { throw "'$AllowPath': '$l' is not '<target> DLL!function'." }
        $allowed[($p[0].ToLowerInvariant() + " " + $p[1].ToUpperInvariant())] = $true
    }
    foreach ($i in $pe.Imports) {
        if ($i -match '!#') { $faults.Add("it imports $i by ordinal"); continue }
        if (-not $allowed.ContainsKey($Target + " " + $i.ToUpperInvariant())) {
            $faults.Add("it imports $i, which scripts\import-gate\stor98-imports.allow does not list for $Target")
        }
    }

    if ($faults.Count -gt 0) {
        Write-Host ("  {0} ({1}) FAILS:" -f $Image, $Target)
        foreach ($f in $faults) { Write-Host "    - $f" }
        exit 1
    }
    Write-Host ("  Stor98 {0}: version {1}, STOR98_FLAVOUR_FREE, {2} import(s) all allowed" -f $Target, $want, $pe.Imports.Count)
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message)
    exit 1
}
