<#
.SYNOPSIS
Generate the counter offset table the matrix harness reads a live guest with.

.DESCRIPTION
The harness reads the driver's counters straight out of guest memory through
the QEMU monitor, by byte offset.  This script produces that offset table.

TWO DRIVERS, ONE OF THEM FROZEN (roadmap-hcd.md 26-A.10; design record 13
section 9.5).  Until 2026-10-02 the counters were the usbport miniport's
XHCI_EXTENSION, published as `ext->Field`, and the table was offsets.txt and
offsets-amd64.txt.  The miniport left the tree that day, so that table can no
longer be derived from it: it is kept frozen for matrix.psd1, and
`-Driver miniport` is refused here rather than allowed to overwrite it from
sources that no longer describe it (regenerate it from branch 1.2.0.0).
The default, `-Driver hcd`, is the successor's counter block: one DDK-free
struct, XHCIHC_COUNTERS in src\xhci_counters.h (section 9.4 item 1),
published as `cnt->Field`, read from the files src\sources names rather than
the src\*.c glob, and written to offsets-hcd.txt and offsets-hcd-amd64.txt.
Everything below about the derivation holds for both; where it says
`ext->`, read the driver's pointer name.

WHY IT IS DERIVED AND NOT A LIST.  scripts\local\offsets.c is a 509-line
hand-maintained roster of fields, and it is one host's file rather than the
repository's - so a clone cannot read counters at all, and the copy that does
exist is already missing counters this project has since added (`DescIsoDerived`
among them).  A row set assembled by hand is only as complete as whoever
assembled it: that is task 7b-A.1.0's finding about `OpensTotal`, and it applies
to the offset table exactly as it applied to the counter set.

So the field list comes from the DRIVER.  Every counter the driver publishes is
published through one of

    XHCI_DBG_VALUE_CHANGED("<human label>", ext-><Field>);

and this script parses those pairs out of src\*.c, emits one `offsetof` per
distinct field, compiles that with MSVC 6.0 and runs it.  The table therefore
cannot drift from the generator-matchable print sites: a counter given such a
site appears here on the next run, and a counter renamed in the driver renames
here too, which is what makes an expectation naming a stale label fail LOUDLY
instead of matching nothing.  What it does NOT do is see a counter with no
matchable site - see the grammar below and the header's silent-loss note - so
"added to the driver" is not the same as "in the table".

The pairs this finds are the scalar extension counters plus array elements
subscripted by a single constant name (`ext->ProbeEpEvents[XHCI_PROBE_EVENT_CLOSE]`),
and their number is whatever the driver currently declares - do not write it
down here, because the whole point of generating the table is that nothing has
to be kept in step by hand.  Print sites that compute a value, read a local, or
subscript with anything but a bare constant name - a macro call such as
`EventCounts[XHCI_EVENT_TYPE_INDEX(...)]` - are deliberately not matched: the
first two are not addressable by a single offset, and the last is outside the
grammar this parser accepts, which is one terminal subscript holding one
identifier.  That is a choice about how much C this regex should understand,
not a reader limitation - the readers split rows on whitespace and use the name
as a literal key, so parentheses would survive them *(round 14 corrected this
sentence, which had blamed the readers)*.  *(Until
review round 13 every subscript was excluded, so `ProbeEpEvents[]`'s two print sites
were silently absent from the table while the roadmap and `src/xhci.h` told a
reader to take `OpensTotal` against `ProbeEpEvents[OPEN] + [REOPEN]` from it -
review round 13.)*

THE ONE WAY THIS DERIVATION LOSES A COUNTER SILENTLY.  The pattern above wants
ONE quoted string before the comma, and C concatenates adjacent string literals,
so a label wrapped across two lines

    XHCI_DBG_VALUE_CHANGED("device commands answered or refused for a "
                           "re-enumerated tenancy", ext->CommandsStaleTenancy);

still compiles and still prints correctly, and this script simply does not see
it.  The field then leaves offsets.txt while SIZEOF is unchanged, so
Assert-OffsetsFresh - which compares SIZEOF against the running driver - passes,
and the loss surfaces only as a counter nobody can name.  That happened once,
from a review round widening a label and wrapping it in the doing.

