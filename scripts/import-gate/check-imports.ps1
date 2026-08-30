<#
.SYNOPSIS
Post-link import-compatibility gate for xhci98.sys (roadmap Phase 3 task 5).

.DESCRIPTION
An unresolved module/symbol import stops a WDM driver before DriverEntry on
both targets, with no call-site diagnostic - on Win98 the only symptom can be a
Device Manager yellow bang, indistinguishable from a bad INF
(docs\usb-xhci-info\win98-wdm.md "Imports are a silent load-time gate"). This script makes
that a build-time failure instead.

Three things happen, in order:

  1. Enforcement, always. Every module/symbol pair in the linked binary must
     appear in scripts\import-gate\xhci98-imports.allow for the build flavor
     being checked, with the USBPORT.SYS rows read from
     scripts\usbport-lib\usbport-imports.expected rather than restated. Pairs
     the allowlist marks `required` must be present. Symbols in the allowlist's
     [deny] section are reported with their specific diagnosed cause - the
     Win2K DDK's ExAllocatePool -> ExAllocatePoolWithTag rewrite is the one
     this project has already been bitten by.
  2. Win2000 resolution, when the extracted baselines are present. Every
     ntoskrnl.exe and hal.dll import must be in the export table of both SP4
     kernel images and all eight HAL images. The tracked manifest
     (win2k-baselines.expected) authenticates every file by version, length,
     and SHA-256 before any exports are trusted. A wholly absent baseline is a
     loud warning; a partial or substituted one is a failure.
  3. Win98 evidence, when the extracted files are present. Nothing host-side
     can be authoritative here: Win98's NT-style export tables are built at
     init by ntkern.vxd, not read from a file, so the real check is the load
     itself on the 2a VM (roadmap Phase 3 task 8). What this does is report,
     per import, whether a binary that ships with Win98 SE or NUSB imports the
     same pair, and whether the name appears in ntkern.vxd - positive evidence
     only. Those files are authenticated the same way, from
     win98-evidence.list: an absent one contributes nothing and is reported,
     but a present file that is not the recorded build is a failure, because
     the gate names these files as its reason for believing an import resolves.
     See the allowlist header for why absence proves nothing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\import-gate\check-imports.ps1

Checks whichever of src\objfre\i386\xhci98.sys, src\objchk\i386\xhci98.sys and
src\objchk_qemu\i386\xhci98.sys exist, inferring the flavor from the path.

.EXAMPLE
powershell -File scripts\import-gate\check-imports.ps1 -Image out\xhci98.sys -Flavor debug
#>

[CmdletBinding()]
param(
    [string[]]$Image = @(),

    [ValidateSet("auto", "release", "debug", "qemu")]
    [string]$Flavor = "auto",

    [string]$AllowPath = "",

    [string]$UsbportExpectedPath = "",

    [string]$Win2kDir = "",

    [string]$Win2kManifestPath = "",

    [string]$Win98EvidenceList = "",

    # Overrides where ntkern.vxd is read from, never what it must be: the
    # manifest's recorded identity is still enforced against whatever file this
    # names.
    [string]$NtkernPath = "",

    # Skip steps 2 and 3 entirely. For a host that has no extracted target
    # files and does not want the warnings; enforcement still runs.
    [switch]$NoTargetEvidence,

    # Parse the allowlist, print one line per row, and stop - no dumpbin, no
    # image, no evidence scan. This exists so the FLAVORS grammar can be
    # self-tested before anything is built, which is when the gate's own tests
    # run: a malformed or wrongly-flavored row must be caught by a test rather
    # than by a binary reaching a bench. scripts\import-gate\test-flavour-rules.ps1
    # is the only caller.
    [switch]$ParseOnly
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "evidence-common.ps1")

$repo = Get-RepoRoot

