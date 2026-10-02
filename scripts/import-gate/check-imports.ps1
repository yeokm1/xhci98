<#
.SYNOPSIS
Post-link import-compatibility gate for xhci98.sys (roadmap Phase 3 task 5),
since 2026-10-02 the successor HCD that took the name (design record 13).

.DESCRIPTION
An unresolved module/symbol import stops a WDM driver before DriverEntry on
both targets, with no call-site diagnostic - on Win98 the only symptom can be a
Device Manager yellow bang, indistinguishable from a bad INF
(docs\usb-xhci-info\win98-wdm.md "Imports are a silent load-time gate"). This script makes
that a build-time failure instead.

Three things happen, in order:

  1. Enforcement, always. Every module/symbol pair in the linked binary must
     appear in scripts\import-gate\xhci98-imports.allow for the build flavor
     being checked; no USBPORT.SYS pair is allowed at all, because the HCD
     replaces usbport.sys. Pairs
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

Checks whichever of the six default images exist - the three flavours under
src\objfre, src\objchk and src\objchk_qemu, in i386 and in amd64 - inferring
the flavor AND the architecture from the path.

.EXAMPLE
powershell -File scripts\import-gate\check-imports.ps1 -Image out\pkg-debug-x86\xhci98.sys -Flavor debug -Arch x86

An image outside the obj trees infers nothing from its path, so -Flavor and
-Arch are both required: `-Arch auto` throws rather than guess, because MSVC
6.0's dumpbin reads an amd64 image, exits 0 and prints no import section at
all - a wrong guess would gate the allowlist against an empty set and pass.
#>

[CmdletBinding()]
param(
    [string[]]$Image = @(),

    [ValidateSet("auto", "release", "debug", "qemu")]
    [string]$Flavor = "auto",

    # The five path overrides below all default to a file beside this script or
    # under the repository, resolved after `param(...)` rather than in it,
    # because `$PSScriptRoot` is not available in a parameter default. They
    # exist for a host that stages the extracted target material outside the
    # tree, and for the gate's own tests, which drive it against fixtures.
    #
    # **THESE ARE THE GATE'S POLICY, NOT MERELY WHERE IT LOOKS.** The allowlist
    # IS the rule about which imports are permitted, and the two manifests ARE
    # the recorded identities an extracted file is held to - so a caller who
    # points any of them at a file of their own has changed what the gate
    # enforces, not just where it read it from. That is what they are for: the
    # tests pass deliberately malformed and deliberately relaxed fixtures. It
    # also means a build that passes with one of these overridden has not
    # passed the committed gate, and `scripts\build-driver.cmd` accordingly
    # passes none of them.

    # The allowlist. Default: xhci98-imports.allow beside this script.
    # Pinning it applies that one file to every image in the run, whatever its
    # architecture - the gate's own tests rely on that.
    [string]$AllowPath = "",

    # The amd64 allowlist, a sibling file rather than a column in the one above
    # (the owner's decision, 2026-09-09). Its header says why: only two of the
    # thirteen kernel/HAL rows are shared between the architectures, and the
    # x86 file's 27 denials are justified entirely by absence on Windows 98 or
    # by blocking the load on Windows 2000 - reasoning that does not transfer
    # to NT 5.2 amd64, where most of those APIs exist.
    # Default: xhci98-imports-amd64.allow beside this script.
    [string]$AllowPathAmd64 = "",

    # Where the extracted NT 5.2 amd64 ntoskrnl.exe/ntkrnlmp.exe/hal.dll are
    # staged. Default: tools\winxp64-extracted, beside the usbport.sys and
    # usbehci.sys the phase-21 ABI readings came from.
    [string]$Amd64Dir = "",

    # The manifest naming those three files and their recorded hashes.
    # Default: winxp64-baselines.expected beside this script.
    [string]$Amd64ManifestPath = "",


    # Where the extracted Windows 2000 SP4 ntoskrnl.exe/hal.dll are staged, for
    # step 2's export check. Default: tools\win2ksp4-extracted. An absent
    # directory is the "no baseline present" warning, not a failure.
    [string]$Win2kDir = "",

    # The manifest naming those files and their recorded hashes - what makes an
    # extracted baseline authenticated rather than merely present.
    # Default: win2k-baselines.expected beside this script.
    [string]$Win2kManifestPath = "",

    # The Windows 98 / NUSB precedent binaries and their recorded identities,
    # for step 3's positive-evidence scan. Default: win98-evidence.list beside
    # this script.
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
    [switch]$ParseOnly,

    # The architecture of the image being gated. "auto" reads it from the
    # image's parent directory - i386 or amd64 - the same way the flavor is
    # read from its grandparent.
    #
    # **This selects the dumper, and that is not a convenience.** MSVC 6.0's
    # dumpbin predates x64 by years: pointed at an amd64 PE it exits 0, prints
    # the section summary, and prints NO IMPORT SECTION AT ALL. A gate that
    # believed it would see an empty import table, find nothing disallowed in
    # it, and pass the binary for the wrong reason. On amd64 the dumper is
    # therefore WDK 7.1's link.exe /dump, and Test-DumperMachine below refuses
    # to proceed unless the dumper reports the machine this run expects - so
    # the failure mode above cannot recur silently if either path is ever
    # rewired.
    [ValidateSet("auto", "x86", "amd64")]
    [string]$Arch = "auto"
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "evidence-common.ps1")

