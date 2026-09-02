<#
.SYNOPSIS
Stage the reference copies of each target's own usbd.sys (and Windows 98 SE's
usbhub.sys) under the git-ignored tools\, from that OS's install media.

.DESCRIPTION
These files are NOT install media and are not packaged. Until release 1.0.0.1
the package carried them under per-target names, authenticated against a
manifest this script also read; since 1.0.0.1 the INF has the Windows setup
engine copy usbd.sys and usbhub.sys from the operating system's own install
source (LayoutFile=layout.inf), and the media carries no Microsoft file
(docs\contributing\legal-provenance.md section 5). What is staged here is
what the repository still reads them for:

  tools\win98se-extracted\usbd.sys, usbhub.sys
      Windows 98 SE 4.10.2222, from the SE CD. The import gate's Windows 98
      precedent pair: scripts\import-gate\win98-evidence.list names both with
      their identity, and check-imports.ps1 reads their import tables as
      evidence of what resolves on Windows 98. The cab is found through the
      media's own layout.inf rather than being hard-coded, so an OEM/retail
      layout difference fails loudly.

  tools\win2ksp4-extracted\USBD.SYS
      Windows 2000 SP4 5.00.2195.6658, expanded from I386\USBD.SY_ on the
      retail SP4 ISO. What scripts\setup-qemu-win2k.ps1 stages for the
      Windows 2000 VM's preparation boot, and what the ABI work read
      statically (legal-provenance.md section 4).

Every staged file is verified before the script reports success: the Windows
98 pair against win98-evidence.list, the Windows 2000 file against the
identity recorded below. Existing files are left alone unless -Force is
given. Nothing here touches the VMs, the driver build, or the package.

Each ISO parameter is required to stage its own target, and neither has a
default: where a person keeps installation media is a property of their
machine. Omitting one skips that target with a warning; omitting both, on a
host where nothing is staged yet, is an error, because the run would
otherwise report success having staged nothing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\extract-usbd-sources.ps1 `
    -Win2KIso D:\isos\win2ksp4-retail.ISO -Win98Iso D:\isos\w98se-oem.iso
#>

[CmdletBinding()]
param(
    [string]$Win2KIso = "",
    [string]$Win98Iso = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "import-gate") "evidence-common.ps1")

$repo = Get-RepoRoot
$work = Join-Path $env:TEMP ("xhci98-usbd-" + [System.IO.Path]::GetRandomFileName())

# The Windows 2000 file's identity, as the retail SP4 ISO ships it. The
# Windows 98 pair's identity lives in the import gate's evidence list, which
# is read below rather than restated here.
$w2kIdentity = [pscustomobject]@{
    Path    = (Join-Path $repo "tools\win2ksp4-extracted\USBD.SYS")
    Label   = "tools\win2ksp4-extracted\USBD.SYS (Windows 2000 SP4)"
    Version = "5.00.2195.6658"
    Length  = 20688
    Sha256  = "45EE7C3D552E31BD21512E7C1D0190512492A8ADC9EE67B4A84D4A5D8EEB8927"
}

$sevenZip = Find-Tool "7z"
if ($null -eq $sevenZip) {
    Write-Err "7z not found on PATH. Install it (scoop install 7zip) - expand.exe alone cannot read an ISO or a Win98 cab."
    exit 1
}
$expand = Join-Path $env:SystemRoot "system32\expand.exe"
if (-not (Test-Path -LiteralPath $expand)) {
    Write-Err "expand.exe not found; it is needed to decompress I386\USBD.SY_."
    exit 1
}

function Invoke-SevenZip {
    param([string[]]$Arguments)

    # Same two PowerShell 5.1 traps as the import gate's extractor: a native
    # command's stderr becomes terminating ErrorRecords under "Stop", and 7z
    # exits nonzero for the Win98 CD's absent neighbouring cab volumes even
    # when it extracted what was asked for. Every caller checks for its file.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $sevenZip @Arguments 2>&1 | Out-Null
    } finally {
        $ErrorActionPreference = $saved
    }
}

function Report-File {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path
    $version = $item.VersionInfo.FileVersion
    if ([string]::IsNullOrWhiteSpace($version)) { $version = "(no version resource)" }
    Write-Host ("  {0,-14} {1,9} B  {2,-18} sha256 {3}" -f `
        $item.Name, $item.Length, $version, (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash)
}

