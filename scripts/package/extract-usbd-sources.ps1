<#
.SYNOPSIS
Stage each target's own usbd.sys, the per-target file xhci98.inf carries.

.DESCRIPTION
usbhub20.sys imports USBD.SYS on both first-class targets and nothing on an
xHCI-only machine ever places it, so src\xhci98.inf carries it (roadmap Phase 3
task 7; docs\contributing\lessons.md, "usbhub20.sys bugchecks Win2000"). The two builds are different binaries with
the same name, and each target must get its own:

  tools\win98se-extracted\usbd.sys    Windows 98 SE 4.10.2222, from the SE CD.
      Also stages usbhub.sys from the same cab: it is the [precedent] pair the
      import gate's Win98 evidence list names, and both come out of one read.
      The cab is found through the media's own layout.inf rather than being
      hard-coded, so an OEM/retail layout difference fails loudly.

  tools\win2ksp4-extracted\USBD.SYS   Windows 2000 SP4 5.00.2195.6658, expanded
      from I386\USBD.SY_ on the retail SP4 ISO.

None of them is tracked, so tools\ is git-ignored and this script is how a fresh
clone or the project's other host gets them back. (They go into the GitHub
release download - a separate channel, recorded in
docs\contributing\legal-provenance.md section 5, and one nothing has actually
been uploaded to yet - which is why a clone still has to stage them and a
downloader would not.) It said "Neither file" until the post-Phase 13 review rounds, from before
task 13-E.1's remedy added usbhub98.sys; the manifest has held
three rows since. Every staged file is verified against
scripts\package\usbd-sources.expected before the script reports success -
staging the wrong build is the one error the per-target split cannot catch
later, because both files are named usbd.sys once installed.

Existing files are left alone unless -Force is given. Nothing here touches the
VMs, the driver build, or the package.

Each ISO parameter is required to stage *its own* target, and neither has a
default: where a person keeps installation media is a property of their machine,
and a committed default is a path that is right on exactly one host and wrong
everywhere else.  Omitting one skips that target with a warning, as an
unreadable path does, and the package then cannot be built for that half.
Omitting both, on a host where neither file is already staged, is an error:
the run would otherwise report success having staged nothing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\extract-usbd-sources.ps1 `
    -Win2KIso D:\isos\win2ksp4-retail.ISO -Win98Iso D:\isos\w98se-oem.iso
#>

[CmdletBinding()]
param(
    [string]$Win2KIso = "",
    [string]$Win98Iso = "",
    [string]$ManifestPath = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

$repo = Get-RepoRoot
if ($ManifestPath -eq "") { $ManifestPath = Get-UsbdSourceManifestPath }
$work = Join-Path $env:TEMP ("xhci98-usbd-" + [System.IO.Path]::GetRandomFileName())

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
    $rows = @(Read-UsbdSourceManifest -Path $ManifestPath -RepoRoot $repo)
    Ensure-Directory $work

    # --- Windows 98 SE usbd.sys (+ usbhub.sys, the gate's precedent pair) ----
    Write-Step "Windows 98 SE usbd.sys"

    $w98Dir = Join-Path $repo "tools\win98se-extracted"
    Ensure-Directory $w98Dir
    $w98Wanted = @("usbd.sys", "usbhub.sys")
    $w98Missing = @($w98Wanted | Where-Object { $Force -or -not (Test-Path -LiteralPath (Join-Path $w98Dir $_)) })

    if ($w98Missing.Count -eq 0) {
        Write-Ok "already staged (pass -Force to redo)"
        foreach ($n in $w98Wanted) { Report-File (Join-Path $w98Dir $n) }
    } elseif ($Win98Iso -eq "") {
        Write-Warn "no -Win98Iso given - skipping. The package cannot be built for the Win98 half without it."
    } elseif (-not (Test-Path -LiteralPath $Win98Iso)) {
        Write-Warn "Win98 SE ISO not found at '$Win98Iso' - skipping. The package cannot be built for the Win98 half without it."
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
    $w2kOut = Join-Path $w2kDir "USBD.SYS"

    if ((Test-Path -LiteralPath $w2kOut) -and -not $Force) {
        Write-Ok "already staged (pass -Force to redo)"
        Report-File $w2kOut
    } elseif ($Win2KIso -eq "") {
        Write-Warn "no -Win2KIso given - skipping. The package cannot be built for the Win2000 half without it."
    } elseif (-not (Test-Path -LiteralPath $Win2KIso)) {
        Write-Warn "Windows 2000 ISO not found at '$Win2KIso' - skipping. The package cannot be built for the Win2000 half without it."
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
    Write-Step "Identity check against $ManifestPath"

    $present = @($rows | Where-Object { Test-Path -LiteralPath $_.Path })
    if ($present.Count -eq 0) {
        # A run with no ISO arguments stages nothing, and a success exit would
        # read as "staged" to a caller following the documented command.
        throw @"
no source file is staged and this run staged none, so scripts\package\make-package.ps1
has nothing to package. Name the install media:
  -Win98Iso <path to the Windows 98 SE CD image>
  -Win2KIso <path to the Windows 2000 SP4 CD image>
Each stages its own target; either alone stages that half only.
"@
    } else {
        $errors = @(Get-UsbdSourceValidationErrors -Rows $rows -SkipMissing)
        if ($errors.Count -gt 0) {
            throw @"
staged source file(s) do not match '$ManifestPath':
  - $($errors -join "`n  - ")
Fix it one of these ways:
  - re-run this script with -Force and the recorded install media
  - delete the offending file and re-run with the media
Do not package an unverified file: staging the wrong OS's usbd.sys is invisible
after install, because both builds are called usbd.sys on the target.
"@
        }
        foreach ($r in $present) {
            Write-Ok ("{0} -> {1} ({2})" -f $r.Relative, $r.Media, $r.Target)
        }
        if ($present.Count -lt $rows.Count) {
            Write-Warn "only $($present.Count) of $($rows.Count) source files are staged; a full package needs both."
        }
    }

    Write-Step "Done"
    Write-Host "Next: scripts\package\make-package.ps1 to assemble the install media."
} catch {
    Write-Err $_.Exception.Message
    exit 1
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0