$repo = Get-RepoRoot

# Whether the caller pinned the allowlist. When they did, that one file is the
# policy for every image in the run, including an amd64 one - the gate's own
# tests drive it with fixtures and must not have a second file substituted
# underneath them.
$script:allowPathPinned = ($AllowPath -ne "")

if ($AllowPath -eq "") {
    $AllowPath = Join-Path $PSScriptRoot "xhci98-imports.allow"
}
if ($AllowPathAmd64 -eq "") {
    $AllowPathAmd64 = Join-Path $PSScriptRoot "xhci98-imports-amd64.allow"
}
if ($Amd64Dir -eq "") {
    $Amd64Dir = Join-Path $repo "tools\winxp64-extracted"
}
if ($Amd64ManifestPath -eq "") {
    $Amd64ManifestPath = Join-Path $PSScriptRoot "winxp64-baselines.expected"
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
    param([string]$ForArch = "x86")

    if ($ForArch -eq "amd64") {
        # WDK 7.1 ships no dumpbin.exe; link.exe /dump is the same dumper, and
        # Invoke-Dumpbin inserts the /dump for any exe named link.
        $wdk = $env:WDKROOT
        if ([string]::IsNullOrWhiteSpace($wdk)) {
            $wdk = Join-Path $repo "tools\WinDDK71"
        }
        $link = Join-Path $wdk "bin\x86\amd64\link.exe"
        if (-not (Test-Path -LiteralPath $link)) {
            throw @"
no x64 dumper found at '$link'.
An amd64 image cannot be gated with MSVC 6.0's dumpbin: it reads the file
without error and reports no imports at all, which would pass this gate for
the wrong reason. Unpack WDK 7.1 into tools\WinDDK71 (design record 11
section 4), or set WDKROOT.
"@
        }
        # link.exe needs its own support DLLs from the sibling bin\x86.
        $support = Join-Path $wdk "bin\x86"
        if (Test-Path -LiteralPath $support) {
            $onPath = $false
            foreach ($entry in ($env:PATH -split ';')) {
                $trimmed = $entry.Trim().TrimEnd('\')
                if ($trimmed -ne "" -and $trimmed -eq $support.TrimEnd('\')) { $onPath = $true }
            }
            if (-not $onPath) {
                $env:PATH = "$support;$env:PATH"
            }
        }
        return $link
    }

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
        # link.exe is the same dumper behind a /dump prefix - that is all
        # dumpbin.exe is. Keyed on the file name so every call site can keep
        # passing a plain path.
        if ([System.IO.Path]::GetFileNameWithoutExtension($Exe) -ieq "link") {
            $out = & $Exe /dump $Mode $Path 2>&1
        } else {
            $out = & $Exe $Mode $Path 2>&1
        }
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin $Mode failed on '$Path' (exit $LASTEXITCODE). If that is 53, MSPDB60.DLL is missing from PATH."
    }
    return @($out | ForEach-Object { [string]$_ })
}

function Get-PeMachine {
    param([string]$Path)

    # Read straight out of the file rather than asking a dumper, because the
    # thing being guarded against is a dumper that misreads this very field.
    # Returns $null for anything that is not a PE at all - the gate's own tests
    # drive it with synthetic fixtures, and a non-PE cannot pass the import
    # enforcement below in any case.
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
    } catch {
        return $null
    }
    if ($bytes.Length -lt 0x40) { return $null }
    if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { return $null }   # MZ
    $peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOff -le 0 -or ($peOff + 6) -ge $bytes.Length) { return $null }
    if ($bytes[$peOff] -ne 0x50 -or $bytes[$peOff + 1] -ne 0x45) { return $null }  # PE
    return [System.BitConverter]::ToUInt16($bytes, $peOff + 4)
}

