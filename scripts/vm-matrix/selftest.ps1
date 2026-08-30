<#
.SYNOPSIS
Task 10.4 - prove the verdict evaluator FAILS when it should, not only that it
passes when things are well.

.DESCRIPTION
"A harness that has never disagreed with a hand-run is untested, not correct."
The same applies one level down: an evaluator that has only ever been fed
healthy readings has not been shown to detect anything.

This drives lib\verdict.ps1 with synthetic deltas - no guest, no boot, a second
to run - and asserts the outcome of each. Every case below is a *negative*
control except the ones explicitly marked as the healthy baseline: the point is
the cases where the answer must NOT be PASS.

The later sections drive the runner's own decisions the same way, through the
functions lib\fresh.ps1 holds for the purpose: the snapshot reader against a
stand-in qemu-img, the target split of the two kinds of run, the writable-image
refusal, the two-leg row loop, the target verdict, the report file, and
prepare-image.ps1's clone and stamp refusals.  The count printed at the end is
the number of checks this file currently makes; nothing else states it.

The complementary test is `-Matrix scripts\vm-matrix\matrix.broken.psd1` against
a real guest, which proves the whole pipeline reports a failure rather than the
evaluator alone.

.EXAMPLE
powershell -File scripts\vm-matrix\selftest.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\monitor.ps1")
. (Join-Path $PSScriptRoot "lib\counters.ps1")
. (Join-Path $PSScriptRoot "lib\verdict.ps1")

$table = Import-CounterTable
$checks = 0
$failures = 0

function Assert {
    param([string]$What, $Expected, $Actual)
    $script:checks++
    if ($Expected -ne $Actual) {
        $script:failures++
        Write-Host ("  FAIL  {0}: expected '{1}', got '{2}'" -f $What, $Expected, $Actual)
    }
}

# Build a delta object of the shape Get-CounterDelta returns, from a label map.
function New-Delta {
    param([hashtable]$ByLabel, [switch]$Restarted)
    $v = @{}
    foreach ($f in $table.Offsets.Keys) { $v[$f] = 0 }
    foreach ($label in $ByLabel.Keys) {
        $v[(Resolve-CounterLabel -Table $table -Label $label)] = $ByLabel[$label]
    }
    return [pscustomobject]@{
        Values = $v
        WentBackwards = $(if ($Restarted) { @("SomeCounter") } else { @() })
        Restarted = [bool]$Restarted
    }
}

function Get-Outcome {
    param([string[]]$Texts, $Delta, [string]$HarnessError = "")
    $results = @()
    foreach ($t in $Texts) {
        $e = ConvertTo-Expectation -Text $t -Table $table
        $results += [pscustomobject]@{ Expectation = $e; Test = (Test-Expectation -Expectation $e -Delta $Delta) }
    }
    return (Get-RowOutcome -Results $results -Delta $Delta -HarnessError $HarnessError -Table $table).Outcome
}

Write-Host "--- parsing: a malformed expectation must be refused at load time ---"
foreach ($bad in @(
    'advance',                                   # no counter
    'advance a counter that does not exist',     # unknown label
    'zero a counter that does not exist',
    'inert devices addressed',                   # inert with no reason
    'wobble devices addressed',                  # unknown verb
    'identity devices addressed'                 # identity with no ==
)) {
    $threw = $false
    try { ConvertTo-Expectation -Text $bad -Table $table | Out-Null } catch { $threw = $true }
    Assert ("refuses '{0}'" -f $bad) $true $threw
}
# ...and a well-formed one must NOT be refused, or the check above proves nothing.
foreach ($good in @(
    'advance devices addressed',
    'advance devices addressed >= 3',
    'zero fatal controller status',
    'inert iso packets answered because no isochronous device is attached in this group',
    'identity endpoint opens seen == endpoint opens accepted + EP0 opens refused'
)) {
    $threw = $false
    try { ConvertTo-Expectation -Text $good -Table $table | Out-Null } catch { $threw = $true }
    Assert ("accepts '{0}'" -f $good) $false $threw
}

Write-Host "--- the healthy baseline: this one MUST pass, or nothing below means anything ---"
$healthy = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoints opened' = 1 }
Assert "healthy row passes" "PASS" (Get-Outcome @(
    'advance devices addressed'
    'advance endpoints opened >= 1'
    'zero fatal controller status'
) (New-Delta $healthy))

Write-Host "--- a counter that did not advance must FAIL, not pass ---"
Assert "no advance -> FAIL" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'advance slots enabled'
) (New-Delta @{ 'devices addressed' = 1 }))

Write-Host "--- a failure-shaped counter that MOVED must FAIL ---"
Assert "nonzero zero-check -> FAIL" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'zero fatal controller status'
) (New-Delta @{ 'devices addressed' = 1; 'fatal controller status' = 1 }))

Write-Host "--- an advance >= N below N must FAIL, and exactly N must pass ---"
Assert "advance >= 3 with 2 -> FAIL" "FAIL" (Get-Outcome @('advance endpoints opened >= 3') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 2 }))
Assert "advance >= 3 with 3 -> PASS" "PASS" (Get-Outcome @('advance endpoints opened >= 3') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 3 }))

Write-Host "--- a broken identity must FAIL ---"
Assert "identity mismatch -> FAIL" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'identity endpoint opens seen == endpoint opens accepted + EP0 opens refused'
) (New-Delta @{ 'devices addressed' = 1; 'endpoint opens seen' = 5; 'endpoint opens accepted' = 3; 'EP0 opens refused' = 1 }))
Assert "identity match -> PASS" "PASS" (Get-Outcome @(
    'advance devices addressed'
    'identity endpoint opens seen == endpoint opens accepted + EP0 opens refused'
) (New-Delta @{ 'devices addressed' = 1; 'endpoint opens seen' = 4; 'endpoint opens accepted' = 3; 'EP0 opens refused' = 1 }))

Write-Host "--- NODRIVER: addressed, never claimed ---"
Assert "addressed but unclaimed -> NODRIVER" "NODRIVER" (Get-Outcome @(
    'advance devices addressed'
    'advance endpoints opened >= 1'
) (New-Delta @{ 'devices addressed' = 1; 'slots enabled' = 1 }))

