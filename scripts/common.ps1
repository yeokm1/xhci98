Set-StrictMode -Version 2.0

function Get-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
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
# QEMU, found the same way by every generator (the audit of
# 2026-09-07). There were five copies of this, and they had drifted: three fell back to
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
# **AND THEN IT SAYS WHICH RUNG ANSWERED AND WHAT THAT QEMU IS, because the
# fall-through is silent and that is the whole point of it.** A rung exists so
# a launcher survives being carried to another host; the same mechanism means
# a host that *loses* a QEMU does not get an error, it gets the next rung. It
# happened here on 2026-09-10: `C:\Program Files\qemu` was uninstalled between
# two sessions, and the launchers for two 64-bit guests installed under 11.0.92
# would have booted them under an older scoop 11.0.0 without a word. This
# project has attributed QEMU behaviour to version more than once, so those are
# not interchangeable.
#
# No gate can catch that - `test-qemu-launchers.ps1` reads generated text, and
# the host is exactly what it must not depend on - so the launcher is where it
# has to be said. Three lines at the top of every boot, which also means a run
# sheet quoting the head of a run has the emulator its reading was taken under
# rather than an assumption about it.
#
# The version is read by running the resolved binary, so it degrades rather
# than fails: anything that is not a working qemu-system-x86_64.exe leaves
# `QEMUVER` unset and the launcher says so instead of dying. That case is not
# hypothetical here either - the launcher gate generates against stand-in files
# and executes this preamble.
#
function Get-QemuLauncherResolver {
    param([string]$FoundPath)

    $lines = @(
        "rem Resolve QEMU at RUN time, not at generation time. In order:",
        "rem   1. %XHCI98_QEMU%, if you have set it;",
        "rem   2. where the script that generated this launcher found it;",
        "rem   3. the two places this project has found QEMU on its hosts;",
        "rem   4. a message naming the override, and a nonzero exit.",
        "rem Then SAY which rung answered and what it is: a QEMU that goes",
        "rem missing does not fail this launcher, the next rung catches the fall",
        "rem and the guest boots under a different emulator in silence.",
        "set ""QEMUFROM=""",
        "set ""QEMUVER=""",
        "if not defined QEMU set ""QEMU=%XHCI98_QEMU%""",
        "if exist ""%QEMU%"" set ""QEMUFROM=the XHCI98_QEMU or QEMU override"""
    )
    if (-not [string]::IsNullOrWhiteSpace($FoundPath)) {
        $lines += @(
            "if not exist ""%QEMU%"" (",
            ("  set ""QEMU={0}""" -f $FoundPath),
            "  set ""QEMUFROM=where this launcher was generated""",
            ")"
        )
    }
    $lines += @(
        "if not exist ""%QEMU%"" (",
        "  set ""QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe""",
        "  set ""QEMUFROM=the ordinary Program Files install""",
        ")",
        "if not exist ""%QEMU%"" (",
        "  set ""QEMU=%USERPROFILE%\scoop\apps\qemu\current\qemu-system-x86_64.exe""",
        "  set ""QEMUFROM=the scoop package""",
        ")",
        "if not exist ""%QEMU%"" (",
        "  echo Could not find qemu-system-x86_64.exe on this host - set XHCI98_QEMU or QEMU to its full path.",
        "  exit /b 1",
        ")",
        'for /f "usebackq delims=" %%V in (`"%QEMU%" --version 2^>nul`) do if not defined QEMUVER set "QEMUVER=%%V"',
        "if not defined QEMUVER set ""QEMUVER=version unreadable - this is not a working qemu-system-x86_64.exe""",
        "echo QEMU: %QEMUVER%",
        "echo QEMU: %QEMU%",
        "echo QEMU: resolved from %QEMUFROM%.",
        ""
    )
    return $lines
}

#
# **The flavour markers a linked image may carry, in one place** (the
# audit of 2026-09-07). There were three scanners: this project's
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
# setup-qemu-win2k-smp.ps1 (the audit of 2026-09-07, and the same shape as
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

#
# The self-tests' shared pass records (roadmap-usb4 task 45.51;
# build-and-test.md, "The self-tests' skip across worktrees"). One store per
# user and host, outside every worktree: a folder per suite and one file per
# passing digest, so a pass in one tree is found by every tree whose inputs
# hash the same. Nothing here deletes a record: one whose digest no tree
# has any more is never asked for.
#
function Get-SelfTestPassDir {
    # XHCI98_SELFTEST_PASS_DIR when set, else %LOCALAPPDATA%\xhci98\selftest-pass;
    # "" when neither can be named (no store: every run runs whole).
    if ("$env:XHCI98_SELFTEST_PASS_DIR" -ne "") { return [System.IO.Path]::GetFullPath("$env:XHCI98_SELFTEST_PASS_DIR") }
    if ("$env:LOCALAPPDATA" -eq "") { return "" }
    return (Join-Path $env:LOCALAPPDATA "xhci98\selftest-pass")
}

