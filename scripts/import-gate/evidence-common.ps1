Set-StrictMode -Version 2.0

# Shared manifest parsing and file-identity checking for the import gate.
#
# Every target file the gate resolves symbols against, or reports as evidence,
# is named in a tracked manifest together with its version, length, and SHA-256.
# A recorded hash that nothing compares is decoration: an unauthenticated file
# can either fail the build for the wrong reason or, worse, make the gate print
# evidence it never had (docs\contributing\lessons.md, the import-gate entry).

function Get-FileIdentityErrors {
    param(
        # Rows need Path, Label, Version, Length, Sha256. Version "-" skips the
        # version check, for files with no version resource.
        [object[]]$Rows,

        # Win98 evidence is optional by design - tools\ is git-ignored, so a
        # fresh clone legitimately has none, and a file that is simply absent
        # contributes nothing rather than being wrong. A Win2000 baseline is not
        # optional once any of the set is present.
        [switch]$SkipMissing
    )

    $errors = @()

    foreach ($row in $Rows) {
        if (-not (Test-Path -LiteralPath $row.Path)) {
            if (-not $SkipMissing) {
                $errors += "missing $($row.Label)"
            }
            continue
        }

        $file = Get-Item -LiteralPath $row.Path
        if ($file.Length -ne $row.Length) {
            $errors += "$($row.Label): length $($file.Length), expected $($row.Length)"
            continue
        }

        $version = $file.VersionInfo.FileVersion
        if ($row.Version -ne "-" -and $version -ne $row.Version) {
            if ([string]::IsNullOrWhiteSpace($version)) {
                $version = "(no version resource)"
            }
            $errors += "$($row.Label): version $version, expected $($row.Version)"
            continue
        }

        $hash = (Get-FileHash -LiteralPath $row.Path -Algorithm SHA256).Hash
        if ($hash -ine $row.Sha256) {
            $errors += "$($row.Label): SHA256 $hash, expected $($row.Sha256)"
        }
    }

    return $errors
}

function Assert-ManifestIdentityFields {
    param(
        [string]$Path,
        [int]$LineNo,
        [string]$Version,
        [string]$Length,
        [string]$Sha256
    )

    $parsed = [long]0
    if (-not [long]::TryParse($Length, [ref]$parsed) -or $parsed -le 0) {
        throw "$Path line ${LineNo}: LENGTH must be a positive integer"
    }
    if ($Sha256 -notmatch "^[0-9A-Fa-f]{64}$") {
        throw "$Path line ${LineNo}: SHA256 must be exactly 64 hexadecimal digits"
    }
    if ($Version -eq "") {
        throw "$Path line ${LineNo}: VERSION must be a version string or '-'"
    }
    return $parsed
}

# ------------------------------------------- Windows 2000 kernel/HAL images ---

function Read-Win2kBaselineManifest {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Windows 2000 baseline manifest not found: $Path"
    }

    $rows = @()
    $outputs = @{}
    $lineNo = 0

    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) {
            continue
        }

        $fields = $line -split "\s+"
        if ($fields.Count -ne 6) {
            throw "$Path line ${lineNo}: expected 'PACKED OUTPUT PROVIDER VERSION LENGTH SHA256'"
        }
        if ([System.IO.Path]::GetFileName($fields[0]) -cne $fields[0] -or
            [System.IO.Path]::GetFileName($fields[1]) -cne $fields[1]) {
            throw "$Path line ${lineNo}: PACKED and OUTPUT must be bare file names"
        }

        $provider = $fields[2].ToLower()
        if ($provider -notin @("ntoskrnl.exe", "hal.dll")) {
            throw "$Path line ${lineNo}: PROVIDER must be ntoskrnl.exe or hal.dll"
        }

        $length = Assert-ManifestIdentityFields -Path $Path -LineNo $lineNo `
            -Version $fields[3] -Length $fields[4] -Sha256 $fields[5]

        $outputKey = $fields[1].ToLower()
        if ($outputs.ContainsKey($outputKey)) {
            throw "$Path line ${lineNo}: duplicate output '$($fields[1])'"
        }
        $outputs[$outputKey] = $true

        $rows += [pscustomobject]@{
            Packed   = $fields[0]
            Out      = $fields[1]
            Provider = $provider
            Version  = $fields[3]
            Length   = $length
            Sha256   = $fields[5].ToUpper()
        }
    }

    if (@($rows | Where-Object { $_.Provider -eq "ntoskrnl.exe" }).Count -eq 0) {
        throw "$Path contains no ntoskrnl.exe provider baseline"
    }
    if (@($rows | Where-Object { $_.Provider -eq "hal.dll" }).Count -eq 0) {
        throw "$Path contains no hal.dll provider baseline"
    }

    return $rows
}

function Get-Win2kBaselineValidationErrors {
    param(
        [string]$Dir,
        [object[]]$Rows
    )

    return Get-FileIdentityErrors -Rows @(
        $Rows | ForEach-Object {
            [pscustomobject]@{
                Path    = (Join-Path $Dir $_.Out)
                Label   = $_.Out
                Version = $_.Version
                Length  = $_.Length
                Sha256  = $_.Sha256
            }
        }
    )
}

# ----------------------------------------------------- Windows 98 evidence ---

function Read-Win98EvidenceManifest {
    param(
        [string]$Path,
        [string]$RepoRoot
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Windows 98 evidence manifest not found: $Path"
    }

    $rows = @()
    $seen = @{}
    $section = ""
    $lineNo = 0

    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) {
            continue
        }
        if ($line -match "^\[(\w+)\]$") {
            $section = $Matches[1].ToLower()
            if ($section -notin @("precedent", "nametable")) {
                throw "$Path line ${lineNo}: unknown section '[$section]'"
            }
            continue
        }
        if ($section -eq "") {
            throw "$Path line ${lineNo}: content before the first [precedent] or [nametable] section"
        }

        $fields = $line -split "\s+", 5
        if ($fields.Count -lt 4) {
            throw "$Path line ${lineNo}: expected 'RELATIVE\PATH VERSION LENGTH SHA256 [note]'"
        }
        if ([System.IO.Path]::IsPathRooted($fields[0]) -or $fields[0] -match "\.\.") {
            throw "$Path line ${lineNo}: PATH must be relative to the repository root and must not escape it"
        }

        $length = Assert-ManifestIdentityFields -Path $Path -LineNo $lineNo `
            -Version $fields[1] -Length $fields[2] -Sha256 $fields[3]

        $key = $fields[0].ToLower()
        if ($seen.ContainsKey($key)) {
            throw "$Path line ${lineNo}: duplicate path '$($fields[0])'"
        }
        $seen[$key] = $true

        $note = ""
        if ($fields.Count -eq 5) {
            $note = $fields[4]
        }

        $rows += [pscustomobject]@{
            Role     = $section
            Relative = $fields[0]
            Path     = (Join-Path $RepoRoot $fields[0])
            Label    = [System.IO.Path]::GetFileName($fields[0])
            Version  = $fields[1]
            Length   = $length
            Sha256   = $fields[3].ToUpper()
            Note     = $note
        }
    }

    if (@($rows | Where-Object { $_.Role -eq "precedent" }).Count -eq 0) {
        throw "$Path lists no [precedent] binaries"
    }
    $nameTable = @($rows | Where-Object { $_.Role -eq "nametable" })
    if ($nameTable.Count -ne 1) {
        throw "$Path must list exactly one [nametable] file (ntkern.vxd), found $($nameTable.Count)"
    }

    return $rows
}