Write-Host "--- ...but NODRIVER must not swallow a real defect on the same row ---"
# A row that was also never addressed is OUR failure, not the OS's disinterest.
Assert "never addressed -> FAIL not NODRIVER" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'advance endpoints opened >= 1'
) (New-Delta @{}))
# A row that tripped a failure-shaped counter has a defect in it; calling that
# NODRIVER would bury it.
Assert "unclaimed + tripped zero -> FAIL not NODRIVER" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'advance endpoints opened >= 1'
    'zero fatal controller status'
) (New-Delta @{ 'devices addressed' = 1; 'fatal controller status' = 1 }))

Write-Host "--- INERT: an all-inert row can never be a PASS ---"
Assert "all inert -> INERT" "INERT" (Get-Outcome @(
    'inert iso packets answered because no isochronous device is attached in this group'
) (New-Delta @{ 'devices addressed' = 1 }))
Write-Host "--- ...and an inert counter that MOVED is a failure of the claim ---"
Assert "inert moved -> FAIL" "FAIL" (Get-Outcome @(
    'advance devices addressed'
    'inert iso packets answered because no isochronous device is attached in this group'
) (New-Delta @{ 'devices addressed' = 1; 'iso packets answered' = 7 }))

Write-Host "--- ERROR outranks everything ---"
Assert "harness error -> ERROR" "ERROR" (Get-Outcome @('advance devices addressed') (New-Delta $healthy) "the device never attached")
Assert "counter went backwards -> ERROR" "ERROR" (Get-Outcome @('advance devices addressed') (New-Delta $healthy -Restarted))

Write-Host "--- an unread counter is an ERROR, never a zero ---"
# This is the one that matters most: a counter the harness could not read must
# not evaluate as "it did not move", which would turn a broken read into a
# clean-looking pass on every `zero` expectation in the file.
$partial = New-Delta $healthy
$partial.Values.Remove((Resolve-CounterLabel -Table $table -Label 'fatal controller status'))
Assert "unread counter -> ERROR" "ERROR" (Get-Outcome @(
    'advance devices addressed'
    'zero fatal controller status'
) $partial)

Write-Host "--- the four trap guards must THROW, not merely exist ---"
#
# Task 10.3 requires the harness to "fail loudly" on four traps this project has
# already paid for.  Two of them had been demonstrated in the course of ordinary
# use - every screenshot goes through Get-ScreendumpPath, and Test-GuestAlive was
# shown to read Alive=False on a deliberately `stop`ped VM - and two had only
# ever been WRITTEN.  A guard nobody has watched fire is a guard nobody knows
# fires, which is the whole argument of this file one level up.
. (Join-Path $PSScriptRoot "lib\qemu.ps1")

# TRAP 1: two -trace arguments. QEMU keeps the last, so the event list is
# silently discarded and the log reads as "the driver did nothing".
$threw = $false
try {
    Assert-SingleTraceArg -QemuArgs @("-m", "64", "-trace", "events=a", "-trace", "file=b")
} catch { $threw = $true }
Assert "two -trace arguments are refused" $true $threw
# ...and one is not, or the check above proves nothing.
$threw = $false
try { Assert-SingleTraceArg -QemuArgs @("-m", "64", "-trace", "events=a,file=b") } catch { $threw = $true }
Assert "one -trace argument is accepted" $false $threw

# TRAP 2: screendump writes a PPM whatever the extension says.
Assert "screendump path is forced to .ppm" $true ((Get-ScreendumpPath -Path "x\y.png") -like "*.ppm")
Assert "an already-.ppm path is left alone" $true ((Get-ScreendumpPath -Path "x\y.ppm") -like "*.ppm")

# TRAP 3: a stale offsets.txt. This is the one that surfaces as a WRONG VALUE
# and never as an error, so it is the one that most needs to throw.
$tmpOff = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-selftest-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".txt")
Set-Content -LiteralPath $tmpOff -Value @("SIZEOF 12345", "SomeField 4") -Encoding ascii
try {
    $threw = $false
    try { Assert-OffsetsFresh -OffsetsFile $tmpOff -ExtensionSizeFromTrace 99999 | Out-Null } catch { $threw = $true }
    Assert "a SIZEOF mismatch voids the run" $true $threw
    $threw = $false
    try { Assert-OffsetsFresh -OffsetsFile $tmpOff -ExtensionSizeFromTrace 12345 | Out-Null } catch { $threw = $true }
    Assert "a matching SIZEOF is accepted" $false $threw
    # A table with no SIZEOF cannot be checked at all, which must also throw
    # rather than pass by default.
    Set-Content -LiteralPath $tmpOff -Value @("SomeField 4") -Encoding ascii
    $threw = $false
    try { Assert-OffsetsFresh -OffsetsFile $tmpOff -ExtensionSizeFromTrace 12345 | Out-Null } catch { $threw = $true }
    Assert "an offsets file with no SIZEOF is refused" $true $threw
} finally {
    Remove-Item -LiteralPath $tmpOff -Force -ErrorAction SilentlyContinue
}

# TRAP 4 (liveness) needs a running guest and was demonstrated against a
# `stop`ped VM: Alive=False with irq delta 0, Alive=True after `cont` (recorded
# in README.md, traps list, item 11). It is named here so the set of four is
# visibly complete rather than three-plus-a-gap.

