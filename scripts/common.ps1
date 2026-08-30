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
