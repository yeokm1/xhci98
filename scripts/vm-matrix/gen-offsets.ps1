<#
.SYNOPSIS
Generate the counter offset table the matrix harness reads a live guest with.

.DESCRIPTION
The harness reads the miniport extension's counters straight out of guest
memory through the QEMU monitor, by byte offset.  This script produces that
offset table.

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

.PARAMETER OutFile
Where to write the table. Defaults to scripts\vm-matrix\offsets.txt.

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
    [string]$Msvc6 = "",
    [switch]$AllowRemovals,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($OutFile -eq "") { $OutFile = Join-Path $PSScriptRoot "offsets.txt" }

if ($Msvc6 -eq "") { $Msvc6 = $env:MSVC6 }
if ([string]::IsNullOrWhiteSpace($Msvc6)) { $Msvc6 = Join-Path $repo "tools\MSVC600" }
$cl = Join-Path $Msvc6 "VC98\BIN\cl.exe"
if (-not (Test-Path -LiteralPath $cl)) {
    throw ("missing {0} - set -Msvc6 or `$env:MSVC6 to the MSVC 6.0 root." -f $cl)
}

# --------------------------------------------------- derive the field list ---
$srcFiles = Get-ChildItem (Join-Path $repo "src\*.c")
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
$rx = [regex]'XHCI_DBG_VALUE_CHANGED\(\s*"([^"]+)"\s*,\s*ext->([A-Za-z_][A-Za-z0-9_.]*(?:\[[A-Za-z_][A-Za-z0-9_]*\])?)\s*\)'
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
    throw "no XHCI_DBG_VALUE_CHANGED(`"...`", ext->Field) pairs found in src\*.c - has the print macro been renamed?"
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
        throw ("{0} field(s) in {1} are no longer derivable from src\*.c and would be dropped: {2}. " -f `
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
    $c += "#include `"xhci.h`""
    $c += ""
    $c += "#define P(f) printf(`"%s %u\n`", #f, (unsigned)offsetof(XHCI_EXTENSION, f))"
    $c += ""
    $c += "int main(void)"
    $c += "{"
    $c += "    printf(`"SIZEOF %u\n`", (unsigned)sizeof(XHCI_EXTENSION));"
    foreach ($f in $fields) { $c += ("    P({0});" -f $f) }
    $c += "    return 0;"
    $c += "}"
    $cFile = Join-Path $work "offsets.c"
    Set-Content -LiteralPath $cFile -Value $c -Encoding ascii

    # MSVC 6 needs Common\MSDev98\Bin on PATH as well as VC98\BIN - that is
    # where MSPDB60.DLL lives.  Without it cl.exe exits 0xC0000135 having
    # printed NOTHING AT ALL: no diagnostic, no .obj, no .exe.  Paid for on
    # in batch 8-A and recorded in scripts\local\regen-offsets.cmd.
    $env:PATH = ("{0}\VC98\BIN;{0}\Common\MSDev98\Bin;{1}" -f $Msvc6, $env:PATH)
    $env:INCLUDE = ("{0}\VC98\INCLUDE" -f $Msvc6)
    $env:LIB = ("{0}\VC98\LIB" -f $Msvc6)

    $exe = Join-Path $work "offsets.exe"
    $obj = Join-Path $work "offsets.obj"
    $srcInc = Join-Path $repo "src"
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
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $raw = & $exe 2>&1
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) { throw ("the generator exited {0}" -f $LASTEXITCODE) }
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