Write-Host "--- the fifth guard: a read that could not happen must name the RIGHT cause ---"
#
# The short-reply guard fired on the 2a `audio` group and reported "asked for 32
# words and got 0" for a run whose QEMU process had ENDED - a sentence about a
# busy guest, written for a guest that no longer existed. A right refusal with
# the wrong reason sends the reader looking in the wrong place, and here it sent
# a whole result box after a device driver that had never been bound. So the two
# causes are now separated, and both messages are asserted: a guard that can only
# say one thing is the defect this pair exists to prevent.
# `0xC1468870` written as a literal is a SIGNED Int32 in Windows PowerShell 5.1
# and arrives as -1052342160, which is the same unsigned-arithmetic trap the
# counter reader's own comment names as its reason for mapping replies
# positionally. A kernel VA has to be converted, not written.
$exampleVa = [Convert]::ToUInt32("C1468870", 16)
$dead = New-CounterReadFailure -Addr $exampleVa -Take 32 -Got 0 -Attempts 4 -MonitorListening $false `
                               -ProcessState (Get-ProcessStateText -Process $null)
Assert "an absent monitor is named as such"          $true  ($dead -match 'nothing is listening')
Assert "...and is NOT reported as a short reply"     $false ($dead -match 'asked for')
$short = New-CounterReadFailure -Addr $exampleVa -Take 32 -Got 7 -Attempts 4 -MonitorListening $true
Assert "a genuine short reply still voids the run"   $true  ($short -match 'void rather than approximate')
Assert "...and says the monitor was answering"       $true  ($short -match 'still answering')

# The process handle is what turns "the monitor is gone" into "and here is why",
# so its three states are asserted rather than assumed. A handle that was never
# passed must say so instead of implying the process is alive.
Assert "a missing handle admits it"        $true ((Get-ProcessStateText -Process $null) -match 'not established')
Assert "a live handle says still running"  $true ((Get-ProcessStateText -Process (Get-Process -Id $PID)) -match 'still running')

# End to end, with no guest at all: point Read-Counters at a port nothing is
# listening on - which IS the failing condition - and require the message to
# name the dead monitor. The unit checks above cannot catch a reader that
# composes the right message and never reaches it.
$freePort = 55598
if (Test-MonitorPortFree -Port $freePort) {
    $msg = ""
    try {
        Read-Counters -Port $freePort -BaseVa "0xC1000000" -Table $table | Out-Null
    } catch {
        $msg = $_.Exception.Message
    }
    Reset-MonitorErrors
    Assert "a read with no guest throws"              $true ($msg -ne "")
    Assert "...naming the absent monitor, not a short read" $true ($msg -match 'nothing is listening')
} else {
    Write-Host ("  (skipped the live check: something is listening on {0})" -f $freePort)
}

Write-Host "--- ...and the liveness probe had the same blindness, in a second place ---"
#
# Found by killing QEMU mid-row to demonstrate the fix above: with the process
# gone, `Test-GuestAlive` returned Alive=$false from an empty status and zero
# parseable lines, and the runner printed "the guest stopped executing while
# this device was attached" - a claim about a guest, for a run that no longer
# had one. The probe's own comment already said an unparseable reply is "an
# unknown, not a dead guest"; the return value did not keep that rule.
if (Test-MonitorPortFree -Port $freePort) {
    $probe = Test-GuestAlive -Port $freePort -Process $null
    Assert "an absent monitor is 'unreachable'"       "unreachable" $probe.Verdict
    Assert "...and is still not Alive"                $false        $probe.Alive
    Assert "...and does not claim a guest stopped"    $false        ($probe.Why -match 'executing and taking|took no timer')
    Assert "...and says nothing was probed"           $true         ($probe.Detail -match 'no probe was taken')
}

Write-Host "--- MayWedgeGuest must be read, and a typo in it must not pass ---"
#
# It was set on the audio row and read by NOTHING for the life of the harness,
# so when that group did end early the report could not say whether the matrix
# had predicted it. A declaration nothing reads is not a declaration.
$wedgeRow = @{ Name = 'usb-audio/fs'; MayWedgeGuest = @('2a') }
Assert "a declared target is recognised"   $true  (Test-RowMayWedge -Row $wedgeRow -TargetId '2a')
Assert "an undeclared target is not"       $false (Test-RowMayWedge -Row $wedgeRow -TargetId '2b')
Assert "a row with no declaration is not"  $false (Test-RowMayWedge -Row @{ Name = 'x' } -TargetId '2a')
Assert "a real target validates clean"     0 (@(Get-RowWedgeProblems -Row $wedgeRow -TargetIds @('2a','2b')).Count)
Assert "a target that does not exist is a problem" 1 (@(Get-RowWedgeProblems -Row @{ Name = 'x'; MayWedgeGuest = @('2c') } -TargetIds @('2a','2b')).Count)

Write-Host "--- stage G's teardown scanner must answer NO as readily as YES ---"
#
# Find-Teardown is the whole oracle for batch 11-V stage G's stop clause, and
# the two ways it could quietly lie are symmetrical: reporting a teardown where
# there was none (a wedge or a swallowed keypress read as a clean shutdown), and
# missing the armed-callback witnesses that are the clause itself. Both are
# fixtures here, because on the run they are one boot each.
$fixture = Join-Path $env:TEMP ("xhci98-teardown-{0}.log" -f $PID)
Set-Content -LiteralPath $fixture -Encoding ascii -Value @(
    'xhci98: DriverEntry',
    'xhci98: cb StartController irql=00 a=C14658C4 b=C1465334 c=00000000',
    'xhci98: command: abandoned outstanding TRB=00000000',
    'xhci98: RH operations retired by the quiesce=00000000',
    'xhci98: cb SuspendController irql=00 a=C14658C4 b=00000000 c=00000000',
    'xhci98: command: abandoned outstanding TRB=0FE3A100',
    'xhci98: root hub: port operations retired by the quiesce=00000002',
    'xhci98: cb DisableInterrupts irql=02 a=C14658C4 b=00000000 c=00000000',
    'xhci98: cb StopController irql=00 a=C14658C4 b=00000001 c=00000000',
    'xhci98: teardown: the stop arrived on a suspended controller - PORTSC is unwritable, so port power stays up')
$t = Find-Teardown -Path $fixture
Assert "the measured shape is read in order" "SuspendController -> DisableInterrupts -> StopController" `
       (($t.Sequence | Where-Object { $_ -ne 'StartController' }) -join " -> ")
Assert "the stop is seen"                            $true  $t.HasStop
Assert "...at irql 00"                               "00"   ($t.StopIrqls -join ",")
Assert "the suspended-stop branch is seen"           $true  $t.SuspendedStop
Assert "a NONZERO abandoned-TRB line is a witness"   1      $t.CommandsTrace.Count
Assert "...and the zero counter row is not"          0      @($t.CommandsTrace | Where-Object { $_ -match '=00000000' }).Count
Assert "the quiesce's retired-ports line is a witness" 1    $t.RhRetiredTrace.Count
# The counter row 'RH operations retired by the quiesce=...' is worded almost
# identically to the trace line and must NOT be counted as one - the run sheet's
# whole point about that witness is that a counter and a trace line are two
# channels, not one read twice.
Assert "...and the counter row of the same name is not" 0 @($t.RhRetiredTrace | Where-Object { $_ -match '^xhci98: RH operations' }).Count

# -From is what separates this teardown from an idle suspend earlier in the log.
$t2 = Find-Teardown -Path $fixture -From 9
Assert "a mark past the teardown finds no stop"      $false $t2.HasStop
Assert "...and no suspend"                           $false $t2.HasSuspend

# The negative control: a log that ends with the driver still up. This is the
# shape a swallowed keypress leaves, and it must not read as a shutdown.
Set-Content -LiteralPath $fixture -Encoding ascii -Value @(
    'xhci98: DriverEntry',
    'xhci98: cb StartController irql=00 a=C14658C4 b=C1465334 c=00000000',
    'xhci98: transfers submitted=0001E240')