Hence -AllowRemovals below.  A regeneration that DROPS a field the previous
table had is refused by default and names the field, because the overwhelmingly
likely cause is a print site that stopped being matchable rather than a counter
deliberately retired.  Retiring one is legitimate and rare, and then the switch
says so out loud.  Keep a label a single literal; if it will not fit, shorten
the label rather than wrapping it.

.PARAMETER Driver
hcd (the default): the successor's counter block. miniport: refused - its
table is frozen (see above).

.PARAMETER OutFile
Where to write the table. Defaults to scripts\vm-matrix\offsets-hcd.txt, or
scripts\vm-matrix\offsets-hcd-amd64.txt with -Arch amd64.

.PARAMETER SrcDir
The source directory: its `sources` file names the .c files read, and it is
the include directory the header is compiled from. Defaults to the
repository's src. Another directory is for a stand-in tree, which is how the
derivation is exercised before the counter block exists.

.PARAMETER Header
.PARAMETER Struct
.PARAMETER Pointer
The counter block's header, struct and the pointer name its print sites use
(`XHCI_DBG_VALUE_CHANGED("label", <Pointer>->Field)`). Default
xhci_counters.h, XHCIHC_COUNTERS and cnt - design record 13 section 9.4's
working names; if the HCD settles on others, change the defaults here.

.PARAMETER Arch
Which build's layout to measure. x86 (the default) compiles with MSVC 6.0,
the compiler the 32-bit driver is built with. amd64 compiles with WDK 7.1's
amd64 cross compiler - the driver's own amd64 compiler, and the one
test\run-host-tests.cmd's amd64 legs use - and runs the result on this host.
The two layouts differ (every pointer-sized member moves), so a post-release
target running the amd64 driver needs its own table: its SIZEOF is what
Assert-OffsetsFresh and the image stamp check against.

.PARAMETER Wdk71
Root of WDK 7.1, for -Arch amd64. Defaults to $env:WDK71, then the repo's
tools\WinDDK71.

.PARAMETER AllowRemovals
Permit the new table to lack fields the existing one had. Off by default: see
above - a silent removal is almost always a print site this script stopped
matching, not a counter that was removed on purpose.

.PARAMETER Msvc6
Root of the MSVC 6.0 toolchain. Defaults to $env:MSVC6, then the repo's
tools\MSVC600.
#>
[CmdletBinding()]
param(
    [string]$OutFile = "",
    [ValidateSet('x86', 'amd64')][string]$Arch = 'x86',
    [ValidateSet('hcd', 'miniport')][string]$Driver = 'hcd',
    [string]$SrcDir = "",
    [string]$Header = "xhci_counters.h",
    [string]$Struct = "XHCIHC_COUNTERS",
    [string]$Pointer = "cnt",
    [string]$Msvc6 = "",
    [string]$Wdk71 = "",
    [switch]$AllowRemovals,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($Driver -eq 'miniport') {
    throw ("-Driver miniport is refused: the miniport left src\ on 2026-10-02, so its XHCI_EXTENSION print sites are no longer in this tree, and a derivation here would overwrite offsets.txt / offsets-amd64.txt - the frozen table matrix.psd1 is read with - from sources that no longer describe it. Regenerate it from a checkout of branch 1.2.0.0.")
}
if ($OutFile -eq "") {
    $OutFile = Join-Path $PSScriptRoot $(if ($Arch -eq 'amd64') { "offsets-hcd-amd64.txt" } else { "offsets-hcd.txt" })
}
if ($SrcDir -eq "") { $SrcDir = Join-Path $repo "src" }
$SrcDir = (Resolve-Path -LiteralPath $SrcDir).Path
if ($Pointer -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw ("-Pointer '{0}' is not a C identifier" -f $Pointer) }
if ($Struct -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw ("-Struct '{0}' is not a C identifier" -f $Struct) }
if (-not (Test-Path -LiteralPath (Join-Path $SrcDir $Header))) {
    throw ("{0} is not in {1}. The HCD's counter block - one DDK-free struct {2} that every counter the matrix reads lives in, published through XHCI_DBG_VALUE_CHANGED(`"<label>`", {3}->Field) sites - is design record 13 section 9.4 item 1, and this table is derived from it. Add the block first (or pass -Header / -Struct / -Pointer if it was named otherwise)." -f `
        $Header, $SrcDir, $Struct, $Pointer)
}

if ($Arch -eq 'amd64') {
    if ($Wdk71 -eq "") { $Wdk71 = $env:WDK71 }
    if ([string]::IsNullOrWhiteSpace($Wdk71)) { $Wdk71 = Join-Path $repo "tools\WinDDK71" }
    $cl = Join-Path $Wdk71 "bin\x86\amd64\cl.exe"
    if (-not (Test-Path -LiteralPath $cl)) {
        throw ("missing {0} - set -Wdk71 or `$env:WDK71 to the WDK 7.1 root." -f $cl)
    }
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw "-Arch amd64 runs the generated program, and this host cannot execute an amd64 binary."
    }
} else {
    if ($Msvc6 -eq "") { $Msvc6 = $env:MSVC6 }
    if ([string]::IsNullOrWhiteSpace($Msvc6)) { $Msvc6 = Join-Path $repo "tools\MSVC600" }
    $cl = Join-Path $Msvc6 "VC98\BIN\cl.exe"
    if (-not (Test-Path -LiteralPath $cl)) {
        throw ("missing {0} - set -Msvc6 or `$env:MSVC6 to the MSVC 6.0 root." -f $cl)
    }
}