function Test-DumperMachine {
    param(
        [string]$Path,
        [string]$ExpectedArch
    )

    # The guard on the failure this gate would otherwise report as a PASS.
    # MSVC 6.0's dumpbin predates x64: pointed at an amd64 image it exits 0,
    # prints the section summary and prints no import section at all, so the
    # gate would enforce the allowlist against an empty set of imports and find
    # nothing wrong. Tying the image's own machine word to the architecture
    # this run was told it is gating is what makes that combination impossible
    # to reach quietly - the dumper is chosen from the same word.
    $machine = Get-PeMachine -Path $Path
    if ($null -eq $machine) { return }

    $want = if ($ExpectedArch -eq "amd64") { 0x8664 } else { 0x014C }
    if ($machine -ne $want) {
        throw @"
'$Path' is machine 0x$('{0:X4}' -f $machine) but this run is gating $ExpectedArch (expected 0x$('{0:X4}' -f $want)).
Either the image is not the architecture it was staged as, or the wrong dumper
was selected for it. Both matter: MSVC 6.0's dumpbin reports NO imports at all
for an amd64 image, so that pairing passes this gate without having checked a
single import.
"@
    }
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
            $module = $Matches[1]
            $symbol = $Matches[2]
            $notes = ""
            if ($fields.Count -eq 4) {
                $notes = $fields[3]
            }
            # SITES=a.obj,b.obj as the first word of the notes names the only
            # object files that may reference the import (design record 13
            # section 7.5: the pool and DMA calls each live in one file). A row
            # without it is unrestricted.
            $sites = @()
            if ($notes -match "^SITES=(\S*)") {
                $sites = @($Matches[1].ToLower() -split "," | Where-Object { $_ -ne "" })
                if ($sites.Count -eq 0 -or @($sites | Where-Object { $_ -notmatch "^[a-z0-9_]+\.obj$" }).Count -gt 0) {
                    throw "$Path line ${lineNo}: SITES= must list object file names (name.obj), comma separated"
                }
            }
            $allow += [pscustomobject]@{
                Module      = $module
                Symbol      = $symbol
                Flavors     = $flavors
                Requirement = $requirement
                Notes       = $notes
                Sites       = $sites
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


# ---------------------------------------------------------------- evidence ---

function Get-Win2kBaseline {
    param(
        [string]$Dir,
        [string]$Dumpbin,
        [object[]]$ManifestRows,
        # Which target's kernel/HAL set this is. The function is the same check
        # for both architectures; only the diagnostics differ, and a message
        # naming the wrong operating system is worse than none.
        [string]$Label = "Windows 2000 SP4",
        [string]$ExtractSwitch = "-Force -Win2KIso <path>"
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
$Label baseline in '$Dir' is incomplete or unauthenticated:
  - $($validationErrors -join "`n  - ")
Fix it one of these ways:
  - with the recorded media, naming the ISO explicitly - with the switch
    alone and no ISO the script warns and stages nothing:
      scripts\import-gate\extract-target-baselines.ps1 $ExtractSwitch
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

    # ORDINAL, because a PowerShell hashtable is case-INSENSITIVE by default
    # and this map is evidence. `Test-NtkernName` was made `-cmatch` for the
    # 2026-09-07 audit's H3 and this half was left as it was, so a precedent
    # binary importing `EXALLOCATEPOOL` still answered for the allowlist's
    # `ExAllocatePool` - the same wrong evidence in the other of the two
    # places the Win98 rule is enforced, and the one that is quoted into the
    # phase records as `win98-precedent <binary>`. An export name differing
    # only in case is a different symbol, and the loader agrees.
    $map = New-Object System.Collections.Hashtable([System.StringComparer]::Ordinal)
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
    # `-cmatch`, not `-match`: an export name differing only in case is a
    # different symbol, and this line is quoted into the phase records as
    # evidence for the exact name in the allowlist (the 2026-09-07 audit's H3).
    return ($Text -cmatch ("\x00" + [regex]::Escape($Symbol) + "\x00"))
}

# -------------------------------------------------------------------- main ---

function Get-ObjectImportRefs {
    param([string[]]$DumpLines)

    # `dumpbin /symbols` on an object lists each import it calls as an UNDEF
    # External of its thunk: `__imp__ExFreePool@4` (x86 stdcall),
    # `__imp_@IofCallDriver@8` (x86 fastcall), `__imp_ExFreePool` (amd64).
    $names = @()
    foreach ($line in $DumpLines) {
        if ($line -match "\bUNDEF\b.*\bExternal\s+\|\s+__imp_(\S+)") {
            $name = $Matches[1]
            if ($name -match "^[_@](.+)$") {
                $name = $Matches[1]
            }
            if ($name -match "^(.+)@\d+$") {
                $name = $Matches[1]
            }
            $names += $name
        }
    }
    return $names
}

function Test-ImportSites {
    param(
        [string]$ImagePath,
        [object]$Rules,
        [string]$Dumpbin
    )

    $siteRows = @($Rules.Allow | Where-Object { $_.Sites.Count -gt 0 })
    if ($siteRows.Count -eq 0) {
        return
    }

    # build.exe writes the objects beside the image it links them into.
    $objDir = Split-Path -Parent $ImagePath
    $objects = @(Get-ChildItem -LiteralPath $objDir -Filter "*.obj" -File -ErrorAction SilentlyContinue)
    if ($objects.Count -eq 0) {
        Add-Failure "$ImagePath has SITES= rows to check but no object files beside it in $objDir - the per-object rule cannot be read from the image alone."
        return
    }

    $checked = 0
    $anonymous = 0
    foreach ($obj in $objects) {
        $dump = @(Invoke-Dumpbin -Exe $Dumpbin -Mode "/symbols" -Path $obj.FullName)
        # WDK 7.1 compiles amd64 objects for link-time code generation (/GL):
        # the dumper reports them as ANONYMOUS OBJECT and lists no symbols, so
        # nothing can be read from them, and saying "0 references" would read
        # as a pass. They are counted and reported instead.
        if (@($dump | Where-Object { $_ -match "ANONYMOUS OBJECT" }).Count -gt 0) {
            $anonymous++
            continue
        }
        $refs = @(Get-ObjectImportRefs $dump)
        foreach ($row in $siteRows) {
            if ($refs -cnotcontains $row.Symbol) {
                continue
            }
            $checked++
            if ($row.Sites -notcontains $obj.Name.ToLower()) {
                Add-Failure "$($row.Module)!$($row.Symbol) is referenced from $($obj.Name), but its allowlist row restricts it to SITES=$($row.Sites -join ',') (design record 13 section 7.5)."
            }
        }
    }
    if ($anonymous -eq $objects.Count) {
        Add-Warning ("SITES rule NOT CHECKED for $ImagePath`: all $anonymous object file(s) are link-time-code-generation objects (ANONYMOUS OBJECT) " +
            "with no readable symbols. The x86 build's check, over the same sources, is the one that holds the rule.")
        return
    }
    if ($anonymous -gt 0) {
        Add-Failure "SITES rule: $anonymous of $($objects.Count) object file(s) beside $ImagePath are unreadable (ANONYMOUS OBJECT) and the rest are not - a mixed obj directory, so the rule cannot be read from it."
        return
    }
    Write-Ok "SITES rule: $($siteRows.Count) restricted pair(s), $checked object reference(s) checked across $($objects.Count) object file(s)"
}

function Test-Image {
    param(
        [string]$Path,
        [string]$ImageFlavor,
        [object]$Rules,
        [string]$Dumpbin,
        # The kernel/HAL export baselines for THIS image's target: Windows 2000
        # SP4's ten images on x86, NT 5.2 amd64's three on amd64. The variable
        # keeps its old name because the check is the same one; $BaselineLabel
        # is what the diagnostics say, so a failure never names the wrong OS.
        [object]$Win2k,
        [string]$BaselineLabel = "Windows 2000 SP4",
        [object]$Precedent,
        [string]$NtkernText,
        [string]$ImageArch = "x86"
    )

    Write-Step "$ImageFlavor build: $Path"

    $pairs = @(Get-ImportPairs (Invoke-Dumpbin -Exe $Dumpbin -Mode "/imports" -Path $Path))
    if ($pairs.Count -eq 0) {
        # The HCD's task 25.8 scaffold is a DriverEntry that registers nothing
        # and links the pure core, which imports nothing; an empty table is its
        # honest result. Accepted only while the image says it is the scaffold,
        # read from the bytes as make-package.ps1 reads them, so the exception
        # retires itself when 26-A.1 removes the marker.
        $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($Path))
        if ($text.Contains("XHCI98_SCAFFOLD_DO_NOT_STAGE")) {
            Write-Ok "imports nothing at all - accepted for the task 25.8 scaffold, whose marker is in the image"
            return
        }
        Add-Failure "$Path imports nothing at all and does not carry the scaffold marker - past task 25.8 a driver that imports nothing cannot be one."
        return
    }

    # Ordinal for the same reason as the precedent map above: the keys are
    # symbol names, and two that differ only in case are two symbols.
    $matched = New-Object System.Collections.Hashtable([System.StringComparer]::Ordinal)

    foreach ($pair in $pairs) {
        if (-not $pair.ByName) {
            Add-Failure "$($pair.Module) $($pair.Symbol): imported by ordinal. This gate can only reason about names, and an ordinal is not stable across the three usbport lineages - link by name."
            continue
        }

        if ($Rules.Deny.ContainsKey($pair.Symbol)) {
            Add-Failure "$($pair.Module)!$($pair.Symbol) is DENIED: $($Rules.Deny[$pair.Symbol])"
            continue
        }

        # The successor replaces usbport.sys and must not import from it - on a
        # stock Windows 98 SE there is no usbport.sys to resolve against, which
        # is the reason the HCD is a second binary at all (roadmap-hcd.md,
        # decisions table, "Pure HCD, not a registry switch").
        if ($pair.Module -ieq "USBPORT.SYS") {
            Add-Failure "$($pair.Module)!$($pair.Symbol): the HCD imports nothing from usbport.sys - it replaces it, and a stock Windows 98 SE has none to resolve against."
            continue
        }

        $row = $null
        $pairRows = @()
        foreach ($candidate in $Rules.Allow) {
            if ($candidate.Module -ieq $pair.Module -and $candidate.Symbol -ceq $pair.Symbol) {
                $pairRows += $candidate
                if ($candidate.Flavors -eq "all" -or $candidate.Flavors -eq $ImageFlavor) {
                    $row = $candidate
                }
            }
        }

        if ($pairRows.Count -eq 0) {
            $elsewhere = @($Rules.Allow | Where-Object { $_.Symbol -ceq $pair.Symbol })
            if ($elsewhere.Count -gt 0) {
                Add-Failure "$($pair.Module)!$($pair.Symbol): allowed only from $(($elsewhere | ForEach-Object { $_.Module }) -join ', '). The PE import descriptor names the provider, so this is a different import and only one of them resolves."
            } else {
                Add-Failure "$($pair.Module)!$($pair.Symbol): not in the allowlist. Add it to scripts\import-gate\xhci98-imports.allow (or its -amd64 sibling) only with target evidence that it resolves - see that file's header."
            }
            continue
        }

        if ($null -eq $row) {
            $allowedFlavors = ($pairRows | ForEach-Object { $_.Flavors } | Sort-Object -Unique) -join ', '
            Add-Failure "$($pair.Module)!$($pair.Symbol): allowed in $allowedFlavors only, but the $ImageFlavor build imports it."
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
                    Add-Failure "$($pair.Module)!$($pair.Symbol) is not exported by $BaselineLabel $($absent -join ', ') - the driver cannot load on that target."
                } else {
                    $evidence += if ($ImageArch -eq "amd64") { "nt52-amd64-export" } else { "w2k-export" }
                }
            }
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

        #
        # **THE WINDOWS 98 HALF OF THE RULE, ENFORCED RATHER THAN STATED.**
        #
        # `xhci98-imports.allow` states it: "every pair here must carry Win98
        # evidence of its own, because every addition is a new way for the load
        # to fail silently on Win98". Until the 2026-09-07 audit's H1 nothing
        # checked it. The warning above fires only when a pair has NO evidence
        # at all, and a `w2k-export` hit alone satisfies that - so a new kernel
        # or HAL row whose only evidence is that Windows 2000 exports the
        # symbol passed silently, which is precisely the case the rule exists
        # for: Windows 2000 exporting something says nothing about whether
        # Windows 98's ntkern.vxd does.
        #
        # The two things that count as Windows 98 evidence are a precedent
        # binary importing the same pair and the ntkern.vxd export name table
        # carrying the symbol. Both are host-side files this repository does
        # not ship, so the check can only run when they are present: with
        # neither source loaded there is nothing to conclude and the pair is
        # left to the warning above. (No USBPORT.SYS pair reaches this point:
        # the refusal above has already failed it.)
        #
        if ($pair.Module -ieq "ntoskrnl.exe" -or $pair.Module -ieq "hal.dll") {
            if ($ImageArch -eq "amd64") {
                #
                # **THE amd64 RULE, AND IT IS STRONGER THAN THE WINDOWS 98 ONE
                # RATHER THAN A RELAXATION OF IT** (the owner's decision,
                # 2026-09-09; xhci98-imports-amd64.allow's header argues it).
                #
                # The Windows 98 rule below compensates for a target with no
                # on-disk export table: ntkern.vxd builds them at run time, so
                # a precedent binary and a name-table hit are proxies whose
                # absence proves nothing. NT 5.2 amd64 has a real export table,
                # so resolution against it IS the evidence and no proxy is
                # wanted. The $Win2k block above has already failed the pair if
                # the symbol is missing from any baseline image, which is the
                # enforcement; all that is left here is to refuse to call a
                # pair evidenced when the baselines were never loaded.
                #
                if ($null -eq $Win2k) {
                    Add-Warning ("$($pair.Module)!$($pair.Symbol): the $BaselineLabel kernel/HAL " +
                        "baselines are not staged in this working copy, so nothing checked that " +
                        "this symbol resolves on a 64-bit target. Stage them with " +
                        "scripts\import-gate\extract-target-baselines.ps1 -Amd64.")
                }
                continue
            }

            $win98Sources = ($null -ne $Precedent) -or ($null -ne $NtkernText)
            $win98Evidence = @($evidence | Where-Object {
                $_ -like "win98-precedent*" -or $_ -eq "ntkern-name"
            })

            if ($win98Sources -and $win98Evidence.Count -eq 0) {
                Add-Failure ("$($pair.Module)!$($pair.Symbol) has no WINDOWS 98 evidence: " +
                    "no precedent binary imports it and it is not in ntkern.vxd's export " +
                    "name table. A Windows 2000 export does not answer this - see the rule " +
                    "in scripts\import-gate\xhci98-imports.allow, and build-and-test.md, " +
                    "'every pair carries Win98 evidence of its own'. Evidence found: $shown")
            } elseif (-not $win98Sources) {
                Add-Warning ("$($pair.Module)!$($pair.Symbol): the Windows 98 evidence sources " +
                    "are not staged in this working copy, so the Win98 half of the allowlist " +
                    "rule could not be checked for it.")
            }
        }
    }

    Test-ImportSites -ImagePath $Path -Rules $Rules -Dumpbin $Dumpbin

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
    # The x86 dumper is initialised unconditionally because the target-evidence
    # files in steps 2 and 3 are x86 images whatever the driver's architecture.
    # Per-image dumpers are added to this table as they are needed.
    $dumpbin = Initialize-Dumpbin -ForArch "x86"
    $dumpers = @{ "x86" = $dumpbin }

    $rules = Read-AllowFile -Path $AllowPath

    $rules = [pscustomobject]@{
        Allow = @($rules.Allow)
        Deny  = $rules.Deny
    }
    Write-Ok ("allowlist: {0} pairs, {1} denied symbols" -f $rules.Allow.Count, $rules.Deny.Count)

    # Per-architecture rule sets, populated as images of each are met. The x86
    # set is the one read above; the amd64 set comes from its sibling file and
    # is only read when an amd64 image is actually being gated, so an x86-only
    # run never touches it and never warns about baselines it does not need.
    $rulesByArch = @{ "x86" = $rules }
    $script:nt52 = $null

    $images = @()
    if ($Image.Count -gt 0) {
        foreach ($path in $Image) {
            if (-not (Test-Path -LiteralPath $path)) {
                throw "image not found: $path"
            }
            $images += (Resolve-Path -LiteralPath $path).Path
        }
    } else {

        foreach ($candidate in @("objfre\i386", "objchk\i386", "objchk_qemu\i386",
                                 "objfre\amd64", "objchk\amd64", "objchk_qemu\amd64")) {
            $path = Join-Path $repo "src\$candidate\xhci98.sys"
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
        # The architecture, read from the parent directory the way the flavour
        # is read from the grandparent, and settled BEFORE the dumper is chosen
        # because on amd64 the wrong dumper reports no imports rather than
        # failing.
        $imageArch = $Arch
        if ($imageArch -eq "auto") {
            $archDir = Split-Path -Leaf (Split-Path -Parent $path)
            switch ($archDir) {
                "i386"  { $imageArch = "x86" }
                "amd64" { $imageArch = "amd64" }
                default {
                    throw "cannot infer the architecture of '$path': its parent directory is '$archDir', not i386 or amd64. Pass -Arch x86 or -Arch amd64."
                }
            }
        }
        if (-not $dumpers.ContainsKey($imageArch)) {
            $dumpers[$imageArch] = Initialize-Dumpbin -ForArch $imageArch
        }
        $imageDumpbin = $dumpers[$imageArch]
        Test-DumperMachine -Path $path -ExpectedArch $imageArch

        # **Each architecture is held to its own allowlist and its own target
        # evidence.** Windows 2000 SP4's kernels and HALs and the Windows 98
        # precedent set are x86 files and say nothing about NT 5.2 amd64;
        # letting an amd64 binary collect "w2k-export" ticks off them would be
        # worse than having no evidence at all. So the amd64 rules and
        # baselines are separate, loaded here the first time an amd64 image is
        # seen, and the Windows 98 proxy rule does not apply to them at all -
        # see Test-Image, and xhci98-imports-amd64.allow's header for why the
        # rule that replaces it is stronger rather than weaker.
        if ($imageArch -eq "amd64") {
            if (-not $rulesByArch.ContainsKey("amd64")) {
                $amd64AllowPath = if ($script:allowPathPinned) { $AllowPath } else { $AllowPathAmd64 }
                $parsed = Read-AllowFile -Path $amd64AllowPath
                $amd64Rules = [pscustomobject]@{
                    Allow = @($parsed.Allow)
                    Deny  = $parsed.Deny
                }
                $rulesByArch["amd64"] = $amd64Rules
                Write-Ok ("amd64 allowlist: {0} pairs, {1} denied symbols" -f $amd64Rules.Allow.Count, $amd64Rules.Deny.Count)

                if (-not $NoTargetEvidence) {
                    $amd64Manifest = @(Read-Win2kBaselineManifest -Path $Amd64ManifestPath)
                    $script:nt52 = Get-Win2kBaseline -Dir $Amd64Dir -Dumpbin $imageDumpbin `
                        -ManifestRows $amd64Manifest -Label "NT 5.2 amd64" `
                        -ExtractSwitch "-Force -Amd64Iso <path>"
                    if ($null -eq $script:nt52) {
                        Add-Warning "none of the authenticated NT 5.2 amd64 kernel/HAL baselines are present in '$Amd64Dir' - the resolution half of the amd64 gate did not run."
                    } else {
                        Write-Ok ("NT 5.2 amd64 baseline: kernels {0}; HAL {1} (versions, lengths and SHA256 authenticated)" -f `
                            (($script:nt52["ntoskrnl.exe"].Keys | Sort-Object) -join ", "), `
                            (($script:nt52["hal.dll"].Keys | Sort-Object) -join ", "))
                    }
                }
            }
            Test-Image -Path $path -ImageFlavor $imageFlavor -Rules $rulesByArch["amd64"] -Dumpbin $imageDumpbin `
                -Win2k $script:nt52 -BaselineLabel "NT 5.2 amd64" `
                -Precedent $null -NtkernText $null -ImageArch "amd64"
        } else {
            Test-Image -Path $path -ImageFlavor $imageFlavor -Rules $rules -Dumpbin $imageDumpbin `
                -Win2k $win2k -BaselineLabel "Windows 2000 SP4" `
                -Precedent $precedent -NtkernText $ntkernText -ImageArch "x86"
        }
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