if ($AllowPath -eq "") {
    $AllowPath = Join-Path $PSScriptRoot "xhci98-imports.allow"
}
if ($UsbportExpectedPath -eq "") {
    $UsbportExpectedPath = Join-Path $repo "scripts\usbport-lib\usbport-imports.expected"
}
if ($Win2kDir -eq "") {
    $Win2kDir = Join-Path $repo "tools\win2ksp4-extracted"
}
if ($Win2kManifestPath -eq "") {
    $Win2kManifestPath = Join-Path $PSScriptRoot "win2k-baselines.expected"
}
if ($Win98EvidenceList -eq "") {
    $Win98EvidenceList = Join-Path $PSScriptRoot "win98-evidence.list"
}

$script:failures = @()
$script:warnings = @()

function Add-Failure {
    param([string]$Message)
    $script:failures += $Message
    Write-Err $Message
}

function Add-Warning {
    param([string]$Message)
    $script:warnings += $Message
    Write-Warn $Message
}

# ---------------------------------------------------------------- dumpbin ---

function Initialize-Dumpbin {
    $msvc = $env:MSVC6
    if ([string]::IsNullOrWhiteSpace($msvc)) {
        $msvc = Join-Path $repo "tools\MSVC600"
    }

    $dumpbin = Join-Path $msvc "VC98\BIN\dumpbin.exe"
    if (-not (Test-Path -LiteralPath $dumpbin)) {
        $found = Find-Tool "dumpbin"
        if ($null -eq $found) {
            throw "dumpbin.exe not found under '$msvc'. Run scripts\setup-msvc6.ps1, or set MSVC6."
        }
        return $found
    }

    # dumpbin 6.0 loads MSPDB60.DLL from Common\MSDev98\Bin. Without it on PATH
    # it exits 53 with no output at all - measured, and it looks
    # exactly like a binary it cannot parse.
    #
    # Membership is tested entry by entry rather than with `-notlike "*$mspdb*"`
    # (repo audit D5). That test was wrong twice over: `-like` treats `[`, `]`,
    # `*` and `?` in the *pattern* as wildcards, so a repository path containing
    # any of them stops matching itself; and a plain substring search says "yes"
    # for a PATH entry that merely contains this one as a prefix. Prepending the
    # directory twice is harmless, so both errors are quiet - which is the
    # problem. `-eq` on trimmed entries is the question actually being asked.
    $mspdb = Join-Path $msvc "Common\MSDev98\Bin"
    if (Test-Path -LiteralPath $mspdb) {
        $onPath = $false
        foreach ($entry in ($env:PATH -split ';')) {
            $trimmed = $entry.Trim().TrimEnd('\')
            if ($trimmed -ne "" -and $trimmed -eq $mspdb.TrimEnd('\')) { $onPath = $true }
        }
        if (-not $onPath) {
            $env:PATH = "$mspdb;$env:PATH"
        }
    }
    return $dumpbin
}

function Invoke-Dumpbin {
    param(
        [string]$Exe,
        [string]$Mode,
        [string]$Path
    )

    # No /nologo: dumpbin 6.0 does not know the option and warns LNK4044.
    #
    # ErrorActionPreference is relaxed across the call on purpose: in Windows
    # PowerShell 5.1 a native command's stderr line becomes an ErrorRecord, and
    # under "Stop" that surfaces as a terminating error with an *empty* message
    # instead of the diagnosis below.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $Exe $Mode $Path 2>&1
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin $Mode failed on '$Path' (exit $LASTEXITCODE). If that is 53, MSPDB60.DLL is missing from PATH."
    }
    return @($out | ForEach-Object { [string]$_ })
}

function Get-ImportPairs {
    param([string[]]$DumpLines)

    $pairs = @()
    $module = ""

    foreach ($line in $DumpLines) {
        if ($line -match "^\s+Summary\s*$") {
            break
        }
        if ($line -match "^\s{4}(\S+\.\S+)\s*$") {
            $module = $Matches[1]
            continue
        }
        if ($module -eq "") {
            continue
        }
        if ($line -match "^\s+Ordinal\s+(\d+)\s*$") {
            $pairs += [pscustomobject]@{
                Module = $module
                Symbol = "(ordinal " + $Matches[1] + ")"
                Hint   = ""
                ByName = $false
            }
            continue
        }
        if ($line -match "^\s+([0-9A-Fa-f]+)\s+([A-Za-z_?@`$][A-Za-z0-9_?@`$.]*)\s*$") {
            $pairs += [pscustomobject]@{
                Module = $module
                Symbol = $Matches[2]
                Hint   = $Matches[1]
                ByName = $true
            }
        }
    }

    return $pairs
}

function Get-ExportNames {
    param([string[]]$DumpLines)

    # "ordinal hint RVA name" rows; no header or summary line has that shape.
    $names = @()
    foreach ($line in $DumpLines) {
        if ($line -match "^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)") {
            $names += $Matches[1]
        }
    }
    return $names
}

# --------------------------------------------------------------- allowlist ---

function Read-AllowFile {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "import allowlist not found: $Path"
    }

    $allow = @()
    $deny = @{}
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
            continue
        }

        if ($section -eq "imports") {
            $fields = $line -split "\s+", 4
            if ($fields.Count -lt 3) {
                throw "$Path line ${lineNo}: expected 'MODULE!SYMBOL FLAVORS REQUIREMENT [notes]'"
            }
            if ($fields[0] -notmatch "^([^!]+)!(.+)$") {
                throw "$Path line ${lineNo}: '$($fields[0])' is not MODULE!SYMBOL"
            }
            $flavors = $fields[1].ToLower()
            # "both" was this column's word for "every flavor" while there were
            # two of them. Task 13-L.1 made three, so the word is "all" and
            # "both" is refused rather than reinterpreted: a row silently read
            # as covering three builds when it was written about two is exactly
            # the failure the third flavor exists to prevent.
            if ($flavors -eq "both") {
                throw "$Path line ${lineNo}: FLAVORS 'both' is retired - there are three flavors since task 13-L.1. Write 'all' for every flavor, or name one of release, debug, qemu."
            }
            if ($flavors -notin @("release", "debug", "qemu", "all")) {
                throw "$Path line ${lineNo}: FLAVORS must be release, debug, qemu or all"
            }
            $requirement = $fields[2].ToLower()
            if ($requirement -notin @("required", "optional")) {
                throw "$Path line ${lineNo}: REQUIREMENT must be required or optional"
            }
            $notes = ""
            if ($fields.Count -eq 4) {
                $notes = $fields[3]
            }
            $allow += [pscustomobject]@{
                Module      = $Matches[1]
                Symbol      = $Matches[2]
                Flavors     = $flavors
                Requirement = $requirement
                Notes       = $notes
                Source      = "allowlist"
            }
            continue
        }

        if ($section -eq "deny") {
            $fields = $line -split "\s+", 2
            $reason = "denied by the import allowlist"
            if ($fields.Count -eq 2) {
                $reason = $fields[1]
            }
            if ($deny.ContainsKey($fields[0])) {
                throw "$Path line ${lineNo}: '$($fields[0])' is denied twice"
            }
            $deny[$fields[0]] = $reason
            continue
        }

        throw "$Path line ${lineNo}: content outside an [imports] or [deny] section"
    }

    return [pscustomobject]@{ Allow = $allow; Deny = $deny }
}

