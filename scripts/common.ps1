Set-StrictMode -Version 2.0

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-DefaultToolsDir {
    return (Join-Path (Get-RepoRoot) "tools")
}

function Get-DefaultVmDir {
    return (Join-Path (Get-RepoRoot) "vm")
}

function Get-DefaultLocalScriptDir {
    return (Join-Path $PSScriptRoot "local")
}

function Get-DefaultDdkPath {
    # The Win2K DDK is a repository directory, not a machine-wide install, so
    # every script that needs it derives the path from its own location and a
    # clone builds wherever it is unpacked. See scripts\install-w2kddk-cabs.ps1.
    return (Join-Path (Get-DefaultToolsDir) "ntddk")
}

function Get-DefaultMsvcRoot {
    # MSVC 6.0 runs in place from the archive's own top-level directory. The
    # .cmd scripts cannot call this, so they spell the same relative path out;
    # keep the two in step.
    return (Join-Path (Get-DefaultToolsDir) "MSVC600")
}

function Get-RelativePathFrom {
    # $To expressed relative to the directory $FromDir, or $null when no
    # relative path exists (different drive, or one side is UNC). Callers use
    # the $null to fall back to an absolute path rather than emitting a wrong
    # one: MakeRelativeUri answers an absolute path when the roots differ.
    param(
        [string]$FromDir,
        [string]$To
    )
    $fromUri = New-Object System.Uri (($FromDir.TrimEnd('\')) + '\')
    $toUri = New-Object System.Uri $To
    if ($fromUri.Scheme -ne $toUri.Scheme) { return $null }
    $rel = [System.Uri]::UnescapeDataString($fromUri.MakeRelativeUri($toUri).ToString())
    $rel = $rel.Replace('/', '\')
    if ($rel -match '^[A-Za-z]:' -or $rel.StartsWith('\\')) { return $null }
    return $rel
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "OK: $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "WARN: $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Find-Tool {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        return $null
    }
    return $cmd.Source
}

function Write-AsciiFile {
    param(
        [string]$Path,
        [string[]]$Lines
    )
    Set-Content -LiteralPath $Path -Value $Lines -Encoding ASCII
}

#
# QEMU, found the same way by every generator (the 2026-09-07 audit's H28 and
# J6). There were five copies of this, and they had drifted: three fell back to
# the bare `qemu-system-x86_64` and two to a hard-coded
# `C:\Program Files\qemu\...`, so which launchers survived being moved to
# another host depended on which script wrote them.
#
function Get-QemuTool {
    param(
        [string]$QemuBinDir,
        [string]$ToolName
    )
    if (-not [string]::IsNullOrWhiteSpace($QemuBinDir)) {
        $candidate = Join-Path $QemuBinDir $ToolName
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return (Find-Tool $ToolName)
}

#
# **What a generated launcher should write for QEMU, and why it is not simply
# the absolute path this host found.** A launcher lives in the git-ignored
# `scripts\local\`, so it is regenerated per host - but it is also read,
# copied and quoted into run sheets, and an absolute path baked into one is
# the standing move-to-another-host trap `build-and-test.md` names. The bare
# command name has the opposite problem: it resolves through PATH, and
# `setup-qemu.ps1 -Install` writes launchers in a process whose PATH predates
# the install it just performed, so the name is right and unresolvable.
#
# So a launcher gets a resolver rather than a value: an override, then the
# path this host found, then the two ordinary install locations, then the bare
# name for PATH. Written as .cmd lines because that is where it runs.
#
function Get-QemuLauncherResolver {
    param([string]$FoundPath)

    $lines = @(
        "rem Resolve QEMU at RUN time, not at generation time. In order:",
        "rem   1. %XHCI98_QEMU%, if you have set it;",
        "rem   2. where the script that generated this launcher found it;",
        "rem   3. the two places this project has found QEMU on its hosts;",
        "rem   4. a message naming the override, and a nonzero exit.",
        "if not defined QEMU set ""QEMU=%XHCI98_QEMU%"""
    )
    if (-not [string]::IsNullOrWhiteSpace($FoundPath)) {
        $lines += ("if not exist ""%QEMU%"" set ""QEMU={0}""" -f $FoundPath)
    }
    $lines += @(
        "if not exist ""%QEMU%"" set ""QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe""",
        "if not exist ""%QEMU%"" set ""QEMU=%USERPROFILE%\scoop\apps\qemu\current\qemu-system-x86_64.exe""",
        "if not exist ""%QEMU%"" (",
        "  echo Could not find qemu-system-x86_64.exe on this host - set XHCI98_QEMU or QEMU to its full path.",
        "  exit /b 1",
        ")",
        ""
    )
    return $lines
}

#
# **The flavour markers a linked image may carry, in one place** (the
# 2026-09-07 audit's J6). There were three scanners: this project's
# `check-flavour-marker.ps1`, `Get-ImageFlavourMarker` in
# `scripts\package\package-common.ps1`, and the list spelled out again in
# `build-driver.cmd`'s comments. They agreed, which is the only reason nothing
# had gone wrong; a fourth flavour would have had to be added to all three.
#
# HOSTTEST is in the list on purpose. It is the host suite's own marker and can
# never be in a linked driver, so finding one would mean the flavour defines
# had gone somewhere very strange - and a check that silently ignored a name it
# knows about is how a fourth flavour arrives unnoticed.
#
$script:XhciFlavourNames = @("RELEASE", "DEBUG", "QEMU", "HOSTTEST")

function Get-XhciFlavourNames {
    return $script:XhciFlavourNames
}

# Every marker present in the image, as full `XHCI98_FLAVOUR_*` strings.
# The caller decides what "exactly one, and this one" means for it: the build
# gate wants a specific flavour, the release script wants to know which it is.
function Get-XhciFlavourMarkers {
    param([string]$Path)

    $text = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($Path))
    return @($script:XhciFlavourNames |
        Where-Object { $text.Contains("XHCI98_FLAVOUR_" + $_) } |
        ForEach-Object { "XHCI98_FLAVOUR_" + $_ })
}

function Test-SetupHost {
    if ($env:OS -ne "Windows_NT") {
        throw "These setup scripts are intended for Windows hosts."
    }
    if (-not [Environment]::Is64BitOperatingSystem) {
        Write-Warn "Host OS is not 64-bit. The expected host is Windows 11 x64."
    } else {
        Write-Ok "64-bit Windows host detected"
    }
    if (-not [Environment]::Is64BitProcess) {
        Write-Warn "PowerShell is running as a 32-bit process. Prefer 64-bit PowerShell on Windows 11 x64."
    }
}

function Test-PathUnderRoot {
    param(
        [string]$Path,
        [string]$Root
    )
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        Ensure-Directory $parent
    }
    $resolvedParent = (Resolve-Path -LiteralPath $parent).Path
    return $resolvedParent.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

#
# Stage the Windows 2000 SP4 USBD.SYS the two Win2000 generators' preparation
# boots copy into the guest, and refuse anything that is not that exact file.
#
# There were two byte-identical copies of this - the identity check and the
# twenty-line staging block around it - in setup-qemu-win2k.ps1 and
# setup-qemu-win2k-smp.ps1 (the 2026-09-07 audit's J6, and the same shape as
# the Get-QemuTool and flavour-marker duplications above). The pinned length
# and version are the point of the function, so two copies of them is two
# places for the pin to drift, in a check whose whole job is that it cannot.
#
# **Identity, not presence.** The 2b and 2d guests' USB stacks are incomplete
# without this file, and a wrong one is worse than an absent one: the stack
# half-starts and the failure surfaces later, somewhere else, as the driver's.
#
# Returns the staged path, or "" when nothing was staged - which is a warning
# and not a failure, because the disk image may be built before the SP4 media
# is to hand.
#
function Install-Win2KUsbdSys {
    param(
        [Parameter(Mandatory = $true)][string]$XferDir,
        [string]$Win2KUsbdSys = ""
    )

    $stagedUsbd = Join-Path $XferDir "USBD.SYS"
    $defaultUsbd = Join-Path (Get-DefaultToolsDir) "win2ksp4-extracted\USBD.SYS"
    if ([string]::IsNullOrWhiteSpace($Win2KUsbdSys) -and
        (Test-Path -LiteralPath $defaultUsbd)) {
        $Win2KUsbdSys = $defaultUsbd
    }
    if (-not [string]::IsNullOrWhiteSpace($Win2KUsbdSys)) {
        if (-not (Test-Path -LiteralPath $Win2KUsbdSys)) {
            throw "Win2000 USBD.SYS not found at: $Win2KUsbdSys"
        }
        Assert-Win2KUsbdFile -Path $Win2KUsbdSys
        Copy-Item -LiteralPath $Win2KUsbdSys -Destination $stagedUsbd -Force
        Write-Ok "Staged Win2000 USBD.SYS for the preparation boot: $stagedUsbd"
        return $stagedUsbd
    }
    if (Test-Path -LiteralPath $stagedUsbd) {
        Assert-Win2KUsbdFile -Path $stagedUsbd
        Write-Ok "Using already-staged Win2000 USBD.SYS: $stagedUsbd"
        return $stagedUsbd
    }
    Write-Warn "Win2000 USBD.SYS is not staged. Extract I386\USBD.SY_ from the SP4 ISO, expand it, then rerun with -Win2KUsbdSys <path> before attaching EHCI."
    return ""
}

function Assert-Win2KUsbdFile {
    param([string]$Path)
    $file = Get-Item -LiteralPath $Path
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName).FileVersion
    if ($file.Length -ne 20688 -or $version -ne "5.00.2195.6658") {
        throw "Expected Win2000 SP4 USBD.SYS 5.00.2195.6658 (20688 bytes); found version '$version' ($($file.Length) bytes) at: $Path"
    }
}
