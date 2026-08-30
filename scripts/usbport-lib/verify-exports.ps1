<#
.SYNOPSIS
Exact-name validation of the USBPORT.SYS export list used to build usbport.lib.

.DESCRIPTION
Two checks, the second optional:

  1. usbport-stub.def's EXPORTS section must match usbport-imports.expected
     exactly - same names, same case, no extras on either side. This is a
     self-consistency gate: adding an import means editing both files on
     purpose, not by accident.
  2. If -DumpPath names a saved `dumpbin /exports` output, every expected name
     must appear in that binary's parsed export table as an exact,
     case-sensitive name. Substring matches such as USBPORT_GetHciMnEx do not
     count - that is the whole reason this is not a findstr call.

Called by scripts\make-usbport-lib.cmd, which reports failure to the caller.
Exits 0 on success and 1 with a single-line ERROR on any failure.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DefPath,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedPath,

    [string]$DumpPath = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")

function Read-ExpectedNames {
    param([string]$Path)

    $names = @(
        Get-Content -LiteralPath $Path |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -ne "" -and -not $_.StartsWith("#") }
    )

    if ($names.Count -eq 0) {
        throw "expected-export manifest is empty: $Path"
    }

    # @() around the pipeline: Sort-Object returns a bare string for a
    # one-element list, and under common.ps1's StrictMode a .Count on that
    # throws instead of reporting the real problem.
    if (@($names | Sort-Object -Unique -CaseSensitive).Count -ne $names.Count) {
        throw "expected-export manifest contains duplicate names: $Path"
    }

    return $names
}

function Read-DefExports {
    param([string]$Path)

    $names = @()
    $inExports = $false

    foreach ($line in Get-Content -LiteralPath $Path) {
        $text = ($line -replace ";.*$", "").Trim()
        if ($text -eq "") {
            continue
        }

        if (-not $inExports) {
            if ($text -match "^EXPORTS(?:\s|$)") {
                $inExports = $true
            }
            continue
        }

        $name = ($text -split "\s+")[0]
        if ($name -match "=") {
            $name = ($name -split "=", 2)[0]
        }
        $names += $name
    }

    if (-not $inExports) {
        throw "no EXPORTS section found in $Path"
    }

    if (@($names | Sort-Object -Unique -CaseSensitive).Count -ne $names.Count) {
        throw "DEF exports the same name more than once: $Path"
    }

    return $names
}

function Assert-SameNames {
    param(
        [string[]]$Expected,
        [string[]]$Actual,
        [string]$Description
    )

    $difference = @(Compare-Object $Expected $Actual -CaseSensitive)

    if ($difference.Count -ne 0) {
        $detail = ($difference | ForEach-Object {
            if ($_.SideIndicator -eq "<=") {
                "{0} (expected, missing from the DEF)" -f $_.InputObject
            } else {
                "{0} (in the DEF, not expected)" -f $_.InputObject
            }
        }) -join "; "
        throw "$Description does not exactly match the expected exports: $detail"
    }
}

function Read-DumpExportNames {
    param([string]$Path)

    # dumpbin /exports name rows are "ordinal hint RVA name"; the header and
    # summary lines never present four such columns.
    $names = @()
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match "^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)") {
            $names += $Matches[1]
        }
    }

    if ($names.Count -eq 0) {
        throw "no exports parsed out of $Path - not an export table this project can bind to"
    }

    return $names
}

try {
    $expectedNames = @(Read-ExpectedNames -Path $ExpectedPath)
    $defNames = @(Read-DefExports -Path $DefPath)
    Assert-SameNames -Expected $expectedNames -Actual $defNames `
        -Description "DEF export list"
    Write-Ok "DEF matches the tracked exact-export manifest"

    if (-not [string]::IsNullOrWhiteSpace($DumpPath)) {
        $dumpNames = @(Read-DumpExportNames -Path $DumpPath)

        foreach ($name in $expectedNames) {
            if ($dumpNames -cnotcontains $name) {
                throw "reference binary lacks exact export '$name'"
            }
        }
        Write-Ok "reference binary exports every expected name exactly"
    }
} catch {
    Write-Err $_.Exception.Message
    exit 1
}

exit 0