$t3 = Find-Teardown -Path $fixture
Assert "a live guest is not a teardown"              $false $t3.HasStop
Assert "...nor a suspend"                            $false $t3.HasSuspend
Assert "...and its one load is counted"              1      $t3.DriverEntries
Remove-Item -LiteralPath $fixture -Force

# An absent log is an absent log, not an absent teardown: the stage script
# throws on it long before this, but a scanner that returns "no stop" for a file
# that does not exist would make that throw removable.
$gone = Find-Teardown -Path (Join-Path $env:TEMP "xhci98-no-such-file.log")
Assert "a missing log reads as zero lines"           0      $gone.Total

Write-Host "--- the post-release run's refusals must fire, guestless (design record 09, sections 3.3 and 6) ---"
#
# Four refusals about an image, each checkable from its snapshot list alone,
# and one about a target's inherited keys.  Every one is a case here because
# a refusal nobody has watched fire is the same untested guard as trap 4.
. (Join-Path $PSScriptRoot "lib\fresh.ps1")

# The list parser, fed the exact shape qemu-img 11 prints.
$snapText = @"
Snapshot list:
ID      TAG               VM_SIZE                DATE        VM_CLOCK     ICOUNT
1       post-nusb             0 B 2026-07-22 23:59:13  0000:00:00.000          0
2       base-1.0.0.0-qemu     0 B 2026-08-30 10:00:00  0000:00:00.000          0
"@
$snaps = @(ConvertFrom-SnapshotList -Text $snapText)
Assert "two snapshots are parsed"              2                   $snaps.Count
Assert "...in creation order"                  "post-nusb"         $snaps[0].Tag
Assert "...with the newest last"               "base-1.0.0.0-qemu" $snaps[1].Tag
Assert "an empty listing parses to nothing"    0                   @(ConvertFrom-SnapshotList -Text "Snapshot list:`nID TAG VM_SIZE DATE VM_CLOCK ICOUNT").Count

# The stamp's name round-trips through its parser.
$stamp = ConvertFrom-BaseStampName -Tag (Get-BaseStampName -Version "1.0.0.0" -Flavour "qemu")
Assert "the stamp names its version"           "1.0.0.0" $stamp.Version
Assert "...and its flavour"                    "qemu"    $stamp.Flavour
Assert "a non-stamp tag parses to null"        $true     ($null -eq (ConvertFrom-BaseStampName -Tag "pre-phase10-prep-2026-08-11"))

function New-Snap { param([string[]]$Tags) $i = 0; return @($Tags | ForEach-Object { $i++; [pscustomobject]@{ Id = $i; Tag = $_; Date = "2026-08-30 10:00:00" } }) }
$ok = New-Snap @('base-1.0.0.0-qemu')
Assert "a stamped image passes"                0 @(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots $ok -Version '1.0.0.0').Count
Assert "no stamp at all is refused"            1 @(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots (New-Snap @('post-nusb')) -Version '1.0.0.0').Count
Assert "an EMPTY snapshot list is refused"     1 @(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots @() -Version '1.0.0.0').Count
Assert "another release's stamp is refused"    $true (@(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots (New-Snap @('base-0.0.0.6-qemu')) -Version '1.0.0.0') -join ' ' -match 'prepared for 0.0.0.6')
Assert "the debug flavour's stamp is refused"  $true (@(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots (New-Snap @('base-1.0.0.0-debug')) -Version '1.0.0.0') -join ' ' -match 'debug flavour')
Assert "a stamp that is not newest is refused" $true (@(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots (New-Snap @('base-1.0.0.0-qemu', 'pre-something')) -Version '1.0.0.0') -join ' ' -match 'newest snapshot')
Assert "...and a re-stamp after it passes"     0 @(Get-FreshImageProblems -ImagePath 'vm\fresh-2a.img' -Snapshots (New-Snap @('base-1.0.0.0-qemu', 'pre-something', 'base-1.0.0.0-qemu')) -Version '1.0.0.0').Count
# Phase 10's images are refused BY NAME, whatever their snapshots say - a stamp
# on one of them would be exactly the mistake the name check exists to catch.
Assert "win98.img is refused even when stamped" $true (@(Get-FreshImageProblems -ImagePath 'vm\win98.img' -Snapshots $ok -Version '1.0.0.0') -join ' ' -match 'carried-along')
Assert "win2k.img is refused even when stamped" $true (@(Get-FreshImageProblems -ImagePath 'D:\somewhere\WIN2K.IMG' -Snapshots $ok -Version '1.0.0.0') -join ' ' -match 'carried-along')

