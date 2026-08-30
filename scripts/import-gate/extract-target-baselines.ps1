<#
.SYNOPSIS
Stage the target files the import gate resolves symbols against.

.DESCRIPTION
scripts\import-gate\check-imports.ps1 always enforces the committed allowlist,
but its two target-evidence steps need files that cannot be committed: tools\
is git-ignored and these are OS binaries. This script recreates them from the
install media, so a fresh clone (or the project's other host) gets the same
evidence rather than a warning.

  tools\win2ksp4-extracted\ (two kernel images and eight HAL images)
      Expanded from the Windows 2000 SP4 ISO. Win2000 resolves driver imports
      against the kernel/HAL pair Setup selected, so the tracked manifest
      covers the UP and SMP kernels plus every HAL on the media, including the
      ACPI variants real hardware can use and the one uncompressed image
      (HALBORG.DLL) that a HAL*.DL_ pattern misses. Version, size, and SHA-256
      must match scripts\import-gate\win2k-baselines.expected.

  tools\win98se-extracted\ntkern.vxd
      Extracted from WIN98_54.CAB on the Win98 SE CD (the cab is named by
      layout.inf's "ntkern.vxd=54"). Win98 builds its NT-style export tables at
      init from this file rather than exposing an export table on disk, so it
      is positive evidence only - see the allowlist header, and note that
      Win98 SE's own usbd.sys imports names this file does not contain.
      Its identity is recorded in scripts\import-gate\win98-evidence.list along
      with the Win98/NUSB precedent binaries, which are staged elsewhere rather
      than by this script: the NUSB set by the Phase 2a package extraction, and
      Win98 SE's own usbd.sys/usbhub.sys by
      scripts\package\extract-usbd-sources.ps1, which needs usbd.sys anyway
      because xhci98.inf carries it.

Existing files are left alone unless -Force is given. Nothing here touches the
VMs or the driver build.

Each ISO parameter is required to stage *its own* target, and neither has a
default: where a person keeps installation media is a property of their machine,
and a committed default is a path that is right on exactly one host and wrong
everywhere else.  Omitting one is not an error - it skips that half of the
staging with a warning, exactly as an unreadable path does, and the gate then
reports that the half did not run.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\import-gate\extract-target-baselines.ps1 `
    -Win2KIso D:\isos\win2ksp4-retail.ISO -Win98Iso D:\isos\w98se-oem.iso
#>

[CmdletBinding()]
param(
    [string]$Win2KIso = "",
    [string]$Win98Iso = "",
    [string]$Win2kManifestPath = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "evidence-common.ps1")

$repo = Get-RepoRoot
$w2kDir = Join-Path $repo "tools\win2ksp4-extracted"
$w98Dir = Join-Path $repo "tools\win98se-extracted"
$work = Join-Path $env:TEMP ("xhci98-baselines-" + [System.IO.Path]::GetRandomFileName())
if ($Win2kManifestPath -eq "") {
    $Win2kManifestPath = Join-Path $PSScriptRoot "win2k-baselines.expected"
}

$sevenZip = Find-Tool "7z"
if ($null -eq $sevenZip) {
    Write-Err "7z not found on PATH. Install it (scoop install 7zip) - expand.exe alone cannot read an ISO."
    exit 1
}
$expand = Join-Path $env:SystemRoot "system32\expand.exe"
if (-not (Test-Path -LiteralPath $expand)) {
    Write-Err "expand.exe not found; it is needed to decompress the media's NTOSKRNL.EX_/HAL.DL_ files."
    exit 1
}

function Invoke-SevenZip {
    param([string[]]$Arguments)

    # Two Windows PowerShell 5.1 traps in one call site. A native command's
    # stderr is wrapped in ErrorRecords, so under $ErrorActionPreference =
    # "Stop" any diagnostic 7z prints becomes a terminating error with an empty
    # message; and 7z exits nonzero for the Win98 CD's absent neighbouring cab
    # volumes even when it extracted what was asked for. So: let errors through
    # locally, swallow the output, and let every caller decide by checking for
    # the file it wanted.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $sevenZip @Arguments 2>&1 | Out-Null
    } finally {
        $ErrorActionPreference = $saved
    }
}

function Get-NtkernIdentityErrors {
    # Authenticates whatever file is handed to it against the [nametable] row of
    # win98-evidence.list. Takes a path rather than reading the staged copy so
    # the *candidate* can be checked before it is installed - see the stage-time
    # call site. An empty manifest section is not an error: the row is what makes
    # the check possible, and its absence is a manifest question, not a media one.
    param([string]$Path)

    $nameRow = @(
        @(Read-Win98EvidenceManifest -Path (Join-Path $PSScriptRoot "win98-evidence.list") -RepoRoot $repo) |
            Where-Object { $_.Role -eq "nametable" }
    )
    if ($nameRow.Count -eq 0) {
        return @()
    }
    $nameRow[0].Path = $Path
    return @(Get-FileIdentityErrors -Rows $nameRow)
}

function Report-File {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path
    $version = $item.VersionInfo.FileVersion
    if ([string]::IsNullOrWhiteSpace($version)) {
        $version = "(no version resource)"
    }
    Write-Host ("  {0,-14} {1,9} B  {2,-18} sha256 {3}" -f `
        $item.Name, $item.Length, $version, (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash)
}

try {
    Ensure-Directory $work
    Ensure-Directory $w2kDir
    Ensure-Directory $w98Dir

    # --- Windows 2000 SP4 kernel and HALs -----------------------------------
    Write-Step "Windows 2000 SP4 kernel and HAL export baselines"

    $w2kWanted = @(Read-Win2kBaselineManifest -Path $Win2kManifestPath)

    $w2kMissing = @($w2kWanted | Where-Object { $Force -or -not (Test-Path -LiteralPath (Join-Path $w2kDir $_.Out)) })

    if ($w2kMissing.Count -eq 0) {
        Write-Ok "already staged (pass -Force to redo)"
        foreach ($w in $w2kWanted) { Report-File (Join-Path $w2kDir $w.Out) }
    } elseif ($Win2KIso -eq "") {
        Write-Warn "no -Win2KIso given - skipping. The gate will warn that its Win2000 half did not run."
    } elseif (-not (Test-Path -LiteralPath $Win2KIso)) {
        Write-Warn "Windows 2000 ISO not found at '$Win2KIso' - skipping. The gate will warn that its Win2000 half did not run."
    } else {
        Invoke-SevenZip (@("e", "-y", "-o$work", $Win2KIso) + @($w2kMissing | ForEach-Object { "I386\" + $_.Packed }))
        foreach ($w in $w2kMissing) {
            $packed = Join-Path $work $w.Packed
            if (-not (Test-Path -LiteralPath $packed)) {
                throw "'$($w.Packed)' is not on '$Win2KIso' under I386\"
            }
            # HALBORG.DLL is the one kernel HAL the media ships uncompressed, so
            # it has nothing to expand - and it is exactly the file a HAL*.DL_
            # enumeration misses. The manifest marks the difference by whether
            # the packed name ends in "_".
            if ($w.Packed.EndsWith("_")) {
                & $expand $packed (Join-Path $w2kDir $w.Out) | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw "expand failed on '$packed' (exit $LASTEXITCODE)"
                }
            } else {
                Copy-Item -LiteralPath $packed -Destination (Join-Path $w2kDir $w.Out) -Force
            }
        }
        Write-Ok "staged into $w2kDir"
        foreach ($w in $w2kWanted) { Report-File (Join-Path $w2kDir $w.Out) }
    }

    $w2kPresent = @($w2kWanted | Where-Object {
        Test-Path -LiteralPath (Join-Path $w2kDir $_.Out)
    })
    if ($w2kPresent.Count -gt 0) {
        $validationErrors = @(Get-Win2kBaselineValidationErrors -Dir $w2kDir -Rows $w2kWanted)
        if ($validationErrors.Count -gt 0) {
            $manifestFiles = (($w2kWanted | Sort-Object Out | ForEach-Object {
                "      $($_.Out)"
            }) -join "`n")
            throw @"
staged Windows 2000 baseline does not match '$Win2kManifestPath':
  - $($validationErrors -join "`n  - ")
Fix it one of these ways:
  - re-run this script with -Force and the recorded SP4 media
  - without the media: delete only the manifest-owned files listed below from
    '$w2kDir'; keep USBPORT/USBEHCI binaries, disassemblies, and every other
    file there. Removing the complete list returns the import gate to its
    "no baseline present" warning:
$manifestFiles
"@
        }
        Write-Ok "all Windows 2000 kernel/HAL files match the authenticated manifest"
    }

    # --- Win98 SE ntkern.vxd ------------------------------------------------
    Write-Step "Win98 SE ntkern.vxd (NT-style export name table)"

    $ntkern = Join-Path $w98Dir "ntkern.vxd"
    if ((Test-Path -LiteralPath $ntkern) -and -not $Force) {
        # The early-out authenticates too. An unchecked "already staged" OK over
        # a file a previous run had condemned is worse than no message at all -
        # it is an affirmative claim about a file nothing looked at.
        $stagedErrors = @(Get-NtkernIdentityErrors -Path $ntkern)
        if ($stagedErrors.Count -gt 0) {
            throw @"
the already-staged tools\win98se-extracted\ntkern.vxd is not the recorded build:
  - $($stagedErrors -join "`n  - ")
Delete it and re-run this script with -Force and the recorded Windows 98 SE
media, or update the [nametable] row in scripts\import-gate\win98-evidence.list
deliberately - it is the file the gate quotes by name as the reason a Win98
import is believed.
"@
        }
        Write-Ok "already staged and matched against win98-evidence.list (pass -Force to redo)"
        Report-File $ntkern
    } elseif ($Win98Iso -eq "") {
        Write-Warn "no -Win98Iso given - skipping. The gate will warn that its ntkern.vxd scan did not run."
    } elseif (-not (Test-Path -LiteralPath $Win98Iso)) {
        Write-Warn "Win98 SE ISO not found at '$Win98Iso' - skipping. The gate will warn that its ntkern.vxd scan did not run."
    } else {
        # PRECOPY2 is staged alongside PRECOPY1 because layout.inf spans the two
        # volumes; see Invoke-SevenZip for why nothing here reads an exit code.
        Invoke-SevenZip @("e", "-y", "-o$work", $Win98Iso, "win98/PRECOPY1.CAB", "win98/PRECOPY2.CAB")
        if (-not (Test-Path -LiteralPath (Join-Path $work "PRECOPY1.CAB"))) {
            throw "7z could not read win98\PRECOPY1.CAB out of '$Win98Iso'"
        }

        # The cab holding ntkern.vxd comes from the media's own layout.inf
        # ("ntkern.vxd=54" on the SE OEM CD) rather than being hard-coded: an
        # OEM/retail layout difference would otherwise silently stage nothing.
        Invoke-SevenZip @("e", "-y", "-o$work", (Join-Path $work "PRECOPY1.CAB"), "layout.inf")
        if (-not (Test-Path -LiteralPath (Join-Path $work "layout.inf"))) {
            throw "could not extract layout.inf from PRECOPY1.CAB"
        }

        $layout = Get-Content -LiteralPath (Join-Path $work "layout.inf")
        $diskId = ""
        foreach ($line in $layout) {
            if ($line -match "^\s*ntkern\.vxd\s*=\s*(\d+)\s*,") {
                $diskId = $Matches[1]
                break
            }
        }
        if ($diskId -eq "") {
            throw "layout.inf does not name a source disk for ntkern.vxd"
        }

        $cabName = ""
        foreach ($line in $layout) {
            if ($line -match ("^\s*" + $diskId + "\s*=\s*[^,]+,\s*""([^""]+)""")) {
                $cabName = $Matches[1]
                break
            }
        }
        if ($cabName -eq "") {
            throw "layout.inf disk $diskId has no cab name"
        }
        Write-Host "  layout.inf: ntkern.vxd is on disk $diskId = $cabName"

        Invoke-SevenZip @("e", "-y", "-o$work", $Win98Iso, ("win98/" + $cabName))
        if (-not (Test-Path -LiteralPath (Join-Path $work $cabName))) {
            throw "7z could not read win98\$cabName out of '$Win98Iso'"
        }
        Invoke-SevenZip @("e", "-y", "-o$work\ntkern", (Join-Path $work $cabName), "ntkern.vxd")
        $staged = Join-Path $work "ntkern\ntkern.vxd"
        if (-not (Test-Path -LiteralPath $staged)) {
            throw "ntkern.vxd was not extracted from $cabName"
        }
        #
        # **Authenticated at stage time, like the Windows 2000 half above** (repo
        # audit D6). `check-imports.ps1` does validate this file against
        # `win98-evidence.list` before quoting it as evidence, so a wrong build
        # was never believed - but it was found by the *next build*, with a
        # failure that names the manifest rather than the media it came from, and
        # by then the ISO the operator had mounted is out of the picture. The
        # check belongs where the answer is actionable.
        #
        # It is made against the extracted *temp* file, before the copy: a check
        # that runs after the install has already installed the file it condemns,
        # and leaves it there for the next run's early-out to bless.
        #
        $ntkernErrors = @(Get-NtkernIdentityErrors -Path $staged)
        if ($ntkernErrors.Count -gt 0) {
            throw @"
the ntkern.vxd extracted from '$Win98Iso' is not the recorded build:
  - $($ntkernErrors -join "`n  - ")
Nothing was staged. That media is a different Windows 98 build from the one this
project's evidence is recorded against. Either stage from the recorded media, or
update the [nametable] row in scripts\import-gate\win98-evidence.list
deliberately - it is the file the gate quotes by name as the reason a Win98
import is believed.
"@
        }

        Copy-Item -LiteralPath $staged -Destination $ntkern -Force
        Write-Ok "staged into $w98Dir and matched against win98-evidence.list"
        Report-File $ntkern
    }

    Write-Step "Done"
    Write-Host "Next: scripts\import-gate\check-imports.ps1 (or scripts\build-driver.cmd,"
    Write-Host "which runs it after every link)."
} catch {
    Write-Err $_.Exception.Message
    exit 1
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0