function Read-UsbportExpected {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "USBPORT.SYS import manifest not found: $Path"
    }

    $rows = @()
    foreach ($raw in Get-Content -LiteralPath $Path) {
        $name = $raw.Trim()
        if ($name -eq "" -or $name.StartsWith("#")) {
            continue
        }
        $rows += [pscustomobject]@{
            Module      = "USBPORT.SYS"
            Symbol      = $name
            Flavors     = "all"
            Requirement = "required"
            Notes       = "read from scripts\usbport-lib\usbport-imports.expected"
            Source      = "usbport manifest"
        }
    }

    if ($rows.Count -eq 0) {
        throw "no names parsed out of $Path"
    }
    return $rows
}

# ---------------------------------------------------------------- evidence ---

function Get-Win2kBaseline {
    param(
        [string]$Dir,
        [string]$Dumpbin,
        [object[]]$ManifestRows
    )

    if (-not (Test-Path -LiteralPath $Dir)) {
        return $null
    }

    $present = @($ManifestRows | Where-Object {
        Test-Path -LiteralPath (Join-Path $Dir $_.Out)
    })
    if ($present.Count -eq 0) {
        return $null
    }

    $validationErrors = @(Get-Win2kBaselineValidationErrors -Dir $Dir -Rows $ManifestRows)
    if ($validationErrors.Count -gt 0) {
        # Three remedies, because only the first needs the install media - a host
        # that has none must still be able to build.
        $manifestFiles = (($ManifestRows | Sort-Object Out | ForEach-Object {
            "      $($_.Out)"
        }) -join "`n")
        throw @"
Windows 2000 SP4 baseline in '$Dir' is incomplete or unauthenticated:
  - $($validationErrors -join "`n  - ")
Fix it one of these ways:
  - with the recorded SP4 media: scripts\import-gate\extract-target-baselines.ps1 -Force
  - without it: delete only the manifest-owned files listed below from '$Dir';
    keep USBPORT/USBEHCI binaries, disassemblies, and every other file there.
    Removing the complete list returns this half of the gate to its
    "no baseline present" warning:
$manifestFiles
  - to skip the target-evidence steps for this run: pass -NoTargetEvidence
"@
    }

    $baseline = @{
        "ntoskrnl.exe" = @{}
        "hal.dll"      = @{}
    }
    foreach ($row in $ManifestRows) {
        $path = Join-Path $Dir $row.Out
        $baseline[$row.Provider][$row.Out] = @(
            Get-ExportNames (Invoke-Dumpbin -Exe $Dumpbin -Mode "/exports" -Path $path)
        )
    }
    return $baseline
}

