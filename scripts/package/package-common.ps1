Set-StrictMode -Version 2.0

# Shared pieces of the two packaging scripts: the media-layout reader that
# keeps make-package.ps1 and make-release.ps1 on check-inf.ps1's own parse of
# [SourceDisksFiles], the binary-versus-INF version comparison, and the
# flavour-marker reader. Until release 1.0.0.1 this file also read
# usbd-sources.expected, the manifest of the Microsoft files the media then
# carried; the media carries none now (the OS supplies usbd.sys, usbhub.sys
# and, on the NT targets, usbport.sys through the INF's LayoutFile), so
# there is nothing to
# authenticate and the reader is gone with the manifest.

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
