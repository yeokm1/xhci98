<#
.SYNOPSIS
Record, or check, which driver sources a built xhci98.sys came from.

.DESCRIPTION
**WHY THIS IS NOT A TIMESTAMP COMPARISON.** `make-release.ps1` already refuses
a staged XHCIQUAL or XHCISNAP older than its own sources, and until the
2026-09-07 audit's H13 there was no equivalent for the driver: the only tie
between `src\` and the binary in `src\objfre` was the file version against the
INF's, which cannot see a code edit that does not bump the version. So a
release could ship a `.sys` built from a tree that no longer exists, and the
shipped bytes would be right only by luck.

The obvious fix - refuse if any source file's mtime is newer than the binary's
- does not work here, and the reason is worth stating because it will be
proposed again. An mtime moves when a file is written, whatever was written:
checking out a branch, reverting an experiment, or a comment-only commit all
make a source "newer" than a binary it is byte-for-byte responsible for. On
this repository that is not hypothetical - the audit found two headers already
newer than the built binaries from a comment-only commit, so an mtime rule
would have had to be bypassed on its first use, and a gate that is bypassed
once is bypassed thereafter.

So this records CONTENT. `-Write` hashes every file the DDK build reads and
writes the list beside the binary; `-Check` recomputes and compares. A file
rewritten with the same bytes is not a change. A file whose bytes differ is,
whatever its timestamp says, and whether it moved forward or back.

**And the binary's own hash, on the stamp's last line.** The source list alone
says what `src\` held; it does not say which `.sys` it was written beside. A
binary restored into the build directory from an archive - the same version,
the same flavour, different bytes - therefore kept a stamp that still matched
a tree nobody had touched, and passed. `make-release.ps1` could not catch that
from its side either: its staged-against-built comparison has the restored
file on BOTH sides. The stamp is the only thing that was present at the moment
the sources and the binary were one event, so it is where the binding belongs.

The source set is derived from `src\sources` rather than listed here, so a
`.c` added to the build cannot be left out of the stamp by forgetting this
file. Headers are not in `SOURCES` - the DDK discovers them by including them
- so every `.h` in `src\` is stamped as well, which over-covers rather than
under-covers: a header nothing includes changing is a false positive, and a
false positive here costs a rebuild while a false negative ships a binary
nobody can reproduce.

.PARAMETER Write
The `src\obj*\i386` directory holding the binary to stamp.

.PARAMETER Check
The same directory, to verify. Answers exit 0 when both the sources and the
binary match, 1 when either does not, and 2 when there is no usable stamp -
either none at all, or one written before the binary line existed, since a
stamp that can answer only half the question must not report a match. Exit 2
is not itself a verdict: a binary built before this script existed has nothing
to compare against, and the caller decides whether that is fatal.

.EXAMPLE
powershell -File scripts\source-stamp.ps1 -Write src\objfre\i386
powershell -File scripts\source-stamp.ps1 -Check src\objfre\i386

Both take the directory, not the file: the stamp is written beside
`xhci98.sys` there and now records that file's SHA-256 as well.
#>
param(
    [string]$Write = "",
    [string]$Check = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $repo "src"
$stampName = "xhci98.srcstamp"
$binaryName = "xhci98.sys"
# Prefixed rather than listed like a source file, so the source comparison
# below cannot mistake it for a file that appeared in src\.
$binaryTag = "BINARY"

function Get-SourceFiles {
    $wanted = New-Object System.Collections.Generic.List[string]

    # The .c list comes from src\sources, so it cannot drift from the build.
    $sources = [System.IO.File]::ReadAllText((Join-Path $srcDir "sources"))
    $m = [regex]::Match($sources, '(?ms)^SOURCES\s*=(.*?)(?:\r?\n\r?\n|\r?\n[A-Z])')
    if (-not $m.Success) {
        throw "src\sources has no SOURCES= list this script can read."
    }
    foreach ($tok in ($m.Groups[1].Value -split '[\s\\]+')) {
        $t = $tok.Trim()
        if ($t -eq "") { continue }
        $wanted.Add($t)
    }

    # Every header, plus the build files themselves: `sources` decides which
    # objects are built and with which defines, and `makefile` drives it.
    foreach ($f in (Get-ChildItem -LiteralPath $srcDir -File)) {
        if ($f.Extension -ieq ".h") { $wanted.Add($f.Name) }
    }
    foreach ($n in @("sources", "makefile")) {
        if (Test-Path -LiteralPath (Join-Path $srcDir $n)) { $wanted.Add($n) }
    }

    return @($wanted | Sort-Object -Unique)
}

# The stamp's last line, and the reason there is one. The source hashes above
# say what src\ held; they say nothing about WHICH binary they were beside, so
# a `.sys` restored into the build directory from an archive - same version,
# same flavour, different bytes - kept a stamp that still matched the tree and
# passed. make-release.ps1's staged-vs-built comparison could not see it
# either: it compares the staged copy against that same restored file, so both
# sides are the wrong binary. Only the stamp can close this, because only the
# stamp was written at the moment the sources and the binary were the same
# event.
function Get-BinaryStampLine {
    param([Parameter(Mandatory = $true)][string]$Dir)
    $bin = Join-Path $Dir $binaryName
    if (-not (Test-Path -LiteralPath $bin)) {
        throw "no '$binaryName' in '$Dir' to stamp."
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = [System.BitConverter]::ToString(
        $sha.ComputeHash([System.IO.File]::ReadAllBytes($bin))).Replace("-", "")
    return ("{0} {1} {2}" -f $binaryTag, $hash, $binaryName)
}

function Get-StampText {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($name in (Get-SourceFiles)) {
        $path = Join-Path $srcDir $name
        if (-not (Test-Path -LiteralPath $path)) {
            throw "src\sources names '$name', which is not in src\."
        }
        $hash = [System.BitConverter]::ToString(
            $sha.ComputeHash([System.IO.File]::ReadAllBytes($path))).Replace("-", "")
        $lines.Add(("{0} {1}" -f $hash, $name))
    }
    return (($lines -join "`r`n") + "`r`n")
}

if ($Write -ne "") {
    if (-not (Test-Path -LiteralPath $Write)) {
        throw "no such directory: '$Write'"
    }
    $text = (Get-StampText) + (Get-BinaryStampLine -Dir $Write) + "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $Write $stampName), $text,
                                   (New-Object System.Text.ASCIIEncoding))
    exit 0
}

# The source hashes the stamp holds, against src\ as it is now. Returns the
# human list of differences, empty when they match.
function Get-SourceChanges {
    param([Parameter(Mandatory = $true)][string]$Have)
    $want = Get-StampText
    $changed = New-Object System.Collections.Generic.List[string]
    if ($Have -ceq $want) { return $changed }

    $haveMap = @{}
    foreach ($line in ($Have -split "`r?`n")) {
        if ($line -match '^([0-9A-F]{64})\s+(.+)$') { $haveMap[$Matches[2]] = $Matches[1] }
    }
    foreach ($line in ($want -split "`r?`n")) {
        if ($line -notmatch '^([0-9A-F]{64})\s+(.+)$') { continue }
        $hash = $Matches[1]
        $name = $Matches[2]
        if (-not $haveMap.ContainsKey($name)) {
            $changed.Add("$name (added since the build)")
        } elseif ($haveMap[$name] -ne $hash) {
            $changed.Add("$name (contents differ)")
            [void]$haveMap.Remove($name)
        } else {
            [void]$haveMap.Remove($name)
        }
    }
    foreach ($name in $haveMap.Keys) { $changed.Add("$name (removed since the build)") }
    return $changed
}

if ($Check -ne "") {
    $stampPath = Join-Path $Check $stampName
    if (-not (Test-Path -LiteralPath $stampPath)) {
        Write-Host "no source stamp beside the binary in '$Check'"
        exit 2
    }
    $haveRaw = [System.IO.File]::ReadAllText($stampPath)

    # Split the binary line off, so the two questions are answered separately
    # and a failure names which of them it was.
    $haveBinary = ""
    $keep = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($haveRaw -split "`r?`n")) {
        if ($line -cmatch ("^" + $binaryTag + "\s+([0-9A-F]{64})\s+(.+)$")) {
            $haveBinary = $Matches[1]
            continue
        }
        if ($line -ne "") { $keep.Add($line) }
    }
    $have = (($keep -join "`r`n") + "`r`n")

    #
    # **THE SOURCES ARE COMPARED FIRST, WHATEVER FORMAT THE STAMP IS IN.**
    #
    # This ordering is the fix to a regression the binary line introduced. The
    # first cut answered 2 - "no usable stamp" - the moment it found no BINARY
    # line, BEFORE looking at the sources at all. An old-format stamp whose
    # sources had genuinely changed therefore stopped answering 1, which
    # make-release.ps1 refuses outright, and started answering 2, which
    # -AllowUnstampedDriver is allowed to wave through. A change meant to make
    # this stricter had made the oldest guarantee bypassable for exactly the
    # binaries least able to afford it.
    #
    # A source mismatch is knowable from any stamp ever written, so it is
    # decided before anything about the format is. Exit 2 then means the
    # binary's identity is unavailable AND nothing was found to refuse: here,
    # because the sources agree and only the BINARY line is missing; at the
    # top of this block, because there is no stamp at all and neither half is
    # knowable. The caller treats both the same way, which is why they share
    # a code, but only this one has checked anything.
    #
    # @(...) because the pipeline unrolls a returned list, and an EMPTY one
    # unrolls to $null - which under Set-StrictMode is a terminating error on
    # .Count rather than a zero. The matching case is the common one, so this
    # would have failed every check that should have passed.
    $changed = @(Get-SourceChanges -Have $have)
    if ($changed.Count -gt 0) {
        Write-Host ("the binary in '{0}' was built from different sources:" -f $Check)
        foreach ($c in ($changed | Sort-Object)) { Write-Host ("  - " + $c) }
        exit 1
    }

    if ($haveBinary -eq "") {
        # A stamp written before the binary line existed cannot say WHICH .sys
        # those matching sources were hashed beside, and answering only half
        # the question is what let a restored binary through. The caller's own
        # rule for "no usable stamp" applies.
        Write-Host ("the stamp beside the binary in '{0}' predates the binary hash: the sources match, but it cannot say which .sys they were hashed beside" -f $Check)
        exit 2
    }

    $wantBinary = (Get-BinaryStampLine -Dir $Check) -split "\s+"
    if ($haveBinary -ne $wantBinary[1]) {
        Write-Host ("the stamp in '{0}' was written for a different {1}:" -f $Check, $binaryName)
        Write-Host ("  - stamped {0}" -f $haveBinary)
        Write-Host ("  - present {0}" -f $wantBinary[1])
        Write-Host "  The sources match; this binary is not the one they were hashed beside."
        exit 1
    }

    exit 0
}

throw "pass -Write <objdir> or -Check <objdir>."