function Get-Win98Precedent {
    param([object[]]$Rows, [string]$Dumpbin)

    $map = @{}
    $scanned = @()

    foreach ($row in @($Rows | Where-Object { $_.Role -eq "precedent" })) {
        if (-not (Test-Path -LiteralPath $row.Path)) {
            continue
        }

        $file = Get-Item -LiteralPath $row.Path
        $lines = $null
        try {
            $lines = Invoke-Dumpbin -Exe $Dumpbin -Mode "/imports" -Path $file.FullName
        } catch {
            # Not a PE with an import table this dumpbin can read.
            continue
        }
        $scanned += $file.Name
        foreach ($pair in Get-ImportPairs $lines) {
            if (-not $pair.ByName) {
                continue
            }
            $key = $pair.Module.ToLower() + "!" + $pair.Symbol
            if (-not $map.ContainsKey($key)) {
                $map[$key] = @()
            }
            $map[$key] += $file.Name
        }
    }

    return [pscustomobject]@{ Map = $map; Scanned = $scanned }
}

function Get-NtkernNames {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    return $text
}

function Test-NtkernName {
    param([string]$Text, [string]$Symbol)

    if ($null -eq $Text) {
        return $false
    }
    # NUL-delimited: the export name tables ntkern.vxd builds hold
    # zero-terminated strings, so this does not match a substring of a longer
    # symbol name.
    return ($Text -match ("\x00" + [regex]::Escape($Symbol) + "\x00"))
}

# -------------------------------------------------------------------- main ---