function Get-Win98CabForFile {
    # Resolves "which cab holds NAME" through the media's own layout.inf, the
    # way the import gate resolves ntkern.vxd. Hard-coding BASE5.CAB would
    # stage nothing (or the wrong thing) on a differently-laid-out CD.
    param([string]$WorkDir, [string]$Iso, [string]$Name)

    $layout = Join-Path $WorkDir "layout.inf"
    if (-not (Test-Path -LiteralPath $layout)) {
        Invoke-SevenZip @("e", "-y", "-o$WorkDir", $Iso, "win98/PRECOPY1.CAB", "win98/PRECOPY2.CAB")
        if (-not (Test-Path -LiteralPath (Join-Path $WorkDir "PRECOPY1.CAB"))) {
            throw "7z could not read win98\PRECOPY1.CAB out of '$Iso'"
        }
        Invoke-SevenZip @("e", "-y", "-o$WorkDir", (Join-Path $WorkDir "PRECOPY1.CAB"), "layout.inf")
        if (-not (Test-Path -LiteralPath $layout)) {
            throw "could not extract layout.inf from PRECOPY1.CAB"
        }
    }

    $lines = Get-Content -LiteralPath $layout
    $diskId = ""
    foreach ($line in $lines) {
        if ($line -match ("^\s*" + [regex]::Escape($Name) + "\s*=\s*(\d+)\s*,")) {
            $diskId = $Matches[1]
            break
        }
    }
    if ($diskId -eq "") { throw "layout.inf does not name a source disk for $Name" }

    foreach ($line in $lines) {
        if ($line -match ("^\s*" + $diskId + "\s*=\s*[^,]+,\s*""([^""]+)""")) {
            return @{ Disk = $diskId; Cab = $Matches[1] }
        }
    }
    throw "layout.inf disk $diskId (holding $Name) has no cab name"
}

