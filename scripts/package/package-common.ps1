Set-StrictMode -Version 2.0

# Shared reader for scripts\package\usbd-sources.expected - the manifest that
# says which OS build belongs under which media name in the install package.
#
# Three tools read it and none of them may disagree: extract-usbd-sources.ps1
# (what to stage), make-package.ps1 (what to copy under which name), and
# check-inf.ps1 -PackageDir (what a staged package must contain). The file
# identity check itself is the import gate's - see the dot-source below.

. (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "import-gate") "evidence-common.ps1")

$script:PackageTargets = @("win98", "win2000")

function Get-UsbdSourceManifestPath {
    return (Join-Path $PSScriptRoot "usbd-sources.expected")
}

function Read-UsbdSourceManifest {
    param(
        [string]$Path,
        [string]$RepoRoot
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "per-target source manifest not found: $Path"
    }

    $rows = @()
    $mediaSeen = @{}
    $targetSeen = @{}
    $hashSeen = @{}
    $lineNo = 0

    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) {
            continue
        }

        $fields = $line -split "\s+", 7
        if ($fields.Count -lt 6) {
            throw "$Path line ${lineNo}: expected 'MEDIA-NAME TARGET SOURCE VERSION LENGTH SHA256 [note]'"
        }

        $media = $fields[0]
        if ([System.IO.Path]::GetFileName($media) -cne $media) {
            throw "$Path line ${lineNo}: MEDIA-NAME must be a bare file name"
        }
        # The media name lands in [SourceDisksFiles] and is read by Win98's
        # parser, which reports a non-8.3 name as a missing file.
        if ($media -notmatch '^[^.\\/:*?"<>|]{1,8}(\.[^.\\/:*?"<>|]{1,3})?$') {
            throw "$Path line ${lineNo}: MEDIA-NAME '$media' is not an 8.3 name"
        }

        $target = $fields[1].ToLower()
        if ($target -notin $script:PackageTargets) {
            throw "$Path line ${lineNo}: TARGET must be one of $($script:PackageTargets -join ', ')"
        }

        $source = $fields[2]
        if ([System.IO.Path]::IsPathRooted($source) -or $source -match "\.\.") {
            throw "$Path line ${lineNo}: SOURCE must be relative to the repository root and must not escape it"
        }

        $length = Assert-ManifestIdentityFields -Path $Path -LineNo $lineNo `
            -Version $fields[3] -Length $fields[4] -Sha256 $fields[5]

        $mediaKey = $media.ToLower()
        if ($mediaSeen.ContainsKey($mediaKey)) {
            throw "$Path line ${lineNo}: duplicate MEDIA-NAME '$media'"
        }
        $mediaSeen[$mediaKey] = $true

        # A target may name MORE THAN ONE source file, and did not until
        # by batch 13-E. This manifest was written when usbd.sys was the only
        # per-target file, so "one row per target" and "one row per destination"
        # were the same sentence; batch 13-E added Windows 98's composite parent
        # usbhub.sys and they stopped being. What the rule protected - two rows
        # claiming the same destination on the same target, which would make the
        # staged source ambiguous - is caught downstream and better: media names
        # are unique here (above), and scripts\inf-gate\check-inf.ps1's TGT-DUP
        # and W98-DUP fail an install path that names a file more than once.
        # The at-least-one rule below is the half worth keeping and is untouched.
        $targetSeen[$target] = $true

        # Two media names carrying one binary is a manifest error rather than a
        # staging surprise. **Across targets** it makes the whole split
        # pointless and silently reintroduces the single-file failure the split
        # exists to prevent; **within one target** - possible since batch 13-E,
        # when a target stopped being limited to one row - it is a row that was
        # copy-pasted and half-edited, which stages the same file under two
        # names and delivers the wrong one to a destination that will load it.
        # The message names both readings, because it used to name only the
        # first and the second is now the likelier mistake.
        $hashKey = $fields[5].ToUpper()
        if ($hashSeen.ContainsKey($hashKey)) {
            throw "$Path line ${lineNo}: '$media' has the same SHA256 as '$($hashSeen[$hashKey])'. Two media names may not carry one build - across targets that defeats the per-target split, and within one target it is a half-edited copy of the other row."
        }
        $hashSeen[$hashKey] = $media

        $note = ""
        if ($fields.Count -eq 7) {
            $note = $fields[6]
        }

        $rows += [pscustomobject]@{
            Media    = $media
            Target   = $target
            Relative = $source
            Path     = (Join-Path $RepoRoot $source)
            Label    = "$media (from $source)"
            Version  = $fields[3]
            Length   = $length
            Sha256   = $hashKey
            Note     = $note
        }
    }

    foreach ($t in $script:PackageTargets) {
        if (-not $targetSeen.ContainsKey($t)) {
            throw "$Path names no source file for target '$t'; both first-class targets need one"
        }
    }

    return $rows
}

function Read-MediaLayout {
    # Reads what check-inf.ps1 -EmitMediaLayout wrote: the place each
    # [SourceDisksFiles] entry has to occupy on the media, as that script's own
    # parse of the INF computed it. Keys are lower-cased source names.
    #
    # The packager reads this rather than parsing [SourceDisksFiles] itself.
    # Two parsers would be free to disagree, and the only way they can disagree
    # is a file staged at one path and authenticated at another - which is the
    # bug the per-file subdirectory handling exists to close.
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "media layout not written: $Path"
    }

    $layout = @{}
    $lineNo = 0
    foreach ($raw in Get-Content -LiteralPath $Path) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { continue }
        if ($line -notmatch '^([^=]+)=(.+)$') {
            throw "$Path line ${lineNo}: expected 'sourcename=relative\path'"
        }
        $name = $Matches[1].Trim()
        $relative = $Matches[2].Trim()
        if ([System.IO.Path]::IsPathRooted($relative) -or $relative -match "\.\.") {
            throw "$Path line ${lineNo}: '$relative' must be relative to the package root and must not escape it"
        }
        $layout[$name.ToLowerInvariant()] = $relative
    }

    if ($layout.Count -eq 0) {
        throw "$Path names no media files; the INF's [SourceDisksFiles] is empty or was not parsed"
    }
    return $layout
}

function Get-UsbdSourceValidationErrors {
    # Checks the staged *source* files (under tools\), before packaging.
    param(
        [object[]]$Rows,
        [switch]$SkipMissing
    )
    return Get-FileIdentityErrors -Rows $Rows -SkipMissing:$SkipMissing
}

function Get-PackagedFileValidationErrors {
    # Checks a staged *package* directory: each media name must be present and
    # be the build its manifest row names. This is the check that catches a
    # swap - the one failure the split cannot detect on the target, because
    # both files are called usbd.sys once installed.
    param(
        [object[]]$Rows,
        [string]$PackageDir,
        [hashtable]$MediaPaths
    )
    return Get-FileIdentityErrors -Rows @(
        $Rows | ForEach-Object {
            $path = Join-Path $PackageDir $_.Media
            $mediaKey = $_.Media.ToLowerInvariant()
            if ($null -ne $MediaPaths -and $MediaPaths.ContainsKey($mediaKey)) {
                $path = $MediaPaths[$mediaKey]
            }
            [pscustomobject]@{
                Path    = $path
                Label   = "$($_.Media) (the $($_.Target) build)"
                Version = $_.Version
                Length  = $_.Length
                Sha256  = $_.Sha256
            }
        }
    )
}

# ------------------------------------------------------------------
# Task 8-A.4: does a binary's reported FileVersion equal the INF's DriverVer?
# ------------------------------------------------------------------
#
# Its own function so that scripts\package\test-package.ps1 can drive it on
# strings. The alternative - testing it only through make-package.ps1 - needs a
# stand-in binary carrying a *chosen* version resource, and there is none in the
# tree to use: the reference binaries are gitignored and the driver's own version
# is whatever the last build produced. So the comparison is the unit, and the one
# vector that matters is a string this used to accept and must not.
#
# **The whole reported string has to be four numbers and nothing else.** An
# earlier version split on separators and compared the first four tokens, which
# silently accepted "1.0.0.1 stale" as 1.0.0.1 - and that trailing text is
# exactly what a user reads off the Version tab and quotes in a bug report. A
# resource may legitimately spell the version with dots or commas, so both are
# accepted as separators; anything that is neither a separator nor a digit is
# not. A `Declared` value shorter than four parts is zero-extended, since an INF
# `DriverVer` of "1.0" means 1.0.0.0.
#
# Which of the three build flavours an image is, read out of the file (task
# 13-L.1). Returns "release", "debug", "qemu" or "" when the image carries no
# marker at all - which means a binary built before the marker existed.
#
# **This is shared because both packaging scripts need the same answer and must
# not derive it two ways.** The path is not evidence: -DriverPath can name a
# binary anywhere, and "objchk_qemu" contains "objchk", so a path match is wrong
# in exactly the direction that would publish the one flavour that must never
# ship. VS_FF_DEBUG is not evidence either - `debug` and `qemu` are both checked
# DDK builds and both set it.
#
# **Exactly one marker, or none.** Returning the first of several would accept
# an ambiguous image as whichever name happened to be tested first, and both
# callers present this as an artifact-level identity gate on a `-DriverPath`
# that may name a binary from anywhere - which is precisely why they read the
# artifact instead of trusting the build wrapper. An image carrying two markers
# is one the preprocessor let through with more than one flavour define, and it
# is not a debug build with a stray string in it. `scripts\check-flavour-marker.ps1`
# has always demanded exactly one; this did not until a Codex review of commit
# `efa86a3` pointed out that the two gates disagreed.
#
# HOSTTEST is in the list for the same reason it is in that script's: it can
# never be in a linked driver, so finding one means the flavour defines have
# gone somewhere very strange, and a check that silently ignored a name it
# knows about is how a fourth flavour arrives unnoticed.
#
function Get-ImageFlavourMarker {
    param([string]$Path)

    $text = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($Path))
    $found = @(@("QEMU", "DEBUG", "RELEASE", "HOSTTEST") |
        Where-Object { $text.Contains("XHCI98_FLAVOUR_" + $_) })
    if ($found.Count -eq 1) {
        return $found[0].ToLower()
    }
    if ($found.Count -gt 1) {
        return "ambiguous (" + (($found | ForEach-Object { $_.ToLower() }) -join ", ") + ")"
    }
    return ""
}

function Test-DriverVersionMatches {
    param(
        [string]$Reported,      # the binary's VersionInfo.FileVersion
        [string]$Declared       # the version half of the INF's DriverVer
    )

    if ($null -eq $Reported -or $null -eq $Declared) { return $false }
    $r = $Reported.Trim()
    $d = $Declared.Trim()
    if ($r -notmatch '^\d+\s*[.,]\s*\d+\s*[.,]\s*\d+\s*[.,]\s*\d+$') { return $false }
    if ($d -notmatch '^\d+(\.\d+){0,3}$') { return $false }

    $a = @($r -split '[.,\s]+' | Where-Object { $_ -ne "" })
    $b = @($d -split '\.')
    while ($b.Count -lt 4) { $b += "0" }
    for ($i = 0; $i -lt 4; $i++) {
        if ([int]$a[$i] -ne [int]$b[$i]) { return $false }
    }
    return $true
}