function Test-SelfTestPassRecordLines {
    # A record counts only whole: exactly six lines in order - the digest and
    # the suite asked, its time, a count of one check or more, the tree that
    # passed (for the reader only, never compared), then the end line.
    param([string[]]$Lines, [string]$Suite, [string]$Digest)
    return ($Lines.Count -eq 6 -and $Lines[0] -ceq ("digest=" + $Digest) -and $Lines[1] -ceq ("suite=" + $Suite) -and
            $Lines[2] -cmatch '^time=\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$' -and $Lines[3] -cmatch '^checks=[1-9]\d*$' -and
            $Lines[4] -cmatch '^tree=.+$' -and $Lines[5] -ceq "end")
}

function Read-SelfTestPassRecord {
    # The record of a pass of -Suite on -Digest, as @{ Time; Checks; Tree;
    # Path }, or $null when there is none or it is not whole.
    param([string]$Suite, [string]$Digest, [string]$Dir = "")
    if ($Suite -cnotmatch '^[a-z]+$' -or $Digest -cnotmatch '^[0-9a-f]{64}$') { return $null }
    # A store that cannot be named or reached is no record: the suite runs.
    try {
        if ($Dir -eq "") { $Dir = Get-SelfTestPassDir }
        if ($Dir -eq "") { return $null }
        $file = Join-Path (Join-Path $Dir $Suite) ($Digest + ".txt")
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $null }
        $lines = @([System.IO.File]::ReadAllLines($file))
    } catch {
        return $null
    }
    if (-not (Test-SelfTestPassRecordLines -Lines $lines -Suite $Suite -Digest $Digest)) { return $null }
    return @{ Time = $lines[2].Substring(5); Checks = $lines[3].Substring(7); Tree = $lines[4].Substring(5); Path = $file }
}