# --------------------------------------------------- derive the field list ---
# THE FILES src\sources NAMES, NOT THE GLOB (design record 13 section 9.5).
# src\ holds files no image is built from - the miniport's Adapted files sat
# there unbuilt until 26-A.2 - and a print site in one of them would put a
# field in the table that no running driver has. The SOURCES= macro is read
# with its backslash continuations; only its .c entries are source.
$sourcesFile = Join-Path $SrcDir "sources"
if (-not (Test-Path -LiteralPath $sourcesFile)) { throw ("no sources file in {0}, so which files the image is built from is unknown" -f $SrcDir) }
$srcNames = @()
$inSources = $false
foreach ($line in (Get-Content -LiteralPath $sourcesFile)) {
    $l = ($line -replace '#.*$', '').Trim()
    if (-not $inSources) {
        if ($l -match '^SOURCES\s*=\s*(.*)$') { $inSources = $true; $l = $Matches[1] } else { continue }
    }
    $cont = $l.EndsWith('\')
    $l = $l.TrimEnd('\').Trim()
    foreach ($tok in ($l -split '\s+')) { if ($tok -match '^[A-Za-z0-9_]+\.c$') { $srcNames += $tok } }
    if (-not $cont) { break }
}
if ($srcNames.Count -eq 0) { throw ("{0} names no .c file in SOURCES=" -f $sourcesFile) }
$srcFiles = @()
foreach ($n in $srcNames) {
    $f = Join-Path $SrcDir $n
    if (-not (Test-Path -LiteralPath $f)) { throw ("{0} names {1}, which is not in {2}" -f $sourcesFile, $n, $SrcDir) }
    $srcFiles += (Get-Item -LiteralPath $f)
}
$all = ($srcFiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
# The member path may be NESTED - `ext->Topology.Descriptors` - and offsetof
# takes a dotted path perfectly well.  A first version matched only a bare
# identifier and silently produced no offset for the whole topology block, which
# the matrix validator then reported as "no counter named 'topology: hub
# descriptors folded'".  That is the right failure and it is why the validator
# runs before a boot rather than after one, but the fix belongs here.
# A subscript is accepted only as one bare constant name - `Field[XHCI_SOME_INDEX]`
# - which offsetof takes as a member designator (`&((T*)0)->Field[IDX]`) and
# which stringizes to a row name with no whitespace in it.  A computed or
# macro-call index stays outside this grammar (see the header).
# The identifier before `->` is the driver's pointer name (-Pointer), the one
# thing the grammar does not fix.
$rx = [regex]('XHCI_DBG_VALUE_CHANGED\(\s*"([^"]+)"\s*,\s*' + $Pointer + '->([A-Za-z_][A-Za-z0-9_.]*(?:\[[A-Za-z_][A-Za-z0-9_]*\])?)\s*\)')
$pairs = @{}
$fieldOfLabel = @{}
foreach ($m in $rx.Matches($all)) {
    $label = $m.Groups[1].Value
    $field = $m.Groups[2].Value
    # A label naming two different fields would make an expectation ambiguous,
    # and the harness would silently read one of them.  Refuse instead.
    if ($fieldOfLabel.ContainsKey($label) -and $fieldOfLabel[$label] -ne $field) {
        throw ("the label '{0}' is printed from two different fields ({1} and {2}). An expectation naming it could not be resolved." -f `
            $label, $fieldOfLabel[$label], $field)
    }
    $fieldOfLabel[$label] = $field
    $pairs[$field] = $label
}
$fields = $pairs.Keys | Sort-Object
if ($fields.Count -eq 0) {
    throw ("no XHCI_DBG_VALUE_CHANGED(`"...`", {0}->Field) pairs found in the {1} files {2} names - has the print macro or the pointer name been renamed?" -f $Pointer, $srcFiles.Count, $sourcesFile)
}
if (-not $Quiet) { Write-Host ("derived {0} counter fields from {1} source files" -f $fields.Count, $srcFiles.Count) }

# ------------------------------------------- refuse a regeneration that LOSES ---
# The table growing is ordinary; the table SHRINKING is the symptom of the one
# failure mode this derivation has (see the header).  A dropped field costs
# nothing anyone notices - SIZEOF is unaffected, so the harness's freshness
# check still passes - and surfaces much later as a counter that cannot be
# named.  So compare against the table already on disk and stop, naming what
# would go.  This runs BEFORE the compile so the existing table is still intact
# when it throws.
if (-not $AllowRemovals -and (Test-Path -LiteralPath $OutFile)) {
    $derived = @{}
    foreach ($f in $fields) { $derived[$f] = $true }
    $lost = @()
    foreach ($line in (Get-Content -LiteralPath $OutFile)) {
        # SIZEOF is emitted by this script, not derived from a print site.
        if ($line -match '^(\S+)\s+\d+\s*$' -and $matches[1] -ne "SIZEOF") {
            if (-not $derived.ContainsKey($matches[1])) { $lost += $matches[1] }
        }
    }
    if ($lost.Count -gt 0) {
        throw ("{0} field(s) in {1} are no longer derivable from the files src\sources names and would be dropped: {2}. " -f `
                   $lost.Count, $OutFile, ($lost -join ", ")) +
              "The usual cause is a print site this script stopped matching - most often a label " +
              "split across adjacent string literals, which C concatenates but the pattern does not. " +
              "Make the label one literal. If the counter really was retired, re-run with -AllowRemovals."
    }
}

# --------------------------------------------------------- emit and build ---
$work = Join-Path $env:TEMP ("xhci98-offsets-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    $c = @()
    $c += "/* GENERATED by scripts\vm-matrix\gen-offsets.ps1 - do not edit. */"
    $c += "#include <stdio.h>"
    $c += "#include <stddef.h>"
    $c += ("#include `"{0}`"" -f $Header)
    $c += ""
    $c += ("#define P(f) printf(`"%s %u\n`", #f, (unsigned)offsetof({0}, f))" -f $Struct)
    $c += ""
    $c += "int main(void)"
    $c += "{"
    $c += ("    printf(`"SIZEOF %u\n`", (unsigned)sizeof({0}));" -f $Struct)
    foreach ($f in $fields) { $c += ("    P({0});" -f $f) }
    $c += "    return 0;"
    $c += "}"
    $cFile = Join-Path $work "offsets.c"
    Set-Content -LiteralPath $cFile -Value $c -Encoding ascii

    # MSVC 6 needs Common\MSDev98\Bin on PATH as well as VC98\BIN - that is
    # where MSPDB60.DLL lives.  Without it cl.exe exits 0xC0000135 having
    # printed NOTHING AT ALL: no diagnostic, no .obj, no .exe.  Paid for on
    # in batch 8-A and recorded in scripts\local\regen-offsets.cmd.
    if ($Arch -eq 'amd64') {
        # The same environment test\run-host-tests.cmd gives its amd64 legs:
        # the WDK's own CRT headers and import libraries, nothing outside tools\.
        $env:PATH = ("{0}\bin\x86\amd64;{0}\bin\x86;{1}" -f $Wdk71, $env:PATH)
        $env:INCLUDE = ("{0}\inc\crt;{0}\inc\api" -f $Wdk71)
        $env:LIB = ("{0}\lib\Crt\amd64;{0}\lib\wnet\amd64" -f $Wdk71)
    } else {
        $env:PATH = ("{0}\VC98\BIN;{0}\Common\MSDev98\Bin;{1}" -f $Msvc6, $env:PATH)
        $env:INCLUDE = ("{0}\VC98\INCLUDE" -f $Msvc6)
        $env:LIB = ("{0}\VC98\LIB" -f $Msvc6)
    }

    $exe = Join-Path $work "offsets.exe"
    $obj = Join-Path $work "offsets.obj"
    $srcInc = $SrcDir
    # ErrorActionPreference is relaxed across both native calls on purpose, the
    # same way `scripts\import-gate\check-imports.ps1` relaxes it for dumpbin
    # (repo audit D4): in Windows PowerShell 5.1 a native command's stderr line
    # becomes an ErrorRecord, and under "Stop" the first one aborts this script
    # with an EMPTY message instead of the diagnosis below - which is exactly the
    # message that says what cl.exe objected to. cl warns to stderr routinely.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $buildLog = & $cl /nologo /W3 /DXHCI_HOST_TEST /I $srcInc $cFile ("/Fe" + $exe) ("/Fo" + $obj) 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $saved
    }
    if (-not (Test-Path -LiteralPath $exe)) {
        throw ("cl.exe produced no executable. Output:{0}{1}" -f [Environment]::NewLine, $buildLog)
    }

    # Into a temporary file first.  `> offsets.txt` truncates it the instant the
    # command starts, so a generator that then crashes leaves a half-written
    # table behind while this script reports a failure - destroying the last
    # trustworthy reading on the failure path.
    # Smart App Control blocks a freshly linked unsigned exe on some launches
    # with a Device Guard message (test\run-host-tests.cmd, lessons.md); that
    # one message is retried, anything else is the generator's own failure.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        for ($attempt = 1; $attempt -le 5; $attempt++) {
            $raw = & $exe 2>&1
            if ($LASTEXITCODE -eq 0) { break }
            if ((($raw | Out-String) -notmatch 'Device Guard|blocked by')) { break }
            Start-Sleep -Seconds 1
        }
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) { throw ("the generator exited {0}: {1}" -f $LASTEXITCODE, ($raw | Out-String)) }
    $lines = @($raw | ForEach-Object { $_.ToString().TrimEnd() } | Where-Object { $_ -ne "" })
    $sizeofLine = $lines | Where-Object { $_ -like "SIZEOF *" }
    if (-not $sizeofLine) {
        throw "the generator produced no SIZEOF line, so its output cannot be checked for staleness. Nothing was written."
    }
    # Every field asked for must have come back, or the table is short and the
    # shortfall would read as "that counter does not exist" at verdict time.
    if ($lines.Count -ne ($fields.Count + 1)) {
        throw ("asked for {0} fields plus SIZEOF and got {1} lines back." -f $fields.Count, $lines.Count)
    }

    $dir = Split-Path -Parent $OutFile
    if ($dir -ne "" -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Set-Content -LiteralPath $OutFile -Value $lines -Encoding ascii

    # The label map goes beside it.  The harness's expectations are written in
    # the driver's own human labels - which is what every result box in this
    # repository already quotes - and this is what resolves one to a field.
    $mapFile = [IO.Path]::ChangeExtension($OutFile, ".labels.txt")
    $mapLines = @()
    foreach ($label in ($fieldOfLabel.Keys | Sort-Object)) {
        $mapLines += ("{0}`t{1}" -f $fieldOfLabel[$label], $label)
    }
    Set-Content -LiteralPath $mapFile -Value $mapLines -Encoding utf8

    if (-not $Quiet) {
        Write-Host ("{0}" -f $sizeofLine)
        Write-Host ("written: {0} ({1} offsets)" -f $OutFile, $fields.Count)
        Write-Host ("written: {0} ({1} labels)" -f $mapFile, $fieldOfLabel.Count)
    }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