function Test-Image {
    param(
        [string]$Path,
        [string]$ImageFlavor,
        [object]$Rules,
        [string]$Dumpbin,
        [object]$Win2k,
        [object]$Precedent,
        [string]$NtkernText
    )

    Write-Step "$ImageFlavor build: $Path"

    $pairs = @(Get-ImportPairs (Invoke-Dumpbin -Exe $Dumpbin -Mode "/imports" -Path $Path))
    if ($pairs.Count -eq 0) {
        Add-Failure "$Path imports nothing at all - it cannot be a usbport miniport."
        return
    }

    $matched = @{}

    foreach ($pair in $pairs) {
        if (-not $pair.ByName) {
            Add-Failure "$($pair.Module) $($pair.Symbol): imported by ordinal. This gate can only reason about names, and an ordinal is not stable across the three usbport lineages - link by name."
            continue
        }

        if ($Rules.Deny.ContainsKey($pair.Symbol)) {
            Add-Failure "$($pair.Module)!$($pair.Symbol) is DENIED: $($Rules.Deny[$pair.Symbol])"
            continue
        }

        $row = $null
        foreach ($candidate in $Rules.Allow) {
            if ($candidate.Module -ieq $pair.Module -and $candidate.Symbol -ceq $pair.Symbol) {
                $row = $candidate
                break
            }
        }

        if ($null -eq $row) {
            $elsewhere = @($Rules.Allow | Where-Object { $_.Symbol -ceq $pair.Symbol })
            if ($elsewhere.Count -gt 0) {
                Add-Failure "$($pair.Module)!$($pair.Symbol): allowed only from $(($elsewhere | ForEach-Object { $_.Module }) -join ', '). The PE import descriptor names the provider, so this is a different import and only one of them resolves."
            } else {
                Add-Failure "$($pair.Module)!$($pair.Symbol): not in the allowlist. Add it to scripts\import-gate\xhci98-imports.allow only with target evidence that it resolves - see that file's header."
            }
            continue
        }

        if ($row.Flavors -ne "all" -and $row.Flavors -ne $ImageFlavor) {
            Add-Failure "$($pair.Module)!$($pair.Symbol): allowed in the $($row.Flavors) build only, but the $ImageFlavor build imports it."
            continue
        }

        $matched[($row.Module.ToLower() + "!" + $row.Symbol)] = $true

        $evidence = @()

        if ($null -ne $Win2k) {
            $moduleKey = ""
            if ($pair.Module -ieq "ntoskrnl.exe") { $moduleKey = "ntoskrnl.exe" }
            if ($pair.Module -ieq "hal.dll") { $moduleKey = "hal.dll" }

            if ($moduleKey -ne "") {
                $absent = @()
                foreach ($binary in $Win2k[$moduleKey].Keys) {
                    if ($Win2k[$moduleKey][$binary] -cnotcontains $pair.Symbol) {
                        $absent += $binary
                    }
                }
                if ($absent.Count -gt 0) {
                    Add-Failure "$($pair.Module)!$($pair.Symbol) is not exported by Windows 2000 SP4 $($absent -join ', ') - the driver cannot load on the co-primary target."
                } else {
                    $evidence += "w2k-export"
                }
            }
        }

        if ($pair.Module -ieq "USBPORT.SYS") {
            $evidence += "usbport manifest"
        }

        if ($null -ne $Precedent) {
            $key = $pair.Module.ToLower() + "!" + $pair.Symbol
            if ($Precedent.Map.ContainsKey($key)) {
                $evidence += "win98-precedent " + (($Precedent.Map[$key] | Sort-Object -Unique) -join "/")
            }
        }

        if (Test-NtkernName -Text $NtkernText -Symbol $pair.Symbol) {
            $evidence += "ntkern-name"
        }

        $shown = "none host-side"
        if ($evidence.Count -gt 0) {
            $shown = $evidence -join "; "
        }
        Write-Host ("  {0,-14} {1,-30} hint {2,-4} [{3}]" -f $pair.Module, $pair.Symbol, $pair.Hint, $shown)

        if ($evidence.Count -eq 0) {
            Add-Warning "$($pair.Module)!$($pair.Symbol) has no host-side target evidence in this working copy. The allowlist row claims: $($row.Notes)"
        }
    }

    foreach ($row in $Rules.Allow) {
        if ($row.Requirement -ne "required") {
            continue
        }
        if ($row.Flavors -ne "all" -and $row.Flavors -ne $ImageFlavor) {
            continue
        }
        if (-not $matched.ContainsKey(($row.Module.ToLower() + "!" + $row.Symbol))) {
            Add-Failure "$($row.Module)!$($row.Symbol) is required in the $ImageFlavor build but is not imported ($($row.Source))."
        }
    }
}

if ($ParseOnly) {
    try {
        $parsed = Read-AllowFile -Path $AllowPath
    } catch {
        Write-Err $_.Exception.Message
        exit 1
    }
    foreach ($row in $parsed.Allow) {
        Write-Host ("allow {0}!{1} {2} {3}" -f $row.Module, $row.Symbol, $row.Flavors, $row.Requirement)
    }
    foreach ($key in ($parsed.Deny.Keys | Sort-Object)) {
        Write-Host ("deny {0}" -f $key)
    }
    exit 0
}

