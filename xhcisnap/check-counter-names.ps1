# check-counter-names.ps1 - XHCISNAP's counter names against the driver's
# counter block, word by word (task 36.35's 18-word shift; p36-int3).
#
# `XHCISNAP -selftest-counters` prints the counter section of a canned
# image whose word i holds i + 1. This script reads XHCIHC_COUNTERS from
# src\xhci_counters.h - the driver's own field order, which XHCISNAP's
# tables only copy - and requires, for every word: a field's line with
# its name and the value i + 1; a reserved array (none since stage B filled SuspBReserved)
# not printed at all; and each word past the struct printed as
# "counter N". So a name table out of step with the header, or a lookup
# the compiler places off the wrong base, fails here on the built EXE.
#
# Usage: check-counter-names.ps1 -Log <selftest output>. Exit 0 = every
# word agrees.
param([Parameter(Mandatory = $true)][string]$Log)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$header = Join-Path $repo 'src\xhci_counters.h'
$text = [IO.File]::ReadAllText($header)
$start = $text.IndexOf('typedef struct _XHCIHC_COUNTERS {')
$end = $text.IndexOf('} XHCIHC_COUNTERS', $start)
if ($start -lt 0 -or $end -lt 0) {
    Write-Output "FAIL: XHCIHC_COUNTERS not found in $header"
    exit 1
}
$body = $text.Substring($start, $end - $start)
# Comments out, then one entry per ULONG field: a name, or a reserved run.
$body = [regex]::Replace($body, '/\*.*?\*/', ' ',
                         [Text.RegularExpressions.RegexOptions]::Singleline)
$words = New-Object System.Collections.Generic.List[string]
foreach ($m in [regex]::Matches($body, 'ULONG\s+([A-Za-z0-9_]+)\s*(\[\s*(\d+)\s*\])?\s*;')) {
    if ($m.Groups[2].Success) {
        for ($k = 0; $k -lt [int]$m.Groups[3].Value; $k++) { $words.Add('') }
    } else {
        $words.Add($m.Groups[1].Value)
    }
}

$lines = Get-Content $Log
$sect = $false
$printed = @{}
foreach ($l in $lines) {
    if ($l -like 'controller counters (XHCIHC_COUNTERS)*') { $sect = $true; continue }
    if (-not $sect) { continue }
    if ($l -match '^\s+\((\d+) of (\d+) nonzero\.\)') { break }
    if ($l -match '^\s+counter\s+(\d+)\s+(\d+)\s*$') {
        $printed["counter $($Matches[1])"] = [long]$Matches[2]
    } elseif ($l -match '^\s+([A-Za-z0-9_]+)\s+(\d+)\s*$') {
        $printed[$Matches[1]] = [long]$Matches[2]
    }
}

$failed = 0
$reserved = 0
for ($i = 0; $i -lt $words.Count; $i++) {
    $n = $words[$i]
    if ($n -eq '') {
        $reserved++
        foreach ($k in $printed.Keys) {
            if ($printed[$k] -eq ($i + 1)) {
                Write-Output "FAIL: reserved word $i printed as $k"
                $failed++
            }
        }
        continue
    }
    if (-not $printed.ContainsKey($n)) {
        Write-Output "FAIL: word $i ($n) not printed by name"
        $failed++
    } elseif ($printed[$n] -ne ($i + 1)) {
        Write-Output "FAIL: word $i ($n) printed with $($printed[$n]), the value of word $($printed[$n] - 1)"
        $failed++
    }
}
foreach ($i in @($words.Count, ($words.Count + 1))) {
    if ($printed["counter $i"] -ne ($i + 1)) {
        Write-Output "FAIL: word $i, past the block, not printed as counter $i"
        $failed++
    }
}
if ($failed -ne 0) {
    Write-Output "counter names: $failed failure(s) over $($words.Count) words"
    exit 1
}
Write-Output "counter names: all $($words.Count) words agree ($reserved reserved, not printed)"
exit 0
