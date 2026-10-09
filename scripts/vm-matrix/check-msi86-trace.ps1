<#
.SYNOPSIS
Validates the qemu flavour's M86 trace in a debug-channel capture (design
record 18 section 9; roadmap-hcd.md 36.15 and 36.16).

.DESCRIPTION
The qemu flavour of xhci98.sys writes the requirements filter's lists, the
helper binding's lifecycle and the start's decision as M86 records on the
port-0xE9 debug channel, which QEMU captures to a host file. That channel
writes a byte at a time with no serialisation, so a record can be cut or
interleaved with another writer's bytes. This script is what makes the
capture evidence: every record's layout and checksum are checked, every
list's begin, configuration and descriptor counts are held to its end
record, and a capture with any failure is refused - to be retaken - before
R1, R2 or the L legs are read from it.

A record: "M86 K FFFF SSSS" then up to ten fields of eight hex digits, then
" cXX", XX the low byte of the byte sum of everything before " c". K is B
(list begin: R/T/I, ListSize, lists, walk verdict, failing list, class,
outcome), C (configuration: R/T/I, index << 16 | lists, count, version and
revision), D (descriptor: R/T/I, configuration << 16 | index, option | type
<< 8 | share << 16, flags, the six words of u), E (list end: R/T/I, D
records written, descriptors walked), L (lifecycle: event, a, b) or S (the
start's decision). FFFF is the FDO's serial and SSSS the filter sequence.
The line may carry the channel's "xhci98: " prefix.

Prints the lists it read, one line per descriptor, so the R1/R2 readings
are taken from this output. Exit 0 clean, 1 a refused capture.

.PARAMETER Path
The debugcon capture.

.PARAMETER SelfTest
Runs the script's own cases and exits.
#>
param(
    [string]$Path = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Test-M86Line {
    # Returns $null for a good record, else why it is not.
    param([string]$Line)
    $i = $Line.IndexOf("M86 ")
    if ($i -lt 0) { return "no M86 marker" }
    $rec = $Line.Substring($i).TrimEnd()
    if ($rec -notmatch '^M86 [BCDELS] [0-9A-F]{4} [0-9A-F]{4}( [0-9A-F]{8}){0,10} c[0-9A-F]{2}$') {
        return "layout"
    }
    $body = $rec.Substring(0, $rec.Length - 4)
    $sum = 0
    foreach ($ch in $body.ToCharArray()) { $sum += [int][char]$ch }
    $want = [Convert]::ToInt32($rec.Substring($rec.Length - 2), 16)
    if (($sum -band 0xFF) -ne $want) { return "checksum" }
    return $null
}

function Read-M86 {
    param([string[]]$Lines)
    $records = New-Object System.Collections.ArrayList
    $bad = New-Object System.Collections.ArrayList
    $n = 0
    foreach ($l in $Lines) {
        $n++
        if ($l -notmatch 'M86 ') { continue }
        $why = Test-M86Line $l
        if ($null -ne $why) { [void]$bad.Add("line ${n}: $why : $l"); continue }
        $rec = $l.Substring($l.IndexOf("M86 ")).TrimEnd()
        $parts = $rec.Split(' ')
        $fields = @()
        for ($k = 4; $k -lt $parts.Count - 1; $k++) { $fields += [Convert]::ToUInt32($parts[$k], 16) }
        [void]$records.Add([pscustomobject]@{
            Line = $n; Kind = $parts[1]; Fdo = $parts[2]; Seq = $parts[3]; F = $fields })
    }
    # Every list: B, then C and D records, then an E whose count matches.
    $open = $null
    foreach ($r in $records) {
        switch ($r.Kind) {
            "B" {
                if ($null -ne $open) { [void]$bad.Add("line $($r.Line): a list begins inside the list opened at line $($open.Line)") }
                $open = [pscustomobject]@{ Line = $r.Line; Which = $r.F[0]; D = 0; C = 0; Lists = $r.F[2]; Verdict = $r.F[3] }
            }
            "C" {
                if ($null -eq $open) { [void]$bad.Add("line $($r.Line): a configuration outside a list"); break }
                $open.C++
            }
            "D" {
                if ($null -eq $open) { [void]$bad.Add("line $($r.Line): a descriptor outside a list"); break }
                $open.D++
            }
            "E" {
                if ($null -eq $open) { [void]$bad.Add("line $($r.Line): a list end with no begin"); break }
                if ($r.F[1] -ne $open.D) { [void]$bad.Add("line $($r.Line): the list says $($r.F[1]) descriptors, $($open.D) were read") }
                if ($open.Verdict -eq 0 -and $open.C -ne $open.Lists) { [void]$bad.Add("line $($r.Line): the list says $($open.Lists) configurations, $($open.C) were read") }
                $open = $null
            }
        }
    }
    if ($null -ne $open) { [void]$bad.Add("line $($open.Line): a list with no end (cut)") }
    return [pscustomobject]@{ Records = $records; Bad = $bad }
}

function Show-M86 {
    param($Read)
    foreach ($r in $Read.Records) {
        $which = ""
        if ($r.F.Count -gt 0 -and $r.Kind -in @("B", "C", "D", "E")) { $which = [string][char]$r.F[0] }
        switch ($r.Kind) {
            "B" { "fdo {0} filter {1} list {2}: ListSize {3:X} lists {4} walk {5} class {6} outcome {7}" -f $r.Fdo, $r.Seq, $which, $r.F[1], $r.F[2], $r.F[3], $r.F[5], $r.F[6] }
            "C" { "  configuration {0} of {1}: {2} descriptors" -f ($r.F[1] -shr 16), ($r.F[1] -band 0xFFFF), $r.F[2] }
            "D" {
                $opt = $r.F[2] -band 0xFF; $type = ($r.F[2] -shr 8) -band 0xFF; $share = ($r.F[2] -shr 16) -band 0xFF
                "    [{0}.{1}] option {2:X2} type {3:X2} share {4} flags {5:X4} u {6:X8} {7:X8} {8:X8} {9:X8} {10:X8} {11:X8}" -f `
                    ($r.F[1] -shr 16), ($r.F[1] -band 0xFFFF), $opt, $type, $share, $r.F[3], $r.F[4], $r.F[5], $r.F[6], $r.F[7], $r.F[8], $r.F[9]
            }
            "E" { "  end: {0} descriptors written of {1}" -f $r.F[1], $r.F[2] }
            "L" { "fdo {0} filter {1} lifecycle event {2} a {3:X} b {4:X}" -f $r.Fdo, $r.Seq, $r.F[0], $r.F[1], $r.F[2] }
            "S" { "fdo {0} START mode {1} grant {2} bind {3} class {4} outcome {5} delivery {6} reason {7} status {8:X8}" -f $r.Fdo, $r.F[0], $r.F[1], $r.F[2], $r.F[3], $r.F[4], $r.F[5], $r.F[6], $r.F[7] }
        }
    }
}

function New-M86 {
    param([string]$Body)
    $sum = 0
    foreach ($ch in $Body.ToCharArray()) { $sum += [int][char]$ch }
    return ("xhci98: " + $Body + (" c{0:X2}" -f ($sum -band 0xFF)))
}

if ($SelfTest) {
    $fails = 0
    $clean = @(
        (New-M86 "M86 B 0001 0002 00000052 00000060 00000001 00000000 00000000 00000003 00000000"),
        (New-M86 "M86 C 0001 0002 00000052 00000001 00000001 00010001"),
        (New-M86 "M86 D 0001 0002 00000052 00000000 00000200 00000002 FFFFFFFE FFFFFFFE 00000000 00000000 00000000 00000000"),
        (New-M86 "M86 E 0001 0002 00000052 00000001 00000001"),
        (New-M86 "M86 L 0001 0002 00000001 00000009 00000000")
    )
    $r = Read-M86 $clean
    if ($r.Bad.Count -ne 0) { "FAIL: a clean capture was refused: $($r.Bad -join '; ')"; $fails++ }
    $inter = @($clean[0], $clean[1], ($clean[2].Substring(0, 30) + "Z" + $clean[2].Substring(30)), $clean[3])
    if ((Read-M86 $inter).Bad.Count -eq 0) { "FAIL: an interleaved byte passed"; $fails++ }
    $dropped = @($clean[0], $clean[1], $clean[3])
    if ((Read-M86 $dropped).Bad.Count -eq 0) { "FAIL: a dropped D record passed"; $fails++ }
    $cut = @($clean[0], $clean[1], $clean[2].Substring(0, 50))
    if ((Read-M86 $cut).Bad.Count -eq 0) { "FAIL: a truncated line passed"; $fails++ }
    if ($fails -eq 0) { "check-msi86-trace self-test: 4 cases, all passed"; exit 0 }
    exit 1
}

if ($Path -eq "" -or -not (Test-Path -LiteralPath $Path)) {
    throw "-Path names no capture."
}
$lines = Get-Content -LiteralPath $Path
$read = Read-M86 $lines
Show-M86 $read
if ($read.Bad.Count -gt 0) {
    ""
    "REFUSED: $($read.Bad.Count) bad record(s) - retake the capture:"
    $read.Bad | ForEach-Object { "  $_" }
    exit 1
}
""
"OK: $($read.Records.Count) M86 record(s), every one checked"
exit 0