try {
    $dumpbin = Initialize-Dumpbin

    $rules = Read-AllowFile -Path $AllowPath
    $usbportRows = Read-UsbportExpected -Path $UsbportExpectedPath
    $rules = [pscustomobject]@{
        Allow = @($rules.Allow + $usbportRows)
        Deny  = $rules.Deny
    }
    Write-Ok ("allowlist: {0} pairs ({1} read from usbport-imports.expected), {2} denied symbols" -f `
        $rules.Allow.Count, $usbportRows.Count, $rules.Deny.Count)

    $images = @()
    if ($Image.Count -gt 0) {
        foreach ($path in $Image) {
            if (-not (Test-Path -LiteralPath $path)) {
                throw "image not found: $path"
            }
            $images += (Resolve-Path -LiteralPath $path).Path
        }
    } else {
        foreach ($candidate in @("src\objfre\i386\xhci98.sys",
                                 "src\objchk\i386\xhci98.sys",
                                 "src\objchk_qemu\i386\xhci98.sys")) {
            $path = Join-Path $repo $candidate
            if (Test-Path -LiteralPath $path) {
                $images += $path
            }
        }
        if ($images.Count -eq 0) {
            throw "no built driver found under src\objfre, src\objchk or src\objchk_qemu. Build first (scripts\build-driver.cmd), or pass -Image."
        }
    }

    $win2k = $null
    $precedent = $null
    $ntkernText = $null

    if ($NoTargetEvidence) {
        # The evidence manifests are not even read here: -NoTargetEvidence must
        # work on a host that has no target files at all, and this switch is the
        # allowlist-only path. test-evidence-manifests.ps1 keeps those tracked
        # files honest on every build regardless.
        Add-Warning "-NoTargetEvidence: only the committed allowlist was enforced. Nothing checked that these symbols exist on either target."
    } else {
        $win2kManifest = @(Read-Win2kBaselineManifest -Path $Win2kManifestPath)
        $win98Manifest = @(Read-Win98EvidenceManifest -Path $Win98EvidenceList -RepoRoot $repo)

        # -NtkernPath relocates the name-table file; the manifest's recorded
        # identity still has to match whatever it names.
        $nameTableRow = @($win98Manifest | Where-Object { $_.Role -eq "nametable" })[0]
        if ($NtkernPath -ne "") {
            $resolved = (Resolve-Path -LiteralPath $NtkernPath -ErrorAction SilentlyContinue)
            if ($null -eq $resolved) {
                $nameTableRow.Path = $NtkernPath
            } else {
                $nameTableRow.Path = $resolved.Path
            }
        }
        $NtkernPath = $nameTableRow.Path

        $win2k = Get-Win2kBaseline -Dir $Win2kDir -Dumpbin $dumpbin -ManifestRows $win2kManifest
        if ($null -eq $win2k) {
            Add-Warning "none of the authenticated Windows 2000 SP4 kernel/HAL baselines are present in '$Win2kDir' - the Win2000 half of this gate did not run. Recreate them with scripts\import-gate\extract-target-baselines.ps1."
        } else {
            Write-Ok ("Win2000 baseline: kernels {0}; HALs {1} (versions, lengths and SHA256 authenticated)" -f `
                (($win2k["ntoskrnl.exe"].Keys | Sort-Object) -join ", "), `
                (($win2k["hal.dll"].Keys | Sort-Object) -join ", "))
        }

        # Identity before use, on both roles. A present-but-different file is a
        # failure rather than weaker evidence: the gate prints these files by
        # name as the reason an import is believed to resolve on Win98, and that
        # line gets quoted in the phase records.
        $win98Errors = @(Get-FileIdentityErrors -Rows $win98Manifest -SkipMissing)
        if ($win98Errors.Count -gt 0) {
            throw @"
Win98 evidence files do not match '$Win98EvidenceList':
  - $($win98Errors -join "`n  - ")
Fix it one of these ways:
  - re-stage the recorded build from the Win98 SE CD or the NUSB package
  - delete the offending file: an absent evidence file contributes nothing and
    is reported, which is safe - a wrong one would be quoted as evidence
  - update the manifest row deliberately, if the new file really is the build
    this project now targets
  - to skip the target-evidence steps for this run: pass -NoTargetEvidence
"@
        }

        $precedent = Get-Win98Precedent -Rows $win98Manifest -Dumpbin $dumpbin
        if ($precedent.Scanned.Count -eq 0) {
            Add-Warning "none of the [precedent] binaries in '$Win98EvidenceList' are present - the Win98 precedent scan did not run."
            $precedent = $null
        } else {
            Write-Ok ("Win98 precedent set: {0} of the drivers in win98-evidence.list, authenticated ({1})" -f `
                $precedent.Scanned.Count, (($precedent.Scanned | Sort-Object) -join ", "))
        }

        $ntkernText = Get-NtkernNames -Path $NtkernPath
        if ($null -eq $ntkernText) {
            Add-Warning "no ntkern.vxd at '$NtkernPath' - the Win98 export-name scan did not run."
        } else {
            Write-Ok "Win98 ntkern.vxd name table available, authenticated (positive evidence only)"
        }
    }

    foreach ($path in $images) {
        $imageFlavor = $Flavor
        if ($imageFlavor -eq "auto") {
            #
            # **The obj directory is READ OUT OF THE LAYOUT, not searched for
            # in the path**, and it took three attempts to get there. build.exe
            # writes `obj<BUILD_ALT_DIR>\<arch>\<target>`, so the flavour is the
            # image's grandparent directory and nothing else - one exact
            # comparison, with no substring anywhere in it.
            #
            # Both earlier forms were substring matches and both were wrong:
            #
            #   `objchk[\/]`  never matched at all. .NET reads `\/` inside a
            #                 class as an escaped FORWARD slash, so it could not
            #                 match the backslash every path here contains, and
            #                 the documented no-argument invocation discovered
            #                 its images and then refused to infer their
            #                 flavour.
            #   `[\\/]objchk[\\/]`
            #                 matched an ANCESTOR. A clone under `D:\objchk\`
            #                 had its own `...\src\objfre\i386\xhci98.sys` gated
            #                 as debug - and testing the more specific name
            #                 first does not help, because it moves the same
            #                 failure to a clone under `D:\objchk_qemu\`. There
            #                 is no ordering of substring tests that survives a
            #                 path containing two flavour names.
            #
            # Getting this wrong is not cosmetic: the whole point of the flavour
            # is which import rules a binary is held to, and
            # HAL.dll!WRITE_PORT_UCHAR being `qemu required` is what keeps it
            # out of a published build.
            #
            $objDir = Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $path))
            switch ($objDir) {
                "objfre"      { $imageFlavor = "release" }
                "objchk"      { $imageFlavor = "debug" }
                "objchk_qemu" { $imageFlavor = "qemu" }
                default {
                    throw "cannot infer the build flavor of '$path': its grandparent directory is '$objDir', not one of objfre, objchk or objchk_qemu. Pass -Flavor release, -Flavor debug or -Flavor qemu."
                }
            }
        }
        Test-Image -Path $path -ImageFlavor $imageFlavor -Rules $rules -Dumpbin $dumpbin `
            -Win2k $win2k -Precedent $precedent -NtkernText $ntkernText
    }
} catch {
    Write-Err $_.Exception.Message
    exit 1
}

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Err ("import gate FAILED: {0} problem(s)." -f $script:failures.Count)
    Write-Host "Do not deploy this binary. An unresolved or wrong-module import stops"
    Write-Host "the driver before DriverEntry, and on Win98 the only symptom may be a"
    Write-Host "Device Manager yellow bang (docs\usb-xhci-info\win98-wdm.md)."
    exit 1
}

if ($script:warnings.Count -gt 0) {
    Write-Warn ("import gate passed with {0} warning(s) - read them: a warning here means an evidence source was missing, not that it agreed." -f $script:warnings.Count)
} else {
    Write-Ok "import gate PASSED"
}
exit 0