function Write-SelfTestPassRecord {
    # Records a pass of -Suite on -Digest: the suite's folder made if it is
    # not there, the record written whole to a temporary name beside its
    # place and moved into it, so it is whole or absent. When the name is
    # taken, a whole record for this digest is left as it is (another tree
    # passed the same inputs) and anything else is replaced from the fresh
    # temporary. Any other failure is a warning and records nothing: the
    # pass stands and the next run runs whole. $true when a whole record is
    # in place afterwards.
    param([string]$Suite, [string]$Digest, [int]$Checks, [string]$Tree, [string]$Dir = "")
    if ($Suite -cnotmatch '^[a-z]+$' -or $Digest -cnotmatch '^[0-9a-f]{64}$' -or $Checks -lt 1) { return $false }
    $tmp = ""
    try {
        if ($Dir -eq "") { $Dir = Get-SelfTestPassDir }
        if ($Dir -eq "") {
            Write-Warn ("the {0} self-tests' pass was not recorded: neither XHCI98_SELFTEST_PASS_DIR nor LOCALAPPDATA is set." -f $Suite)
            return $false
        }
        $folder = Join-Path $Dir $Suite
        [void][System.IO.Directory]::CreateDirectory($folder)
        $dest = Join-Path $folder ($Digest + ".txt")
        $tmp = Join-Path $folder ($Digest + "." + [System.IO.Path]::GetRandomFileName() + ".tmp")
        $text = ("digest={0}`r`nsuite={1}`r`ntime={2}`r`nchecks={3}`r`ntree={4}`r`nend`r`n" -f $Digest, $Suite,
                 [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"), $Checks, $Tree)
        [System.IO.File]::WriteAllText($tmp, $text, (New-Object System.Text.UTF8Encoding($false)))
        try {
            [System.IO.File]::Move($tmp, $dest)
        } catch {
            # Taken by another run's record, or failed for another reason.
            if (-not [System.IO.File]::Exists($dest)) { throw }
            if ($null -ne (Read-SelfTestPassRecord -Suite $Suite -Digest $Digest -Dir $Dir)) { return $true }
            [System.IO.File]::Replace($tmp, $dest, [NullString]::Value)
        }
        return ($null -ne (Read-SelfTestPassRecord -Suite $Suite -Digest $Digest -Dir $Dir))
    } catch {
        Write-Warn ("the {0} self-tests' pass was not recorded ({1}); the next run runs them whole." -f $Suite, $_.Exception.Message)
        return $false
    } finally {
        if ($tmp -ne "" -and [System.IO.File]::Exists($tmp)) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
}

#
# The self-tests' git (roadmap-usb4 task 45.51): one policy, set in the
# suite's own environment at its start, so the fixtures it builds and every
# production git call it reaches - in this process and in every child it
# starts - see the same git on every host, whatever the caller's system and
# global configuration, hooks, templates, replacement refs or injected
# variables. The digest's inherited rows are taken before this is set, and
# leave out the names this sets or clears (Get-SelfTestGitPolicyNames).
#
function Get-SelfTestGitPolicyNames {
    # Every variable the policy sets or clears: a caller's value of one of
    # them is overridden here, so it cannot change an outcome and is no
    # input. GIT_CONFIG_KEY_<n> and GIT_CONFIG_VALUE_<n> are cleared too.
    return @("GIT_CONFIG_NOSYSTEM", "GIT_CONFIG_GLOBAL", "GIT_ATTR_NOSYSTEM", "GIT_TEMPLATE_DIR", "GIT_NO_REPLACE_OBJECTS",
             "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_NAMESPACE", "GIT_CONFIG", "GIT_CONFIG_COUNT",
             "GIT_CONFIG_PARAMETERS", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_REPLACE_REF_BASE",
             "GIT_GRAFT_FILE", "GIT_SHALLOW_FILE", "GIT_ATTR_SOURCE", "GIT_LITERAL_PATHSPECS", "GIT_GLOB_PATHSPECS",
             "GIT_NOGLOB_PATHSPECS", "GIT_ICASE_PATHSPECS", "GIT_EXTERNAL_DIFF", "GIT_DIFF_OPTS", "GIT_EXEC_PATH",
             "GIT_SSH", "GIT_SSH_COMMAND", "GIT_ASKPASS", "GIT_EDITOR", "GIT_PAGER", "GIT_SEQUENCE_EDITOR",
             "GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_AUTHOR_DATE", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL",
             "GIT_COMMITTER_DATE", "GIT_TERMINAL_PROMPT", "GIT_OPTIONAL_LOCKS", "GIT_TRACE", "GIT_FLUSH",
             "GIT_DEFAULT_HASH", "GIT_DEFAULT_REF_FORMAT")
}

function Test-SelfTestGitPolicyName {
    param([string]$Name)
    $n = $Name.ToUpperInvariant()
    return ((Get-SelfTestGitPolicyNames) -contains $n -or $n -match '^GIT_CONFIG_(KEY|VALUE)_\d+$')
}

function Set-SelfTestGitPolicy {
    # Sets the policy in this process's environment and returns it as
    # @{ Root; Saved }: -ScratchRoot, which holds its files, and every
    # variable it changes (and those -Keep names) as they were, for
    # Restore-SelfTestGitPolicy to put back, since a suite run in a caller's
    # session shares that session's environment. The policy:
    # no system configuration (GIT_CONFIG_NOSYSTEM, GIT_ATTR_NOSYSTEM); a
    # global configuration of the suite's own (GIT_CONFIG_GLOBAL) holding
    # only empty core.excludesFile and core.attributesFile, an empty
    # core.hooksPath and init.templateDir, and signing off; an empty
    # GIT_TEMPLATE_DIR; replacement objects off (GIT_NO_REPLACE_OBJECTS);
    # no prompt; and every injection variable cleared (GIT_DIR, GIT_WORK_TREE,
    # GIT_INDEX_FILE, GIT_CONFIG_COUNT and its pairs, GIT_CONFIG_PARAMETERS,
    # the object directories, GIT_DEFAULT_HASH and the rest of
    # Get-SelfTestGitPolicyNames), so a fixture is SHA-1 with files refs.
    # A repository's own configuration still applies: it is the tree's.
    param([string]$ScratchRoot, [string[]]$Keep = @())
    $saved = @{}
    foreach ($n in @(@(Get-SelfTestGitPolicyNames) + @($Keep))) { $saved[$n] = [Environment]::GetEnvironmentVariable($n) }
    foreach ($v in @(Get-ChildItem Env:)) {
        if (Test-SelfTestGitPolicyName -Name $v.Name) { $saved[$v.Name] = $v.Value }
    }
    # Set whole or not at all: an error or Ctrl+C before the caller holds the
    # policy puts back what was changed.
    $policy = @{ Root = $ScratchRoot; Saved = $saved; Restored = $false }
    $held = $false
    try {
        $dir = Join-Path $ScratchRoot "git-policy"
        foreach ($d in @($dir, (Join-Path $dir "hooks"), (Join-Path $dir "template"))) { [void][System.IO.Directory]::CreateDirectory($d) }
        $ignore = Join-Path $dir "ignore"
        $attributes = Join-Path $dir "attributes"
        foreach ($f in @($ignore, $attributes)) { [System.IO.File]::WriteAllText($f, "") }
        $fwd = { param($p) $p.Replace('\', '/') }
        $config = Join-Path $dir "config"
        [System.IO.File]::WriteAllText($config, (("[core]`n`texcludesFile = ""{0}""`n`tattributesFile = ""{1}""`n`thooksPath = ""{2}""`n" +
            "[init]`n`ttemplateDir = ""{3}""`n[commit]`n`tgpgSign = false`n[tag]`n`tgpgSign = false`n") -f
            (& $fwd $ignore), (& $fwd $attributes), (& $fwd (Join-Path $dir "hooks")), (& $fwd (Join-Path $dir "template"))))
        foreach ($v in @(Get-ChildItem Env:)) {
            if (Test-SelfTestGitPolicyName -Name $v.Name) { [Environment]::SetEnvironmentVariable($v.Name, $null) }
        }
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_NOSYSTEM", "1")
        [Environment]::SetEnvironmentVariable("GIT_ATTR_NOSYSTEM", "1")
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_GLOBAL", $config)
        [Environment]::SetEnvironmentVariable("GIT_TEMPLATE_DIR", (Join-Path $dir "template"))
        [Environment]::SetEnvironmentVariable("GIT_NO_REPLACE_OBJECTS", "1")
        [Environment]::SetEnvironmentVariable("GIT_TERMINAL_PROMPT", "0")
        $held = $true
        return $policy
    } finally {
        if (-not $held) { Restore-SelfTestGitPolicy -Policy $policy }
    }
}

function Restore-SelfTestGitPolicy {
    # Puts back every variable Set-SelfTestGitPolicy changed, as it was, and
    # removes its folder; once, whichever way the suite ends.
    param($Policy)
    if ($null -eq $Policy -or $Policy.Restored) { return }
    $Policy.Restored = $true
    foreach ($v in @(Get-ChildItem Env:)) {
        if ((Test-SelfTestGitPolicyName -Name $v.Name) -and -not $Policy.Saved.ContainsKey($v.Name)) { [Environment]::SetEnvironmentVariable($v.Name, $null) }
    }
    foreach ($n in $Policy.Saved.Keys) { [Environment]::SetEnvironmentVariable($n, $Policy.Saved[$n]) }
    Remove-Item -LiteralPath $Policy.Root -Recurse -Force -ErrorAction SilentlyContinue
}

function Get-SelfTestUnlistedPaths {
    # The guard on a self-test's set (task 45.51; build-and-test.md, "The
    # self-tests' skip across worktrees", "Each set guarded"): every
    # repository-relative path literal in -Scripts (paths under -Root) that
    # names a file git tracks there and is neither in -Set nor a key of
    # -NotInput (a path the scripts name that no case reads as an input,
    # each with its reason), as "path (script:line)". It catches a new read
    # by name, not every read: a path built from parts, a folder scanned, a
    # runtime read by a child and git's own reads are the set's entries by
    # hand. One entry naming git's failure when git cannot list -Root.
    param([string]$Root, [string[]]$Scripts, [string[]]$Set, [hashtable]$NotInput = @{})
    # Local to this function: git's warnings on stderr are not errors here.
    $ErrorActionPreference = "Continue"
    $tracked = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $listed = & git -C $Root -c core.quotePath=false ls-files 2>$null
    if ($LASTEXITCODE -ne 0) { return @("git could not list '$Root'") }
    foreach ($n in @($listed)) { if ("$n" -ne "") { [void]$tracked.Add(([string]$n).Replace('/', '\')) } }
    $in = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($n in @($Set)) { [void]$in.Add([string]$n) }
    $rx = '["'']((?:src|docs|releases|scripts|xhciinst|xhcimark|xhciqual|xhcitr|xhciperf|test|tools)\\[^"''$*?\s]+|LICENSE|README\.md)["'']'
    $out = @()
    foreach ($sc in $Scripts) {
        $lines = [System.IO.File]::ReadAllLines((Join-Path $Root $sc))
        for ($i = 0; $i -lt $lines.Count; $i++) {
            foreach ($m in [regex]::Matches($lines[$i], $rx)) {
                $lit = $m.Groups[1].Value.TrimEnd('\')
                if ($tracked.Contains($lit) -and -not $in.Contains($lit) -and -not $NotInput.ContainsKey($lit)) {
                    $out += ("{0} ({1}:{2})" -f $lit, $sc, ($i + 1))
                }
            }
        }
    }
    return $out
}
