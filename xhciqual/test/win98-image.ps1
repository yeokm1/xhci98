# win98-image.ps1 - locate (or fetch) the bare Win98SE / MS-DOS 7.1 boot floppy
# that the QEMU harnesses boot from.
#
# The image is proprietary Microsoft software and is NOT committed, and this
# repo hardcodes NO download source. If tools\w98se.img is missing, the image
# is fetched from a URL that the USER supplies, so each user chooses their own
# source. Resolution order (first hit wins):
#   1. -Url argument (the harnesses expose it as -BootImageUrl)
#   2. $env:XHCIQUAL_WIN98_URL
#   3. tools\w98se.url  (gitignored; first non-blank, non-# line)
# Optional integrity check: $env:XHCIQUAL_WIN98_SHA256, if set, must match.
#
# The URL must point to an automation-ready image: a BARE disk that boots
# straight to a DOS prompt (no menu, no DATE/TIME prompt). See xhciqual/README.md
# for how to build one from a stock Win98 boot floppy with mtools.

function Resolve-Win98Url {
    param([string]$Url, [string]$Repo)
    if ($Url) { return $Url.Trim() }
    if ($env:XHCIQUAL_WIN98_URL) { return $env:XHCIQUAL_WIN98_URL.Trim() }
    $urlFile = Join-Path $Repo "tools\w98se.url"
    if (Test-Path -LiteralPath $urlFile) {
        foreach ($line in Get-Content -LiteralPath $urlFile) {
            $t = $line.Trim()
            if ($t -ne "" -and -not $t.StartsWith("#")) { return $t }
        }
    }
    return ""
}

# Absolute filesystem path for a -BootImage value. Callers MUST resolve before
# use and pass the resolved path everywhere, because the path is also handed to
# QEMU. PowerShell path syntax is wider than what a native process accepts: a
# PSDrive qualifier (xqimg:\w98se.img) or a ~ prefix satisfies Test-Path here
# but is an unopenable filename to QEMU, so resolving only inside this helper
# would download to one path and then boot from another.
#
# Plain relative paths happen to survive unresolved - Start-Process launches
# with the current PowerShell location, not [Environment]::CurrentDirectory -
# but that is an implementation detail, not something to depend on.
function Resolve-Win98ImagePath {
    param([string]$BootImage)
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BootImage)
}

# Returns $true if $BootImage is present (or was fetched), $false only when no
# source is configured and the harness should skip. Acquisition errors throw.
function Ensure-Win98Image {
    param([string]$BootImage, [string]$Repo, [string]$Url = "")

    # Idempotent: callers resolve too, but this keeps the helper safe standalone.
    $BootImage = Resolve-Win98ImagePath $BootImage
    if (Test-Path -LiteralPath $BootImage) { return $true }

    $resolved = Resolve-Win98Url -Url $Url -Repo $Repo
    if ($resolved -eq "") {
        Write-Host "SKIP: Win98 boot image not found at $BootImage."
        Write-Host "      It is proprietary and not committed. Either drop a bare"
        Write-Host "      boot floppy there, or set a download URL (your choice of"
        Write-Host "      source):"
        Write-Host "        * pass -BootImageUrl <url>, or"
        Write-Host "        * set `$env:XHCIQUAL_WIN98_URL, or"
        Write-Host "        * put the URL on a line in tools\w98se.url"
        return $false
    }

    Write-Host "Win98 boot image missing; downloading from $resolved ..."
    $dir = Split-Path -Parent $BootImage
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $tmp = "$BootImage.part"
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    try {
        $prev = $ProgressPreference
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $resolved -OutFile $tmp -UseBasicParsing
    } catch {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        throw ("Win98 boot image download failed: " + $_.Exception.Message)
    } finally {
        $ProgressPreference = $prev
    }

    $len = (Get-Item -LiteralPath $tmp).Length
    # A floppy image is 720K / 1.44M / 2.88M; anything tiny is an HTML error page.
    if ($len -lt 300KB) {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        throw "Downloaded Win98 boot image is only $len bytes; expected a floppy image."
    }

    if ($env:XHCIQUAL_WIN98_SHA256) {
        $want = $env:XHCIQUAL_WIN98_SHA256.Trim().ToUpperInvariant()
        $got = (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($got -ne $want) {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
            throw "Win98 boot image SHA256 mismatch: expected $want, got $got."
        }
        Write-Host "SHA256 verified."
    }

    Move-Item -LiteralPath $tmp -Destination $BootImage -Force
    Write-Host ("Saved $BootImage ($len bytes).")
    return $true
}
