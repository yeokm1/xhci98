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
The same directory, to verify. Answers exit 0 when the stamp matches, 1 when
it does not, and 2 when there is no stamp at all - which is not a failure: a
binary built before this script existed has nothing to compare against, and
the caller decides whether that is fatal.

.EXAMPLE
powershell -File scripts\source-stamp.ps1 -Write src\objfre\i386
powershell -File scripts\source-stamp.ps1 -Check src\objfre\i386
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
    [System.IO.File]::WriteAllText((Join-Path $Write $stampName), (Get-StampText),
                                   (New-Object System.Text.ASCIIEncoding))
    exit 0
}

if ($Check -ne "") {
    $stampPath = Join-Path $Check $stampName
    if (-not (Test-Path -LiteralPath $stampPath)) {
        Write-Host "no source stamp beside the binary in '$Check'"
        exit 2
    }
    $have = [System.IO.File]::ReadAllText($stampPath)
    $want = Get-StampText
    if ($have -ceq $want) {
        exit 0
    }

    $haveMap = @{}
    foreach ($line in ($have -split "`r?`n")) {
        if ($line -match '^([0-9A-F]{64})\s+(.+)$') { $haveMap[$Matches[2]] = $Matches[1] }
    }
    $changed = New-Object System.Collections.Generic.List[string]
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

    Write-Host ("the binary in '{0}' was built from different sources:" -f $Check)
    foreach ($c in ($changed | Sort-Object)) { Write-Host ("  - " + $c) }
    exit 1
}

throw "pass -Write <objdir> or -Check <objdir>."