try {
    Ensure-Directory $work

    # --- Windows 98 SE usbd.sys + usbhub.sys (the gate's precedent pair) -----
    Write-Step "Windows 98 SE usbd.sys and usbhub.sys"

    $w98Dir = Join-Path $repo "tools\win98se-extracted"
    Ensure-Directory $w98Dir
    $w98Wanted = @("usbd.sys", "usbhub.sys")
    $w98Missing = @($w98Wanted | Where-Object { $Force -or -not (Test-Path -LiteralPath (Join-Path $w98Dir $_)) })

    if ($w98Missing.Count -eq 0) {
        Write-Ok "already staged (pass -Force to redo)"
        foreach ($n in $w98Wanted) { Report-File (Join-Path $w98Dir $n) }
    } elseif ($Win98Iso -eq "") {
        Write-Warn "no -Win98Iso given - skipping the Windows 98 pair."
    } elseif (-not (Test-Path -LiteralPath $Win98Iso)) {
        Write-Warn "Win98 SE ISO not found at '$Win98Iso' - skipping the Windows 98 pair."
    } else {
        $loc = Get-Win98CabForFile -WorkDir $work -Iso $Win98Iso -Name "usbd.sys"
        Write-Host "  layout.inf: usbd.sys is on disk $($loc.Disk) = $($loc.Cab)"
        Invoke-SevenZip @("e", "-y", "-o$work", $Win98Iso, ("win98/" + $loc.Cab))
        $cab = Join-Path $work $loc.Cab
        if (-not (Test-Path -LiteralPath $cab)) {
            throw "7z could not read win98\$($loc.Cab) out of '$Win98Iso'"
        }
        $out98 = Join-Path $work "w98usb"
        Invoke-SevenZip (@("e", "-y", "-o$out98", $cab) + $w98Missing)
        foreach ($n in $w98Missing) {
            $staged = Join-Path $out98 $n
            if (-not (Test-Path -LiteralPath $staged)) {
                throw "$n was not extracted from $($loc.Cab)"
            }
            Copy-Item -LiteralPath $staged -Destination (Join-Path $w98Dir $n) -Force
        }
        Write-Ok "staged into $w98Dir"
        foreach ($n in $w98Wanted) { Report-File (Join-Path $w98Dir $n) }
    }

    # --- Windows 2000 SP4 USBD.SYS ------------------------------------------
    Write-Step "Windows 2000 SP4 USBD.SYS"

    $w2kDir = Join-Path $repo "tools\win2ksp4-extracted"
    Ensure-Directory $w2kDir
    $w2kOut = $w2kIdentity.Path

    if ((Test-Path -LiteralPath $w2kOut) -and -not $Force) {
        Write-Ok "already staged (pass -Force to redo)"
        Report-File $w2kOut
    } elseif ($Win2KIso -eq "") {
        Write-Warn "no -Win2KIso given - skipping the Windows 2000 file."
    } elseif (-not (Test-Path -LiteralPath $Win2KIso)) {
        Write-Warn "Windows 2000 ISO not found at '$Win2KIso' - skipping the Windows 2000 file."
    } else {
        Invoke-SevenZip @("e", "-y", "-o$work", $Win2KIso, "I386\USBD.SY_")
        $packed = Join-Path $work "USBD.SY_"
        if (-not (Test-Path -LiteralPath $packed)) {
            throw "'USBD.SY_' is not on '$Win2KIso' under I386\"
        }
        & $expand $packed $w2kOut | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "expand failed on '$packed' (exit $LASTEXITCODE)"
        }
        Write-Ok "staged into $w2kDir"
        Report-File $w2kOut
    }

    # --- Authenticate whatever is now staged --------------------------------
    Write-Step "Identity check"

    $evidenceList = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "import-gate") "win98-evidence.list"
    $w98Rows = @(Read-Win98EvidenceManifest -Path $evidenceList -RepoRoot $repo |
                 Where-Object { $_.Relative -match '(?i)^tools\\win98se-extracted\\(usbd|usbhub)\.sys$' })
    if ($w98Rows.Count -ne 2) {
        throw "$evidenceList does not name both tools\win98se-extracted\usbd.sys and usbhub.sys; nothing can authenticate the Windows 98 pair."
    }
    $rows = @($w98Rows) + @($w2kIdentity)

    $present = @($rows | Where-Object { Test-Path -LiteralPath $_.Path })
    if ($present.Count -eq 0) {
        throw @"
no reference file is staged and this run staged none. Name the install media:
  -Win98Iso <path to the Windows 98 SE CD image>
  -Win2KIso <path to the Windows 2000 SP4 CD image>
Each stages its own target; either alone stages that half only.
"@
    }
    $errors = @(Get-FileIdentityErrors -Rows $rows -SkipMissing)
    if ($errors.Count -gt 0) {
        throw @"
staged file(s) do not match their recorded identity:
  - $($errors -join "`n  - ")
Re-run this script with -Force and the recorded install media, or delete the
offending file and re-run with the media.
"@
    }
    foreach ($r in $present) {
        Write-Ok ("{0} is {1}" -f $r.Path, $r.Version)
    }
    if ($present.Count -lt $rows.Count) {
        Write-Warn "only $($present.Count) of $($rows.Count) reference files are staged."
    }

    Write-Step "Done"
    Write-Host "These are reference copies for the import gate and the Windows 2000 VM setup;"
    Write-Host "the install media is assembled by scripts\package\make-package.ps1 and carries"
    Write-Host "neither of them."
} catch {
    Write-Err $_.Exception.Message
    exit 1
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0