# The version under test comes from the single source, and a header with no
# XHCI_VER_STR must throw rather than default.
$vut = Get-DriverVersionUnderTest -RepoRoot (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Assert "the version under test is four-part"   $true ($vut -match '^\d+\.\d+\.\d+\.\d+$')
$fakeRepo = Join-Path $env:TEMP ("xhci98-selftest-repo-{0}" -f $PID)
New-Item -ItemType Directory -Path (Join-Path $fakeRepo "src") -Force | Out-Null
Set-Content -LiteralPath (Join-Path $fakeRepo "src\xhci_version.h") -Value @('#define XHCI_VER_CSV 1,0,0,0') -Encoding ascii
$threw = $false
try { Get-DriverVersionUnderTest -RepoRoot $fakeRepo | Out-Null } catch { $threw = $true }
Assert "a header with no XHCI_VER_STR throws"  $true $threw
Remove-Item -LiteralPath $fakeRepo -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "--- a fresh target inherits its Phase 10 target's entries through Like ---"
$fresh = @{ Id = '2a-fresh'; Like = '2a'; CloneFrom = @{ Image = 'win98.img'; Snapshot = 'post-nusb' } }
$plain = @{ Id = '2a' }
Assert "a fresh target is recognised"          $true  (Test-FreshTarget -Target $fresh)
Assert "a Phase 10 target is not"              $false (Test-FreshTarget -Target $plain)
Assert "its keys are its id then its Like"     "2a-fresh,2a" ((Get-TargetKeys -Target $fresh) -join ",")
Assert "a 2a entry applies to 2a-fresh"        "2a"       (Find-TargetKey -Table @{ '2a' = 'x' } -Target $fresh)
Assert "...and an own-id entry wins"           "2a-fresh" (Find-TargetKey -Table @{ '2a' = 'x'; '2a-fresh' = 'y' } -Target $fresh)
Assert "a 2b entry does not apply"             $true ($null -eq (Find-TargetKey -Table @{ '2b' = 'x' } -Target $fresh))
Assert "MayWedgeGuest = @('2a') covers 2a-fresh" $true (Test-TargetInList -List @('2a') -Target $fresh)

Write-Host "--- ExpectNoDriver: read for the right target, and a typo is a problem ---"
$ndRow = @{ Name = 'usb-net/fs'; ExpectNoDriver = @{ '2a' = 'no class driver' } }
Assert "the reason is read for 2a-fresh"       "no class driver" (Get-RowNoDriverReason -Row $ndRow -Target $fresh)
Assert "...and not for 2b"                     $true ($null -eq (Get-RowNoDriverReason -Row $ndRow -Target @{ Id = '2b' }))
Assert "a row without the key reads null"      $true ($null -eq (Get-RowNoDriverReason -Row @{ Name = 'x' } -Target $fresh))
Assert "a known key validates clean"           0 @(Get-RowNoDriverProblems -Row $ndRow -KnownKeys @('2a', '2b', '2a-fresh')).Count
Assert "an unknown key is a problem"           1 @(Get-RowNoDriverProblems -Row @{ Name = 'x'; ExpectNoDriver = @{ '2c' = 'r' } } -KnownKeys @('2a', '2b')).Count
Assert "an empty reason is a problem"          1 @(Get-RowNoDriverProblems -Row @{ Name = 'x'; ExpectNoDriver = @{ '2a' = '' } } -KnownKeys @('2a', '2b')).Count

Write-Host "--- the replug leg: same outcome twice, or a FAIL ---"
Assert "PASS then PASS is PASS"                "PASS"     (Get-ReplugOutcome -First "PASS" -Second "PASS").Outcome
Assert "NODRIVER twice is NODRIVER"            "NODRIVER" (Get-ReplugOutcome -First "NODRIVER" -Second "NODRIVER").Outcome
Assert "PASS then NODRIVER is FAIL"            "FAIL"     (Get-ReplugOutcome -First "PASS" -Second "NODRIVER").Outcome
Assert "NODRIVER then PASS is FAIL"            "FAIL"     (Get-ReplugOutcome -First "NODRIVER" -Second "PASS").Outcome
Assert "a FAIL on either leg is FAIL"          "FAIL"     (Get-ReplugOutcome -First "PASS" -Second "FAIL").Outcome
Assert "an ERROR on either leg is ERROR"       "ERROR"    (Get-ReplugOutcome -First "ERROR" -Second "PASS").Outcome
Assert "...and outranks a FAIL"                "ERROR"    (Get-ReplugOutcome -First "FAIL" -Second "ERROR").Outcome

Write-Host "--- what counts against a target's verdict ---"
Assert "FAIL counts"                                   $true  (Test-RowCountsAgainst -Outcome "FAIL")
Assert "PASS does not"                                 $false (Test-RowCountsAgainst -Outcome "PASS")
Assert "EXCLUDED does not"                             $false (Test-RowCountsAgainst -Outcome "EXCLUDED")
Assert "an unexpected NODRIVER counts"                 $true  (Test-RowCountsAgainst -Outcome "NODRIVER")
Assert "an expected NODRIVER does not"                 $false (Test-RowCountsAgainst -Outcome "NODRIVER" -NoDriverExpected $true)
Assert "an undeclared wedge (ERROR) counts"            $true  (Test-RowCountsAgainst -Outcome "ERROR")
Assert "a declared wedge (the pinned reading) does not" $false (Test-RowCountsAgainst -Outcome "ERROR" -WedgeDeclared $true)

Write-Host "--- the header carries every variable thing, and nothing else does ---"
$hdr = New-PostReleaseHeader -TargetId '2a-fresh' -Version '1.0.0.0' -DriverLine '1.0.0.0 qemu, 1 B, sha256 0' -ImageLine 'vm\fresh-2a.img, stamp base-1.0.0.0-qemu, from win98.img post-nusb' `
           -QemuVersion '11.0.0' -Accel 'tcg' -Sizeof 12345 -Counters 7 -Started (Get-Date '2026-08-30 10:00:00') -Elapsed ([timespan]::FromMinutes(61)) `
           -Verdict 'PASS' -Rows 20 -NoDriverExpected 4 -NotReached 3
Assert "the verdict line is as designed"       $true (($hdr -join "`n") -match '# verdict:\s+2a-fresh PASS, 20 rows, 4 NODRIVER expected, 3 not reached')
Assert "the elapsed time is h:mm:ss"           $true (($hdr -join "`n") -match 'elapsed 1:01:00')
Assert "every header line is a comment"        0 @($hdr | Where-Object { -not $_.StartsWith('#') }).Count

Write-Host "--- the snapshot reader must fail on qemu-img's exit code, not read silence as 'no snapshots' ---"
#
# A source image held by a running QEMU, a missing DLL and a corrupt qcow2 all
# leave qemu-img with an empty listing and a nonzero exit, and the run read the
# empty listing as "carries no base- stamp" (repo audit S-5).  A stand-in
# qemu-img here plays both parts: one prints a listing and exits 0, the other
# prints the lock refusal on stderr and exits 1.
$fakeDir = Join-Path $env:TEMP ("xhci98-selftest-qemuimg-{0}" -f $PID)
New-Item -ItemType Directory -Path $fakeDir -Force | Out-Null
$fakeImage = Join-Path $fakeDir "fresh.img"
Set-Content -LiteralPath $fakeImage -Value "not a qcow2" -Encoding ascii
$goodImg = Join-Path $fakeDir "qemu-img-good.cmd"
Set-Content -LiteralPath $goodImg -Encoding ascii -Value @(
    '@echo off',
    'echo Snapshot list:',
    'echo ID      TAG               VM_SIZE                DATE        VM_CLOCK     ICOUNT',
    'echo 1       post-nusb             0 B 2026-07-22 23:59:13  0000:00:00.000          0',
    'exit /b 0')
$badImg = Join-Path $fakeDir "qemu-img-bad.cmd"
Set-Content -LiteralPath $badImg -Encoding ascii -Value @(
    '@echo off',
    'echo qemu-img: Could not open the image: Failed to get shared "write" lock 1>&2',
    'exit /b 1')
try {
    $got = @(Get-ImageSnapshots -QemuImg $goodImg -Image $fakeImage)
    Assert "a listing with exit 0 is parsed"          1           $got.Count
    Assert "...to its tag"                            "post-nusb" $got[0].Tag
    $msg = ""
    try { Get-ImageSnapshots -QemuImg $badImg -Image $fakeImage | Out-Null } catch { $msg = $_.Exception.Message }
    Assert "a nonzero exit throws"                    $true ($msg -ne "")
    Assert "...naming the exit code"                  $true ($msg -match 'exit 1')
    Assert "...and carrying qemu-img's own reason"    $true ($msg -match 'shared "write" lock')
    $msg = ""
    try { Get-ImageSnapshots -QemuImg $goodImg -Image (Join-Path $fakeDir "absent.img") | Out-Null } catch { $msg = $_.Exception.Message }
    Assert "a missing image throws before qemu-img runs" $true ($msg -match 'image not found')
} finally {
    Remove-Item -LiteralPath $fakeDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "--- the two kinds of run boot disjoint target sets, and say so ---"
$plainA = @{ Id = '2a'; Image = 'win98.img' }
$plainB = @{ Id = '2b'; Image = 'win2k.img' }
$freshA = @{ Id = '2a-fresh'; Like = '2a'; Image = 'fresh-2a.img'; CloneFrom = @{ Image = 'win98.img'; Snapshot = 'post-nusb' } }
$allTargets = @($plainA, $plainB, $freshA)
Assert "the ordinary matrix boots the Phase 10 targets" "2a,2b"    ((@(Select-RunTargets -Targets $allTargets -PostRelease $false) | ForEach-Object { $_.Id }) -join ",")
Assert "the post-release run boots the fresh ones"      "2a-fresh" ((@(Select-RunTargets -Targets $allTargets -PostRelease $true) | ForEach-Object { $_.Id }) -join ",")
Assert "-Target narrows within the pool"                "2b"       ((@(Select-RunTargets -Targets $allTargets -PostRelease $false -Requested @('2b')) | ForEach-Object { $_.Id }) -join ",")
Assert "an empty -Target entry is ignored"              "2a,2b"    ((@(Select-RunTargets -Targets $allTargets -PostRelease $false -Requested @('')) | ForEach-Object { $_.Id }) -join ",")
$msg = ""; try { Select-RunTargets -Targets $allTargets -PostRelease $false -Requested @('2a-fresh') | Out-Null } catch { $msg = $_.Exception.Message }
Assert "a fresh target named to the ordinary matrix is refused" $true ($msg -match 'only -PostRelease boots' -and $msg -match 'Pass -PostRelease')
$msg = ""; try { Select-RunTargets -Targets $allTargets -PostRelease $true -Requested @('2a') | Out-Null } catch { $msg = $_.Exception.Message }
Assert "a Phase 10 target named to the post-release run is refused" $true ($msg -match "Phase 10's images")
$msg = ""; try { Select-RunTargets -Targets $allTargets -PostRelease $false -Requested @('2c') | Out-Null } catch { $msg = $_.Exception.Message }
Assert "an unknown target names the pool"               $true ($msg -match 'matched no target' -and $msg -match '2a, 2b')
$msg = ""; try { Select-RunTargets -Targets @($plainA, $plainB) -PostRelease $true | Out-Null } catch { $msg = $_.Exception.Message }
Assert "a config with no fresh target says how to add one" $true ($msg -match 'no fresh \(CloneFrom\) targets' -and $msg -match 'config.sample.psd1')

Write-Host "--- a config that turns Snapshot off is refused by the post-release run ---"
Assert "Snapshot = `$false is a problem"        $true  ($null -ne (Get-SnapshotOffProblem -Config @{ Snapshot = $false } -TargetId '2a-fresh'))
Assert "...naming the target and the rule"     $true  ((Get-SnapshotOffProblem -Config @{ Snapshot = $false } -TargetId '2a-fresh') -match '2a-fresh.*never writes to the image')
Assert "Snapshot = `$true is not"               $true  ($null -eq (Get-SnapshotOffProblem -Config @{ Snapshot = $true } -TargetId '2a-fresh'))
Assert "an absent key is not"                  $true  ($null -eq (Get-SnapshotOffProblem -Config @{} -TargetId '2a-fresh'))

Write-Host "--- the two-leg loop: a leg without a reading ends the row, and the second leg is never taken ---"
function New-LegReading { param([bool]$Read) if ($Read) { return [pscustomobject]@{ After = @{}; Before = @{}; Error = "" } } else { return [pscustomobject]@{ After = $null; Before = @{}; Error = "the device never appeared" } } }
$script:legCalls = @(); $script:legErrors = @(); $script:judged = @()
$r = Invoke-RowLegs -RowName 'usb-storage/hs' -LegCount 2 `
        -RunLeg { param($Leg, $LegName) $script:legCalls += $LegName; return (New-LegReading -Read ($Leg -ne 1)) } `
        -JudgeLeg { param($Leg, $LegName, $LegResult) $script:judged += $LegName; return "PASS" } `
        -OnLegError { param($Leg, $LegName, $LegResult) $script:legErrors += ("{0}:{1}" -f $LegName, $LegResult.Error) }
Assert "leg 1 without a reading runs no leg 2"     "usb-storage/hs"                        ($script:legCalls -join ",")
Assert "...its error handler ran once, with the reason" "usb-storage/hs:the device never appeared" ($script:legErrors -join ",")
Assert "...nothing was judged"                     0                                       $script:judged.Count
Assert "...and the row is ERROR"                   "ERROR"                                 $r.Outcome
Assert "...because a reading was not taken"        $true                                   ($r.Why -match 'reading was not taken')
Assert "...with one leg outcome recorded"          "ERROR"                                 ($r.LegOutcomes -join ",")

$script:legCalls = @(); $script:judged = @()
$r = Invoke-RowLegs -RowName 'usb-kbd/hs' -LegCount 2 `
        -RunLeg { param($Leg, $LegName) $script:legCalls += $LegName; return (New-LegReading -Read $true) } `
        -JudgeLeg { param($Leg, $LegName, $LegResult) $script:judged += $LegName; return "PASS" }
Assert "two good legs are both taken"              "usb-kbd/hs,usb-kbd/hs/replug"          ($script:legCalls -join ",")
Assert "...and both judged"                        "usb-kbd/hs,usb-kbd/hs/replug"          ($script:judged -join ",")
Assert "...PASS twice is PASS"                     "PASS"                                  $r.Outcome
Assert "...with nothing to explain"                ""                                      $r.Why

$r = Invoke-RowLegs -RowName 'usb-net/fs' -LegCount 2 `
        -RunLeg { param($Leg, $LegName) return (New-LegReading -Read $true) } `
        -JudgeLeg { param($Leg, $LegName, $LegResult) if ($Leg -eq 1) { return "PASS" } else { return "NODRIVER" } }
Assert "a replug that came back differently is FAIL" "FAIL"   $r.Outcome
Assert "...and the reason names both outcomes"       $true    ($r.Why -match 'replug reached NODRIVER where the first attach reached PASS')

$r = Invoke-RowLegs -RowName 'usb-net/fs' -LegCount 2 `
        -RunLeg { param($Leg, $LegName) return (New-LegReading -Read ($Leg -ne 2)) } `
        -JudgeLeg { param($Leg, $LegName, $LegResult) return "PASS" }
Assert "a leg 2 without a reading is ERROR"          "ERROR"  $r.Outcome
Assert "...after a judged leg 1"                     "PASS,ERROR" ($r.LegOutcomes -join ",")

$script:legCalls = @()
$r = Invoke-RowLegs -RowName 'usb-mouse/fs' -LegCount 1 `
        -RunLeg { param($Leg, $LegName) $script:legCalls += $LegName; return (New-LegReading -Read $true) } `
        -JudgeLeg { param($Leg, $LegName, $LegResult) return "NODRIVER" }
Assert "the ordinary matrix takes one leg"           "usb-mouse/fs" ($script:legCalls -join ",")
Assert "...whose outcome is the row's"               "NODRIVER"     $r.Outcome
Assert "...with no replug reasoning"                 ""             $r.Why

Write-Host "--- the target verdict: no rows is a FAIL, not an empty pass ---"
Assert "zero rows is FAIL"                     "FAIL" (Get-TargetVerdict -Tally @{ Rows = 0; Against = 0 })
Assert "a row against the target is FAIL"      "FAIL" (Get-TargetVerdict -Tally @{ Rows = 3; Against = 1 })
Assert "rows with nothing against is PASS"     "PASS" (Get-TargetVerdict -Tally @{ Rows = 3; Against = 0 })

Write-Host "--- the report file: header, column line, then this target's rows ---"
$reportPath = Join-Path $env:TEMP ("xhci98-selftest-report-{0}.txt" -f $PID)
try {
    $written = Write-PostReleaseReport -Path $reportPath -Header @('# post-release run', '# verdict:   2a-fresh PASS') -ColumnLine 'TARGET ROW' -Body @('2a-fresh usb-kbd/hs PASS', '2a-fresh usb-kbd/hs/replug PASS')
    $back = @(Get-Content -LiteralPath $reportPath)
    Assert "the path written is returned"        $reportPath $written
    Assert "five lines come back"                5           $back.Count
    Assert "the header leads"                    "# post-release run" $back[0]
    Assert "the column line follows the header"  "TARGET ROW" $back[2]
    Assert "the rows follow the column line"     "2a-fresh usb-kbd/hs PASS" $back[3]
    Assert "...in order"                         "2a-fresh usb-kbd/hs/replug PASS" $back[4]
    Write-PostReleaseReport -Path $reportPath -Header @('# h') -ColumnLine 'TARGET ROW' -Body @() | Out-Null
    Assert "an empty body still writes the header and columns" 2 @(Get-Content -LiteralPath $reportPath).Count
} finally {
    Remove-Item -LiteralPath $reportPath -Force -ErrorAction SilentlyContinue
}

Write-Host "--- prepare-image -Clone: the source, its snapshot, and an existing destination ---"
$srcSnaps = New-Snap @('phase2b-clean', 'post-nusb')
Assert "a good clone has no problems"          0 @(Get-CloneProblems -Source 'vm\win98.img' -Tag 'post-nusb' -SourceExists $true -SourceSnapshots $srcSnaps -Destination 'vm\fresh-2a.img' -DestinationExists $false).Count
Assert "a missing source is refused"           $true (@(Get-CloneProblems -Source 'vm\win98.img' -Tag 'post-nusb' -SourceExists $false -SourceSnapshots @() -Destination 'vm\fresh-2a.img' -DestinationExists $false) -join ' ' -match 'clone source not found')
$msgs = @(Get-CloneProblems -Source 'vm\win98.img' -Tag 'no-such' -SourceExists $true -SourceSnapshots $srcSnaps -Destination 'vm\fresh-2a.img' -DestinationExists $false) -join ' '
Assert "a missing snapshot is refused"         $true ($msgs -match "no snapshot named 'no-such'")
Assert "...and the ones it has are listed"     $true ($msgs -match 'phase2b-clean, post-nusb')
Assert "an existing destination is refused"    $true (@(Get-CloneProblems -Source 'vm\win98.img' -Tag 'post-nusb' -SourceExists $true -SourceSnapshots $srcSnaps -Destination 'vm\fresh-2a.img' -DestinationExists $true) -join ' ' -match 'already exists.*-FreshCopy')
Assert "...unless -FreshCopy says so"          0 @(Get-CloneProblems -Source 'vm\win98.img' -Tag 'post-nusb' -SourceExists $true -SourceSnapshots $srcSnaps -Destination 'vm\fresh-2a.img' -DestinationExists $true -FreshCopy $true).Count

Write-Host "--- prepare-image -Stamp: refused while the guest is up, on a missing image, on the wrong file, and without a witness ---"
$stampOk = @{ Port = 56694; PortFree = $true; Image = 'D:\vm\fresh-2a.img'; ImageExists = $true; DebugconLog = 'out\prep.log'; IdentSize = 12345; TableSizeof = 12345 }
Assert "a witnessed install on the booted file stamps" 0 @(Get-StampProblems @stampOk).Count
Assert "...also when the paths file names that same file" 0 @(Get-StampProblems @stampOk -BootedImage 'D:\vm\fresh-2a.img').Count
$stampBusy = $stampOk.Clone(); $stampBusy.PortFree = $false
Assert "a listening prep guest refuses the stamp"      $true (@(Get-StampProblems @stampBusy) -join ' ' -match 'still listening on 56694')
$stampGone = $stampOk.Clone(); $stampGone.ImageExists = $false
Assert "a missing fresh image refuses the stamp"       $true (@(Get-StampProblems @stampGone) -join ' ' -match 'fresh image not found.*-Clone')
$msgs = @(Get-StampProblems @stampOk -BootedImage 'C:\work\fresh-2a.img') -join ' '
Assert "a stamp on a file the last boot did not run is refused" $true ($msgs -match 'ran C:\\work\\fresh-2a.img, not D:\\vm\\fresh-2a.img')
Assert "...and says to run -CopyBack first"           $true ($msgs -match '-CopyBack')

Write-Host "--- prepare-image -CopyBack: the work copy goes back and the stamp then passes ---"
$cbOk = @{ Port = 56694; PortFree = $true; Image = 'D:\vm\fresh-2a.img'; BootedImage = 'C:\work\fresh-2a.img'; BootedExists = $true }
Assert "a shut-down work copy can be copied back"      0 @(Get-CopyBackProblems @cbOk).Count
$cbBusy = $cbOk.Clone(); $cbBusy.PortFree = $false
Assert "a listening prep guest refuses the copy back"  $true (@(Get-CopyBackProblems @cbBusy) -join ' ' -match 'still listening on 56694')
$cbNone = $cbOk.Clone(); $cbNone.BootedImage = ''
Assert "no recorded boot refuses the copy back"        $true (@(Get-CopyBackProblems @cbNone) -join ' ' -match 'no prep boot is recorded')
$cbSame = $cbOk.Clone(); $cbSame.BootedImage = 'D:\vm\fresh-2a.img'
Assert "a boot that ran the vm-dir file has nothing to copy back" $true (@(Get-CopyBackProblems @cbSame) -join ' ' -match 'nothing to copy back')
$cbGone = $cbOk.Clone(); $cbGone.BootedExists = $false
Assert "a missing work copy refuses the copy back"     $true (@(Get-CopyBackProblems @cbGone) -join ' ' -match 'work copy the last boot ran is gone')
$cbDir = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-selftest-copyback-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $cbDir -Force | Out-Null
try {
    $cbWork = Join-Path $cbDir 'work.img'; $cbVm = Join-Path $cbDir 'vm.img'; $cbPaths = Join-Path $cbDir 'paths.txt'
    Set-Content -LiteralPath $cbWork -Value 'installed' -Encoding ascii
    Set-Content -LiteralPath $cbVm -Value 'pre-install' -Encoding ascii
    Set-Content -LiteralPath $cbPaths -Value @('out\prep.log', $cbWork) -Encoding ascii
    Invoke-CopyBackRecord -Source $cbWork -Destination $cbVm -PathsFile $cbPaths
    Assert "the copy back puts the work copy's content in the vm-dir file" 'installed' ((Get-Content -LiteralPath $cbVm) -join '')
    $cbLines = @(Get-Content -LiteralPath $cbPaths)
    Assert "...keeps the debugcon line"                  'out\prep.log' $cbLines[0]
    Assert "...and records the vm-dir file as booted"    $cbVm $cbLines[1]
    $stampVm = $stampOk.Clone(); $stampVm.Image = $cbVm
    Assert "after which the stamp is accepted"           0 @(Get-StampProblems @stampVm -BootedImage $cbLines[1]).Count
    Set-Content -LiteralPath $cbPaths -Value @('out\prep.log', $cbWork) -Encoding ascii
    Remove-Item -LiteralPath $cbWork -Force
    $cbThrew = $false
    try { Invoke-CopyBackRecord -Source $cbWork -Destination $cbVm -PathsFile $cbPaths } catch { $cbThrew = $true }
    Assert "a failed copy throws"                        $true $cbThrew
    Assert "...and leaves the old record in place"       $cbWork (@(Get-Content -LiteralPath $cbPaths))[1]
    Assert "...so the stamp still refuses"               $true (@(Get-StampProblems @stampVm -BootedImage $cbWork) -join ' ' -match '-CopyBack')
} finally {
    Remove-Item -LiteralPath $cbDir -Recurse -Force -ErrorAction SilentlyContinue
}
$stampNoWitness = $stampOk.Clone(); $stampNoWitness.IdentSize = $null
Assert "no MiniPortExtensionSize refuses the stamp"    $true (@(Get-StampProblems @stampNoWitness) -join ' ' -match 'no MiniPortExtensionSize')
$stampWrong = $stampOk.Clone(); $stampWrong.IdentSize = 99999
Assert "another binary's size refuses the stamp"       $true (@(Get-StampProblems @stampWrong) -join ' ' -match 'not the build under test')

Write-Host "--- the monitor transport: a reply is complete only with its prompt, and a path with a space is quoted ---"
Assert "a reply ending in the prompt is complete"     $true  (Test-MonitorReplyComplete -Raw "info usb`r`n(qemu) ")
$esc = [string][char]27
Assert "...also under readline escapes"               $true  (Test-MonitorReplyComplete -Raw ($esc + "[K(qemu) " + $esc + "[D"))
Assert "a reply without the prompt is not"            $false (Test-MonitorReplyComplete -Raw "Device 0.0, Port 2, ID: dut1`r`n")
Assert "an absent reply is not"                       $false (Test-MonitorReplyComplete -Raw $null)
Assert "a plain path is sent as it is"                'C:\out\x.ppm' (ConvertTo-HmpArgument -Text 'C:\out\x.ppm')
Assert "a path with a space is quoted with forward slashes" '"C:/out dir/x.ppm"' (ConvertTo-HmpArgument -Text 'C:\out dir\x.ppm')

Write-Host "--- a monitor port that cannot be bound is a validation problem, not a sixty-second wait ---"
$probeListener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
$probeListener.Start()
$heldPort = $probeListener.LocalEndpoint.Port
try {
    Assert "a port something listens on is reported"  $true ((Test-MonitorPortBindable -Port $heldPort) -match ('port {0} cannot be bound' -f $heldPort))
    Assert "...with the netsh command to check reservations" $true ((Test-MonitorPortBindable -Port $heldPort) -match 'excludedportrange')
} finally {
    $probeListener.Stop()
}
Assert "the same port is bindable once released"     ""    (Test-MonitorPortBindable -Port $heldPort)

Write-Host "--- 'is the device listed' has three answers, and no monitor is the third ---"
if (Test-MonitorPortFree -Port $freePort) {
    $listed = Test-UsbDeviceListed -Port $freePort -Id 'dut1'
    Reset-MonitorErrors
    Assert "no monitor answers null, not 'gone'"      $true ($null -eq $listed)
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host ("selftest: {0} checks, all passed" -f $checks)
    exit 0
}
Write-Host ("selftest: {0} checks, {1} FAILED" -f $checks, $failures)
exit 1
