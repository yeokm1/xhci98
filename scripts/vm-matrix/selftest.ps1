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
    'identity devices addressed',                # identity with no ==
    'advance devices addressed == 0'             # an exact 0 is `zero`
)) {
    $threw = $false
    try { ConvertTo-Expectation -Text $bad -Table $table | Out-Null } catch { $threw = $true }
    Assert ("refuses '{0}'" -f $bad) $true $threw
}
# ...and a well-formed one must NOT be refused, or the check above proves nothing.
foreach ($good in @(
    'advance devices addressed',
    'advance devices addressed >= 3',
    'advance devices addressed == 10',
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

Write-Host "--- an advance == N is a limit both ways: under N and over N must FAIL ---"
Assert "advance == 3 with 3 -> PASS" "PASS" (Get-Outcome @('advance endpoints opened == 3') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 3 }))
Assert "advance == 3 with 2 -> FAIL" "FAIL" (Get-Outcome @('advance endpoints opened == 3') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 2 }))
Assert "advance == 3 with 4 -> FAIL" "FAIL" (Get-Outcome @('advance endpoints opened == 3') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 4 }))
# Past its limit is something that happened, so a row the OS never claimed
# must not have it waived as NODRIVER; short of it is the missing bind's.
Assert "over an == limit is not NODRIVER"  "FAIL" (Get-Outcome @(
    'advance endpoints opened >= 1'
    'advance topology: behind-hub opens == 9'
) (New-Delta @{ 'devices addressed' = 1; 'topology: behind-hub opens' = 10 }))
Assert "under an == limit can be NODRIVER" "NODRIVER" (Get-Outcome @(
    'advance endpoints opened >= 1'
    'advance topology: behind-hub opens == 9'
) (New-Delta @{ 'devices addressed' = 1 }))

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

Write-Host "--- a refusal by THIS driver outranks NODRIVER and PASS (roadmap Phase 20, F3, F9) ---"
#
# The 2026-09-05 audit fed the real evaluator two deltas and got the two wrong
# answers a verdict must never give: a request this driver REFUSED read as the
# OS's silence (NODRIVER), and a Configure Endpoint that FAILED after the open
# was accepted read as a pass.  Both vectors are evaluated here exactly as the
# runner evaluates a row - the matrix's whole `Always` block plus the row's own
# `Expect` - because a hand-picked subset is how the gap went unnoticed.
function Get-OutcomeWhy {
    param([string[]]$Texts, $Delta)
    $results = @()
    foreach ($t in $Texts) {
        $e = ConvertTo-Expectation -Text $t -Table $table
        $results += [pscustomobject]@{ Expectation = $e; Test = (Test-Expectation -Expectation $e -Delta $Delta) }
    }
    return (Get-RowOutcome -Results $results -Delta $Delta -HarnessError "" -Table $table).Why
}
$mx = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix.psd1")
$mouseRow = $null
foreach ($g in $mx.Groups) { foreach ($r in $g.Rows) { if ($r.Name -eq 'usb-mouse/hs') { $mouseRow = $r } } }
Assert "the matrix still carries the usb-mouse/hs row the vector was taken against" $true ($null -ne $mouseRow)
$mouseTexts = @($mx.Always) + @($mouseRow.Expect)
# Every refusal counter the evaluator names must resolve against the current
# table, or a renamed counter would silently retire the rule.
foreach ($lbl in @($script:DriverRefusalLabelsPermanent) + @($script:DriverRefusalLabelTransient)) {
    $threw = $false
    try { Resolve-CounterLabel -Table $table -Label $lbl | Out-Null } catch { $threw = $true }
    Assert ("refusal label '{0}' resolves" -f $lbl) $false $threw
}
# F3, the audit's exact vector: EP0 is the one accepted open, the function
# driver's open was refused by the ring pool, `endpoints opened` stays 0, the
# identity holds.  The only failed expectation is the row's bind clause.
$f3 = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 2; 'endpoint opens accepted' = 1; 'endpoint refusals - ring pool' = 1 }
Assert "F3: a ring-pool refusal with EP0 the only accepted open -> FAIL, not NODRIVER" "FAIL" (Get-Outcome $mouseTexts (New-Delta $f3))
Assert "...and the reason names the refusal"          $true ((Get-OutcomeWhy $mouseTexts (New-Delta $f3)) -match 'refused.*endpoint refusals - ring pool \+1')
foreach ($lbl in @('endpoint refusals - type', 'endpoint refusals - no device', 'endpoint refusals - params')) {
    $v = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 2; 'endpoint opens accepted' = 1 }
    $v[$lbl] = 1
    Assert ("F3 variant: '{0}' -> FAIL, not NODRIVER" -f $lbl) "FAIL" (Get-Outcome $mouseTexts (New-Delta $v))
}
# The transient refusal: usbport retries it, so a not-ready count followed by
# an open that landed is tolerated - but a not-ready count with NO open is a
# function driver that asked and never got its pipe, which is not the OS's
# disinterest.
$nrOpened = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 3; 'endpoint opens accepted' = 2; 'endpoint refusals - not ready' = 1; 'endpoints opened' = 1 }
Assert "not-ready then an open that landed -> PASS (transient, tolerated)" "PASS" (Get-Outcome $mouseTexts (New-Delta $nrOpened))
$nrNever = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 2; 'endpoint opens accepted' = 1; 'endpoint refusals - not ready' = 1 }
Assert "not-ready with no open landed -> FAIL, not NODRIVER" "FAIL" (Get-Outcome $mouseTexts (New-Delta $nrNever))
Assert "...and the reason says no endpoint opened"    $true ((Get-OutcomeWhy $mouseTexts (New-Delta $nrNever)) -match 'not ready \+1 with no non-default endpoint opened')
# F9: the open was accepted (`endpoints opened` +1, which the row's bind clause
# is satisfied by) and the Configure Endpoint it queued then failed.  Each of
# the three completion classes must read FAIL and be named.
foreach ($lbl in @('endpoint configure failures', 'endpoints refused - no bandwidth', 'endpoints refused - no resources')) {
    $v = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 2; 'endpoint opens accepted' = 2; 'endpoints opened' = 1 }
    $v[$lbl] = 1
    Assert ("F9: accepted open then '{0}' -> FAIL, not PASS" -f $lbl) "FAIL" (Get-Outcome $mouseTexts (New-Delta $v))
    Assert ("...and the reason names '{0}'" -f $lbl) $true ((Get-OutcomeWhy $mouseTexts (New-Delta $v)) -match [regex]::Escape($lbl))
}
# The rule lives in the evaluator, not only in the matrix's `Always` block: a
# row evaluated with the bind clause ALONE still reads the refusal.
Assert "F3 with the bind clause alone -> FAIL"        "FAIL" (Get-Outcome @('advance endpoints opened >= 1') (New-Delta $f3))
Assert "F9 with the bind clause alone -> FAIL"        "FAIL" (Get-Outcome @('advance endpoints opened >= 1') (New-Delta @{ 'devices addressed' = 1; 'endpoints opened' = 1; 'endpoint configure failures' = 1 }))
# A true NODRIVER is retained through the whole `Always` block: addressed,
# EP0 opened and accepted, nothing refused, nothing above usbport asked.
$trueNoDriver = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoint opens seen' = 1; 'endpoint opens accepted' = 1 }
Assert "addressed, never asked, nothing refused -> still NODRIVER" "NODRIVER" (Get-Outcome $mouseTexts (New-Delta $trueNoDriver))
# An unread refusal counter is an ERROR, never a zero - the rule every other
# unread counter already follows.
$unreadDelta = New-Delta $trueNoDriver
$unreadDelta.Values.Remove((Resolve-CounterLabel -Table $table -Label 'endpoint refusals - ring pool'))
Assert "an unread refusal counter -> ERROR, not NODRIVER" "ERROR" (Get-Outcome @('advance endpoints opened >= 1') $unreadDelta)

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

Write-Host "--- MayWedgeGuest must be read, and a typo in it must not pass ---"
#
# It was set on the audio row and read by NOTHING for the life of the harness,
# so when that group did end early the report could not say whether the matrix
# had predicted it. A declaration nothing reads is not a declaration.
#
# Driven through `Test-TargetInList`, which is what run-matrix.ps1 calls - a
# `Test-RowMayWedge` beside it answered the same question by exact Id only and
# was called by this self-test and by nothing else, so the harness was checking
# the answer it does not use (the 2026-09-16 audit's D4).
$wedgeRow = @{ Name = 'usb-audio/fs'; MayWedgeGuest = @('2a') }
Assert "a declared target is recognised"   $true  (Test-TargetInList -List $wedgeRow.MayWedgeGuest -Target @{ Id = '2a' })
Assert "an undeclared target is not"       $false (Test-TargetInList -List $wedgeRow.MayWedgeGuest -Target @{ Id = '2b' })
Assert "a row with no declaration is not"  $false (Test-TargetInList -List (@{ Name = 'x' }).MayWedgeGuest -Target @{ Id = '2a' })
Assert "a real target validates clean"     0 (@(Get-RowWedgeProblems -Row $wedgeRow -TargetIds @('2a','2b')).Count)
Assert "a target that does not exist is a problem" 1 (@(Get-RowWedgeProblems -Row @{ Name = 'x'; MayWedgeGuest = @('2c') } -TargetIds @('2a','2b')).Count)

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
# F3's whole point: the refusal reads FAIL, and FAIL is not a NODRIVER the
# ExpectNoDriver entry can waive.
Assert "a refusal is not waived by ExpectNoDriver"     $true  (Test-RowCountsAgainst -Outcome (Get-Outcome $mouseTexts (New-Delta $f3)) -NoDriverExpected $true)
Assert "an undeclared wedge (ERROR) counts"            $true  (Test-RowCountsAgainst -Outcome "ERROR")
Assert "a declared wedge (the pinned reading) does not" $false (Test-RowCountsAgainst -Outcome "ERROR" -WedgeDeclared $true -WedgeShape $true)
# The 2026-09-17 audit's D1: the declaration is keyed on the row, and it used
# to waive ANY error on that row - a refused device_add, a device never on the
# bus, an unconfirmed device_del, identity drift, a monitor timeout.  It
# licenses one shape, and both halves are required.
Assert "a declared row's non-wedge ERROR counts"       $true  (Test-RowCountsAgainst -Outcome "ERROR" -WedgeDeclared $true -WedgeShape $false)
Assert "...and so does an undeclared row's wedge"      $true  (Test-RowCountsAgainst -Outcome "ERROR" -WedgeDeclared $false -WedgeShape $true)
Assert "a stopped guest is the wedge shape"            $true  (Test-WedgeShape -LivenessVerdict "not-executing")
Assert "a gone monitor is the wedge shape"             $true  (Test-WedgeShape -LivenessVerdict "unreachable")
Assert "an alive guest is not"                         $false (Test-WedgeShape -LivenessVerdict "alive")
Assert "an unknown is not either"                      $false (Test-WedgeShape -LivenessVerdict "unknown")

Write-Host "--- the liveness verdict: a ticking PIT is not a running kernel ---"
#
# The 2026-09-17 audit's D2.  A bugchecked guest reads `running` with an
# `info irq` delta, because the PIT raises IRQ0 whether or not a halted kernel
# services it; with the keep-alive pump's `transfers completed` handed in as
# the sign of life, that guest has to complete a transfer too.  Without a
# sign of life the timer reading alone decides, as before, for the callers
# that have no pump.
Assert "running, parsed, ticking is alive"             "alive"         (Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 40).Verdict
Assert "...with the pump advancing too"                "alive"         (Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 40 -LifeDelta 3).Verdict
Assert "ticking but no transfer completed is not"      "not-executing" (Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 40 -LifeDelta 0).Verdict
Assert "...and says why"                               $true           ((Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 40 -LifeDelta 0).Why -match 'completed no transfer')
Assert "a stopped clock is not"                        "not-executing" (Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 0 -LifeDelta 3).Verdict
Assert "a paused VM is not"                            "not-executing" (Get-LivenessVerdict -Running $false -Parsed $true -IrqDelta 40).Verdict
Assert "an unparseable info irq is unknown"            "unknown"       (Get-LivenessVerdict -Running $true -Parsed $false -IrqDelta 0).Verdict
Assert "...even with the pump advancing"               "unknown"       (Get-LivenessVerdict -Running $true -Parsed $false -IrqDelta 0 -LifeDelta 3).Verdict

Write-Host "--- ...and the sign of life is read off the snapshot the way Read-Counters returns it ---"
#
# Read-Counters returns { Values; Unread; Read } with Values keyed by the raw
# field name (TransfersCompleted), not by the label.  The runner's callback
# is `Get-KeepAliveTransfers -Snapshot (Read-Counters ...) -Table $table`, so
# it is driven here against a snapshot shaped exactly like that return, with
# the real table: a callback indexing the wrong object or the wrong key would
# read $null, cast to 0, and every pumped row would read as a dead guest
# (Codex review round 1 on the 2026-09-17 audit's D2).
$lifeSnap = [pscustomobject]@{ Values = @{ 'TransfersCompleted' = [int64]7; 'TransfersSubmitted' = [int64]9 }; Unread = 0; Read = 2 }
Assert "the field is read from .Values by its raw name" 7 (Get-KeepAliveTransfers -Snapshot $lifeSnap -Table $table)
$lifeCb = { Get-KeepAliveTransfers -Table $table -Snapshot $lifeSnap }.GetNewClosure()
Assert "...through a closure built as the runner builds it" 7 ([int64](& $lifeCb))
$lifeSnap2 = [pscustomobject]@{ Values = @{ 'TransfersCompleted' = [int64]12 }; Unread = 0; Read = 1 }
Assert "...and two snapshots give the pump's delta"     "alive" (Get-LivenessVerdict -Running $true -Parsed $true -IrqDelta 40 -LifeDelta ((Get-KeepAliveTransfers -Snapshot $lifeSnap2 -Table $table) - (Get-KeepAliveTransfers -Snapshot $lifeSnap -Table $table))).Verdict
$lifeThrew = $false
try { Get-KeepAliveTransfers -Snapshot ([pscustomobject]@{ Values = @{ 'transfers completed' = [int64]7 }; Unread = 1; Read = 1 }) -Table $table | Out-Null } catch { $lifeThrew = $true }
Assert "a snapshot keyed by the label, not the field, is an error and not a zero" $true $lifeThrew
$lifeThrew = $false
try { Get-KeepAliveTransfers -Snapshot ([pscustomobject]@{ Values = @{}; Unread = 1; Read = 0 }) -Table $table | Out-Null } catch { $lifeThrew = $true }
Assert "an unread field is an error and not a zero"    $true $lifeThrew

Write-Host "--- the header carries every variable thing, and nothing else does ---"
$hdr = New-PostReleaseHeader -TargetId '2a-fresh' -Version '1.0.0.0' -DriverLine '1.0.0.0 qemu, 1 B, sha256 0' -ImageLine 'vm\fresh-2a.img, stamp base-1.0.0.0-qemu, from win98.img post-nusb' `
           -QemuVersion '11.0.0' -Accel 'tcg' -Sizeof 12345 -Counters 7 -Started (Get-Date '2026-08-30 10:00:00') -Elapsed ([timespan]::FromMinutes(61)) `
           -Verdict 'PASS' -Rows 20 -NoDriverExpected 4 -NotReached 3 -VhubLine 'switch 2 in every group'
Assert "the verdict line is as designed"       $true (($hdr -join "`n") -match '# verdict:\s+2a-fresh PASS, 20 rows, 4 NODRIVER expected, 3 not reached')
Assert "the switch the rows were judged at"    $true (($hdr -join "`n") -match '# vhub:\s+switch 2 in every group')
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
$prepE = @{ Id = '2e'; Like = '2a'; Image = 'winme.img'; PrepareOnly = $true }
Assert "a PrepareOnly target boots in neither run (ordinary)"     "2a,2b"    ((@(Select-RunTargets -Targets ($allTargets + $prepE) -PostRelease $false) | ForEach-Object { $_.Id }) -join ",")
Assert "a PrepareOnly target boots in neither run (post-release)" "2a-fresh" ((@(Select-RunTargets -Targets ($allTargets + $prepE) -PostRelease $true) | ForEach-Object { $_.Id }) -join ",")
$msg = ""; try { Select-RunTargets -Targets ($allTargets + $prepE) -PostRelease $false -Requested @('2e') | Out-Null } catch { $msg = $_.Exception.Message }
Assert "a PrepareOnly target named with -Target is refused"       $true     ($msg -match 'PrepareOnly' -and $msg -match 'prepare-image')
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
# roadmap Phase 20, F11: a target whose every row was EXCLUDED (or never reached)
# has measured nothing, and read PASS.
Assert "every row not reached is FAIL (F11)"   "FAIL" (Get-TargetVerdict -Tally @{ Rows = 3; NotReached = 3; Against = 0 })
Assert "one row reached, nothing against, is PASS" "PASS" (Get-TargetVerdict -Tally @{ Rows = 3; NotReached = 2; Against = 0 })
Assert "one row reached and against is FAIL"   "FAIL" (Get-TargetVerdict -Tally @{ Rows = 3; NotReached = 2; Against = 1 })

Write-Host "--- a group-level failure counts the rows it never measured (H20) ---"
# The audit's own example: a seventeen-row group whose fifth row was in flight
# owes twelve unreached rows, not zero.
$midGroup = Get-GroupFailureTally -GroupRows 17 -Reached 5 -RowInFlight $true
Assert "a row in flight owns the ERROR line"   1  ($midGroup.Rows - $midGroup.NotReached)
Assert "...and the tail behind it is unreached" 12 $midGroup.NotReached
# And the case the first fix missed entirely: the failure that happens before
# the first row - a monitor that never answers, a driver that never starts.
$preRow = Get-GroupFailureTally -GroupRows 17 -Reached 0 -RowInFlight $false
Assert "a failure before the first row counts every row" 17 $preRow.Rows
Assert "...all of them unreached"                        17 $preRow.NotReached
Assert "...so the target cannot read PASS on it" "FAIL" (Get-TargetVerdict -Tally @{ Rows = $preRow.Rows; NotReached = $preRow.NotReached; Against = 0 })
$lastRow = Get-GroupFailureTally -GroupRows 3 -Reached 3 -RowInFlight $true
Assert "the last row in flight leaves nothing behind"    0 $lastRow.NotReached
Assert "...and still counts itself"                      1 $lastRow.Rows

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

Write-Host "--- the qemu package: read from out\pkg-qemu-<arch>, and the untagged out\pkg-qemu is refused, not used ---"
$pkgRepo = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-selftest-pkg-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $pkgRepo -Force | Out-Null
try {
    Assert "the x86 package directory is what make-package.ps1 writes" (Join-Path $pkgRepo 'out\pkg-qemu-x86') (Get-QemuPackageDir -Repo $pkgRepo)
    Assert "...and amd64 names its own"                    (Join-Path $pkgRepo 'out\pkg-qemu-amd64') (Get-QemuPackageDir -Repo $pkgRepo -Arch 'amd64')
    $msg = Get-QemuPackageProblem -Repo $pkgRepo
    Assert "no package at all is a problem"                $true ($msg -match 'no qemu package at .*pkg-qemu-x86')
    Assert "...that says how to build one"                 $true ($msg -match 'make-package\.ps1 -Flavor qemu -Arch x86')
    Assert "...and names no stale directory that is not there" $false ($msg -match 'older build')
    $legacyDir = Join-Path $pkgRepo 'out\pkg-qemu'
    New-Item -ItemType Directory -Path $legacyDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $legacyDir 'xhci98.sys') -Value 'old' -Encoding ascii
    Set-Content -LiteralPath (Join-Path $legacyDir 'xhci98.inf') -Value 'old' -Encoding ascii
    $msg = Get-QemuPackageProblem -Repo $pkgRepo
    Assert "a stale untagged out\pkg-qemu does not stand in for the package" $true ($null -ne $msg)
    Assert "...and is named as the older build it is"      $true ($msg -match 'pkg-qemu holds a binary' -and $msg -match 'not used')
    $newDir = Get-QemuPackageDir -Repo $pkgRepo
    New-Item -ItemType Directory -Path $newDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $newDir 'xhci98.sys') -Value 'new' -Encoding ascii
    Assert "a binary without its INF is not a package"     $true ($null -ne (Get-QemuPackageProblem -Repo $pkgRepo))
    Set-Content -LiteralPath (Join-Path $newDir 'xhci98.inf') -Value 'new' -Encoding ascii
    Assert "the INF and the binary together are"           $true ($null -eq (Get-QemuPackageProblem -Repo $pkgRepo))
    Assert "...whatever the untagged directory still holds" $true (Test-Path -LiteralPath (Join-Path $legacyDir 'xhci98.sys'))
    Assert "an x86 package does not satisfy amd64"         $true ($null -ne (Get-QemuPackageProblem -Repo $pkgRepo -Arch 'amd64'))
} finally {
    Remove-Item -LiteralPath $pkgRepo -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "--- the monitor transport: a reply is complete only with its prompt, and a path with a space is quoted ---"
Assert "a reply ending in the prompt is complete"     $true  (Test-MonitorReplyComplete -Raw "info usb`r`n(qemu) ")
$esc = [string][char]27
Assert "...also under readline escapes"               $true  (Test-MonitorReplyComplete -Raw ($esc + "[K(qemu) " + $esc + "[D"))
Assert "a reply without the prompt is not"            $false (Test-MonitorReplyComplete -Raw "Device 0.0, Port 2, ID: dut1`r`n")
Assert "an absent reply is not"                       $false (Test-MonitorReplyComplete -Raw $null)
# H19, the half no timeout can see: the banner's own prompt still in the
# buffer when the command goes out. Without -RequireEcho that reads complete
# and the caller gets an empty answer, which for `info usb` is a departure
# that never happened.
$bannerOnly = "QEMU 11.0.0 monitor - type 'help'`r`n(qemu) "
Assert "the banner's prompt alone is not this command's reply" $false (Test-MonitorReplyComplete -Raw $bannerOnly -Echo "info usb" -RequireEcho)
Assert "...and it is exactly what the lenient form accepts" $true (Test-MonitorReplyComplete -Raw $bannerOnly -Echo "info usb")
Assert "the echo with its own prompt after it is complete" $true (Test-MonitorReplyComplete -Raw ($bannerOnly + "info usb`r`nDevice 0.0, Port 2, ID: dut1`r`n(qemu) ") -Echo "info usb" -RequireEcho)
Assert "the echo with no prompt after it is not"      $false (Test-MonitorReplyComplete -Raw ($bannerOnly + "info usb`r`n") -Echo "info usb" -RequireEcho)
Assert "a plain path is sent as it is"                'C:\out\x.ppm' (ConvertTo-HmpArgument -Text 'C:\out\x.ppm')
Assert "a path with a space is quoted with forward slashes" '"C:/out dir/x.ppm"' (ConvertTo-HmpArgument -Text 'C:\out dir\x.ppm')
# chardev-add: HMP quotes WHOLE arguments, so a spaced path must quote the
# whole option string, not the value after path= (Phase 20 review, round 2,
# probed against QEMU 11); a comma cannot be carried and is refused.
Assert "a chardev-add with a plain path is unquoted"  'chardev-add file,id=matrixchr1,path=C:\out\m.log' (New-ChardevAddCommand -Id 'matrixchr1' -Path 'C:\out\m.log')
Assert "a chardev-add with a spaced path quotes the whole option string" 'chardev-add "file,id=matrixchr1,path=C:/out dir/m.log"' (New-ChardevAddCommand -Id 'matrixchr1' -Path 'C:\out dir\m.log')
$commaThrew = $false
try { New-ChardevAddCommand -Id 'matrixchr1' -Path 'C:\out,dir\m.log' | Out-Null } catch { $commaThrew = $true }
Assert "a chardev-add path with a comma is refused"   $true $commaThrew

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

Write-Host "--- the XP x64 and Windows 7 targets: family, build, table and 64-bit addresses ---"
Assert "2a-fresh is the Windows 98 family"      "win98"   (Get-TargetFamily -Target @{ Id = '2a-fresh'; Like = '2a' })
Assert "2b-fresh is the Windows 2000 family"    "win2k"   (Get-TargetFamily -Target @{ Id = '2b-fresh'; Like = '2b' })
Assert "an explicit Family wins"                "winxp64" (Get-TargetFamily -Target @{ Id = 'xp64-fresh'; Family = 'WinXP64' })
$famThrew = $false
try { Get-TargetFamily -Target @{ Id = '2d' } | Out-Null } catch { $famThrew = $true }
Assert "the SMP guest has no family"            $true $famThrew
$famThrew = $false
try { Get-TargetFamily -Target @{ Id = 'x'; Family = 'winme' } | Out-Null } catch { $famThrew = $true }
Assert "an unknown Family is refused"           $true $famThrew
Assert "no Arch key is x86"                     "x86"   (Get-TargetArch -Target @{ Id = '2a' })
Assert "Arch amd64 is read"                     "amd64" (Get-TargetArch -Target @{ Id = 'xp64-fresh'; Arch = 'amd64' })
$archThrew = $false
try { Get-TargetArch -Target @{ Id = 'x'; Arch = 'ia64' } | Out-Null } catch { $archThrew = $true }
Assert "an unknown Arch is refused"             $true $archThrew
Assert "winxp64.img is refused even when stamped" $true (@(Get-FreshImageProblems -ImagePath 'vm\winxp64.img' -Snapshots $ok -Version '1.0.0.0') -join ' ' -match 'carried-along')
Assert "win7.img is refused even when stamped"  $true (@(Get-FreshImageProblems -ImagePath 'vm\win7.img' -Snapshots $ok -Version '1.0.0.0') -join ' ' -match 'carried-along')

$table64 = Import-CounterTable -Arch amd64
Assert "the amd64 table is its own file"        $true ($table64.OffsetsFile -like '*offsets-amd64.txt')
Assert "...with a larger SIZEOF than x86"       $true ($table64.Sizeof -gt $table.Sizeof)
Assert "...and the same counters"               $table.Offsets.Count $table64.Offsets.Count

Assert "an x86 address keeps eight digits"      "0xC1468970"         (Format-GuestAddress -BaseVa 'C1468870' -Offset 0x100)
Assert "an amd64 address keeps sixteen"         "0xFFFFFADFCE308830" (Format-GuestAddress -BaseVa 'FFFFFADFCE2F5DC8' -Offset 76392)
Assert "the carry reaches the high half"        "0xFFFFFAE000000010" (Format-GuestAddress -BaseVa 'FFFFFADFFFFFFFF0' -Offset 0x20)
Assert "a string address reaches the message"   $true ((New-CounterReadFailure -Addr '0xFFFFFADFCE308830' -Take 4 -Got 2 -Attempts 4 -MonitorListening $true) -match '0xFFFFFADFCE308830')

$idLog = Join-Path $env:TEMP ("xhci98-selftest-ident-{0}.log" -f $PID)
Set-Content -LiteralPath $idLog -Encoding ascii -Value @(
    'xhci98: cb StartController irql=00 a=CE2F5DC8 b=0012F000 c=00000000'
    'xhci98: StartController extension VA high=FFFFFADF'
    'xhci98: StartController extension VA low=CE2F5DC8'
    'xhci98: MiniPortExtensionSize=00017538'
    'xhci98: cb StartController irql=00 a=CE2F5DC8 b=0012F000 c=00000000')
$id64 = Find-ExtensionIdentity -DebugconLog $idLog
Assert "an amd64 log is read from the full pair" "FFFFFADFCE2F5DC8" $id64.Va
Assert "...and the truncated a= does not count"  $false $id64.Spans
Set-Content -LiteralPath $idLog -Encoding ascii -Value @(
    'xhci98: cb StartController irql=00 a=C14658C4 b=81F0437C c=00000000'
    'xhci98: MiniPortExtensionSize=00016890')
$id32 = Find-ExtensionIdentity -DebugconLog $idLog
Assert "an x86 log is read from a= as before"   "C14658C4" $id32.Va
Set-Content -LiteralPath $idLog -Encoding ascii -Value @(
    'xhci98: StartController extension VA high=FFFFFADF'
    'xhci98: StartController extension VA low=CE2F5DC8'
    'xhci98: StartController extension VA high=FFFFFADF'
    'xhci98: StartController extension VA low=CE31A008')
Assert "two amd64 loads are a span"             $true (Find-ExtensionIdentity -DebugconLog $idLog -Arch amd64).Spans
# A boot poll that lands between the callback line and the low half: on amd64
# the truncated a= must never become the identity (Codex review of fcbf9a1).
Set-Content -LiteralPath $idLog -Encoding ascii -Value @(
    'xhci98: cb StartController irql=00 a=CE2F5DC8 b=0012F000 c=00000000'
    'xhci98: StartController extension VA high=FFFFFADF')
Assert "amd64: a= alone is no identity"          $null (Find-ExtensionIdentity -DebugconLog $idLog -Arch amd64).Va
Assert "x86 reading of the same log keeps a="    "CE2F5DC8" (Find-ExtensionIdentity -DebugconLog $idLog).Va
Assert "the identity carries its architecture"   "amd64" (Find-ExtensionIdentity -DebugconLog $idLog -Arch amd64).Arch
$partialIdent = [pscustomobject]@{ Va = 'FFFFFADFCE2F5DC8'; Arch = 'amd64' }
Assert "amd64 drift sees the missing pair"       $true ((Get-ExtensionIdentityDrift -Ident $partialIdent -DebugconLog $idLog) -ne "")
Add-Content -LiteralPath $idLog -Encoding ascii -Value 'xhci98: StartController extension VA low=CE2F5DC8'
Assert "amd64: the completed pair is read"       "FFFFFADFCE2F5DC8" (Find-ExtensionIdentity -DebugconLog $idLog -Arch amd64).Va
Assert "amd64 drift on the same pair is none"    "" (Get-ExtensionIdentityDrift -Ident $partialIdent -DebugconLog $idLog)
Remove-Item -LiteralPath $idLog -Force -ErrorAction SilentlyContinue

Write-Host "--- the virtual-hub switch: read from the driver, every value covered once, each row judged by its own form ---"
Assert "nothing started is switch 0"              0 (Get-VhubSwitchReading -Started 0 -Created 0 -Dropped 0)
Assert "started, no hub yet is switch 1"          1 (Get-VhubSwitchReading -Started 1 -Created 0 -Dropped 0)
Assert "started, the keep-alive's hub is 1"       1 (Get-VhubSwitchReading -Started 1 -Created 1 -Dropped 0)
Assert "a hub dropped and re-made is still 1"     1 (Get-VhubSwitchReading -Started 1 -Created 2 -Dropped 1)
Assert "a hub on two ports is switch 2"           2 (Get-VhubSwitchReading -Started 1 -Created 2 -Dropped 0)
Assert "the matrix QEMU's four is switch 2"       2 (Get-VhubSwitchReading -Started 1 -Created 4 -Dropped 0)
$swThrew = $false
try { Get-VhubSwitchReading -Started 0 -Created 4 -Dropped 0 | Out-Null } catch { $swThrew = $true }
Assert "hubs with nothing started is refused"     $true $swThrew

function New-SwitchSnapshot {
    param([int64]$Started, [int64]$Created, [int64]$Dropped, [string]$Omit = "")
    $v = @{}
    foreach ($p in @(@('vhub started', $Started), @('vhub hubs created', $Created), @('vhub hubs dropped', $Dropped))) {
        if ($p[0] -ne $Omit) { $v[(Resolve-CounterLabel -Table $table -Label $p[0])] = $p[1] }
    }
    return [pscustomobject]@{ Values = $v; Unread = 0; Read = $v.Count }
}
Assert "the snapshot form reads 2"                2 (Get-VhubSwitchFromSnapshot -Snapshot (New-SwitchSnapshot 1 4 0) -Table $table).Switch
Assert "...and says what it read it from"         $true ((Get-VhubSwitchFromSnapshot -Snapshot (New-SwitchSnapshot 1 4 0) -Table $table).Text -match 'hubs created 4')
$swThrew = $false
try { Get-VhubSwitchFromSnapshot -Snapshot (New-SwitchSnapshot 1 4 0 -Omit 'vhub hubs dropped') -Table $table | Out-Null } catch { $swThrew = $true }
Assert "an unread switch counter is an error"     $true $swThrew
$oldTable = [pscustomobject]@{ FieldOfLabel = @{}; Offsets = @{}; OffsetsFile = 'old' }
Assert "a build before 24.3 is switch 0"          0 (Get-VhubSwitchFromSnapshot -Snapshot ([pscustomobject]@{ Values = @{} }) -Table $oldTable).Switch

Assert "a well-formed ExpectBySwitch is clean"    0 @(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @('b') } }).Count
Assert "a row without one is clean"               0 @(Get-RowSwitchProblems -Row @{ Name = 'x' }).Count
Assert "a value left out is a problem"            1 @(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '2' = @('b') } }).Count
Assert "a value covered twice is a problem"       1 @(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0,1' = @('a'); '1,2' = @('b') } }).Count
Assert "a value that is no switch is a problem"   $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @('b'); '3' = @('c') } }).Count -ge 1)
Assert "a key naming one value twice is refused"  $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0,0' = @('a'); '1,2' = @('b') } }).Count -ge 1)
Assert "a list instead of a table is a problem"   1 @(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @('a') }).Count
Assert "an empty form is a problem"               $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @() } }).Count -ge 1)
Assert "a blank line in a form is a problem"      $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @('b', ' ') } }).Count -ge 1)
Assert "a non-string in a form is a problem"      $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @(1) } }).Count -ge 1)
Assert "a null form is a problem"                 $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = $null } }).Count -ge 1)
Assert "a null beside a line is a problem"        $true (@(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a'); '1,2' = @('b', $null) } }).Count -ge 1)
Assert "a bare one-line string is a form"         0 @(Get-RowSwitchProblems -Row @{ Name = 'x'; ExpectBySwitch = @{ '0' = 'a'; '1,2' = @('b') } }).Count
$swRow = @{ Name = 'x'; ExpectBySwitch = @{ '0' = @('a0'); '1,2' = @('b1', 'b2') } }
Assert "switch 0 takes its own lines"             "a0"    ((Get-RowSwitchExpectTexts -Row $swRow -Switch 0) -join ",")
Assert "switch 2 takes the shared on-form"        "b1,b2" ((Get-RowSwitchExpectTexts -Row $swRow -Switch 2) -join ",")
Assert "a row without the key adds nothing"       0 @(Get-RowSwitchExpectTexts -Row @{ Name = 'x' } -Switch 2).Count

Assert "one value everywhere is one line"         "switch 2 in every group" (Format-VhubSwitchLine -ByGroup @{ audio = 2; hid = 2 } -GroupOrder @('audio', 'hid'))
Assert "a group without a reading is named"       "switch 0 in audio; not read in hid" (Format-VhubSwitchLine -ByGroup @{ audio = 0 } -GroupOrder @('audio', 'hid'))
Assert "a difference is said loudly"              $true ((Format-VhubSwitchLine -ByGroup @{ audio = 2; hid = 0 } -GroupOrder @('audio', 'hid')) -match '^SWITCH DIFFERS BY GROUP: audio 2, hid 0')
Assert "no reading at all says so"                $true ((Format-VhubSwitchLine -ByGroup @{} -GroupOrder @('audio')) -match '^not read')

$rhLog = Join-Path $env:TEMP ("xhci98-selftest-rh-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".log")
Set-Content -LiteralPath $rhLog -Encoding ascii -Value @('xhci98: cb StartController irql=00 a=8182292C b=81822338 c=00000000')
Assert "StartController alone: root hub not asked" $false (Test-RootHubDataAsked -DebugconLog $rhLog)
Add-Content -LiteralPath $rhLog -Encoding ascii -Value 'xhci98: cb RH_GetRootHubData irql=00 a=8182292C b=F401B514 c=00000000'
Assert "...then asked"                             $true (Test-RootHubDataAsked -DebugconLog $rhLog)
Remove-Item -LiteralPath $rhLog -Force -ErrorAction SilentlyContinue
Assert "no log: not asked"                         $false (Test-RootHubDataAsked -DebugconLog $rhLog)

# The tracked matrix, against round 11's readings at 2 and run 23's at 0.
$mxTracked = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix.psd1")
$mxProblems = @()
foreach ($g in $mxTracked.Groups) { foreach ($r in $g.Rows) { $mxProblems += (Get-RowSwitchProblems -Row $r) } }
Assert "the tracked matrix's switch forms are clean" 0 $mxProblems.Count
function Get-TrackedRow { param([string]$Name) foreach ($g in $mxTracked.Groups) { foreach ($r in $g.Rows) { if ($r.Name -eq $Name) { return $r } } } }
function Get-SwitchOutcome {
    param($Row, [int]$Switch, [hashtable]$ByLabel)
    $texts = @($mxTracked.Always) + @($Row.Expect) + @(Get-RowSwitchExpectTexts -Row $Row -Switch $Switch)
    return (Get-Outcome $texts (New-Delta $ByLabel))
}
$kbdFs = Get-TrackedRow 'usb-kbd/fs'
$fsAt2 = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoints opened' = 1; 'endpoint opens seen' = 3; 'endpoint opens accepted' = 3 }
$fsAt0 = $fsAt2.Clone(); $fsAt0['endpoint speed mismatches'] = 1
Assert "kbd/fs at 2, no mismatch: PASS"           "PASS" (Get-SwitchOutcome $kbdFs 2 $fsAt2)
Assert "kbd/fs at 1, no mismatch: PASS"           "PASS" (Get-SwitchOutcome $kbdFs 1 $fsAt2)
Assert "kbd/fs at 2 with a mismatch: FAIL"        "FAIL" (Get-SwitchOutcome $kbdFs 2 $fsAt0)
Assert "kbd/fs at 0 with its mismatch: PASS"      "PASS" (Get-SwitchOutcome $kbdFs 0 $fsAt0)
Assert "kbd/fs at 0 with none: FAIL"              "FAIL" (Get-SwitchOutcome $kbdFs 0 $fsAt2)
Assert "mouse/fs at 2, no mismatch: PASS"         "PASS" (Get-SwitchOutcome (Get-TrackedRow 'usb-mouse/fs') 2 $fsAt2)
$churn = Get-TrackedRow 'usb-hub/churn'
# Round 11 at 2, one leg (r11-matrix-report.md); opens seen = accepted = 33.
$churnAt2 = @{ 'devices addressed' = 10; 'slots enabled' = 10; 'endpoints opened' = 9; 'endpoint opens seen' = 33; 'endpoint opens accepted' = 33
               'topology: hub descriptors folded' = 6; 'topology: hub slots marked' = 5; 'topology: behind-hub opens' = 9
               'topology: behind-hub devices addressed' = 9; 'topology: TT pairs agreeing with usbport' = 9 }
Assert "churn at 2, round 11's leg: PASS"         "PASS" (Get-SwitchOutcome $churn 2 $churnAt2)
Assert "churn at 0 on round 11's leg: FAIL"       "FAIL" (Get-SwitchOutcome $churn 0 $churnAt2)
$churnTier5 = $churnAt2.Clone(); $churnTier5['devices addressed'] = 11; $churnTier5['slots enabled'] = 11
$churnTier5['topology: behind-hub devices addressed'] = 10; $churnTier5['topology: behind-hub opens'] = 10
Assert "churn at 2 reaching tier 5: FAIL"         "FAIL" (Get-SwitchOutcome $churn 2 $churnTier5)
$churnExtra = $churnAt2.Clone(); $churnExtra['devices addressed'] = 11; $churnExtra['slots enabled'] = 11
Assert "churn at 2, one device too many: FAIL"    "FAIL" (Get-SwitchOutcome $churn 2 $churnExtra)
$churnPhantom = $churnAt2.Clone(); $churnPhantom['topology: TT pairs disagreeing with usbport'] = 1
Assert "churn at 2 with a disagreement: FAIL"     "FAIL" (Get-SwitchOutcome $churn 2 $churnPhantom)
$churnTooDeep = $churnAt2.Clone(); $churnTooDeep['topology: behind-hub refused - too deep'] = 1
Assert "churn at 2 the driver refusing: FAIL"     "FAIL" (Get-SwitchOutcome $churn 2 $churnTooDeep)
# Run 23 at 0, one leg: eleven addressed, ten behind a hub, ten phantom TTs.
$churnAt0 = @{ 'devices addressed' = 11; 'slots enabled' = 11; 'endpoints opened' = 10; 'endpoint opens seen' = 36; 'endpoint opens accepted' = 36
               'topology: hub descriptors folded' = 6; 'topology: hub slots marked' = 5; 'topology: behind-hub opens' = 10
               'topology: behind-hub devices addressed' = 10; 'topology: TT pairs disagreeing with usbport' = 10 }
Assert "churn at 0, run 23's leg: PASS"           "PASS" (Get-SwitchOutcome $churn 0 $churnAt0)
Assert "churn at 2 on run 23's leg: FAIL"         "FAIL" (Get-SwitchOutcome $churn 2 $churnAt0)


Write-Host "--- the HCD's expectation set (26-A.10): every row decided, a wrong speed a FAIL, the retired labels refused ---"
#
# Driven against a STAND-IN table: one invented field per label the HCD's set
# and this harness name, because the counter block those labels will be
# derived from (design record 13 section 9.4) is not in src\ yet, and the
# vectors below are about the evaluator and the set, not about offsets. The
# real table is checked against the same set by run-matrix.ps1 -ValidateOnly
# once gen-offsets.ps1 -Driver hcd can write it.
$mxHcd = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix-hcd.psd1")
$mxHcdBroken = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix-hcd.broken.psd1")
Assert "matrix-hcd.psd1 is the HCD's set"            "hcd" (Get-MatrixDriver -Matrix $mxHcd)
Assert "matrix.psd1 is the miniport's"               "miniport" (Get-MatrixDriver -Matrix (Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix.psd1")))
$threw = $false; try { Get-MatrixDriver -Matrix @{ Driver = 'usbport' } | Out-Null } catch { $threw = $true }
Assert "an unknown Driver is refused"                $true $threw
Assert "the tracked HCD set validates clean"         0 @(Get-HcdSetProblems -Matrix $mxHcd).Count
Assert "the broken HCD set validates clean"          0 @(Get-HcdSetProblems -Matrix $mxHcdBroken).Count
Assert "the miniport's set is refused as an HCD set" $true (@(Get-HcdSetProblems -Matrix (Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix.psd1"))).Count -gt 0)

# The stand-in: every label the set names, the harness's own, the HCD refusal
# set and the speed labels - and nothing else, so a retired label cannot
# resolve against it.
$hcdLabels = @('devices addressed', 'slots enabled', 'endpoints opened', 'transfers completed') +
             @($script:HcdRefusalLabelsPermanent) + @(Get-HcdEnumerationLabels) + @($script:HcdSpeedDisagreeLabel)
foreach ($t in @($mxHcd.Always)) { $hcdLabels += @(Get-ExpectationLabels -Text $t) }
foreach ($g in $mxHcd.Groups) {
    foreach ($r in $g.Rows) {
        foreach ($t in @($r.Expect)) { $hcdLabels += @(Get-ExpectationLabels -Text $t) }
        if ($r.ContainsKey('ExpectByTarget')) { foreach ($k in $r.ExpectByTarget.Keys) { foreach ($t in @($r.ExpectByTarget[$k])) { $hcdLabels += @(Get-ExpectationLabels -Text $t) } } }
        if ($r.ContainsKey('ClaimLabel')) { $hcdLabels += [string]$r.ClaimLabel }
    }
}
$hcdLabels = @($hcdLabels | Sort-Object -Unique)
$hcdOff = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-selftest-hcd-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".txt")
$hcdOffLines = @(("SIZEOF {0}" -f (4 * $hcdLabels.Count)))
$hcdMapLines = @()
for ($i = 0; $i -lt $hcdLabels.Count; $i++) {
    $hcdOffLines += ("F{0} {1}" -f $i, (4 * $i))
    $hcdMapLines += ("F{0}`t{1}" -f $i, $hcdLabels[$i])
}
Set-Content -LiteralPath $hcdOff -Value $hcdOffLines -Encoding ascii
Set-Content -LiteralPath ([IO.Path]::ChangeExtension($hcdOff, ".labels.txt")) -Value $hcdMapLines -Encoding ascii
$hcdTable = Import-CounterTable -OffsetsFile $hcdOff -Driver hcd
Remove-Item -LiteralPath $hcdOff, ([IO.Path]::ChangeExtension($hcdOff, ".labels.txt")) -Force -ErrorAction SilentlyContinue
Assert "the stand-in table says which driver it is"  "hcd" $hcdTable.Driver

function Get-HcdRow { param($Matrix, [string]$Name) foreach ($g in $Matrix.Groups) { foreach ($r in $g.Rows) { if ($r.Name -eq $Name) { return $r } } } }
function New-HcdDelta {
    param([hashtable]$ByLabel)
    $v = @{}
    foreach ($f in $hcdTable.Offsets.Keys) { $v[$f] = 0 }
    foreach ($label in $ByLabel.Keys) { $v[(Resolve-CounterLabel -Table $hcdTable -Label $label)] = $ByLabel[$label] }
    return [pscustomobject]@{ Values = $v; WentBackwards = @(); Restarted = $false }
}
# A row judged as run-matrix.ps1 judges it: Always, Expect, the target's
# ExpectByTarget, the harness's speed lines, the row's claim label and the
# HCD's refusal and enumeration sets. -NoEnumerationLabels drops the last, to
# show it is load-bearing.
function Get-HcdOutcome {
    param($Row, $Delta, [string]$TargetKey = "", [switch]$NoEnumerationLabels, [switch]$Why, $Matrix = $mxHcd)
    $texts = @($Matrix.Always) + @($Row.Expect)
    if ($TargetKey -ne "" -and $Row.ContainsKey('ExpectByTarget') -and $Row.ExpectByTarget.ContainsKey($TargetKey)) { $texts += @($Row.ExpectByTarget[$TargetKey]) }
    $texts += @(Get-HcdSpeedExpectationTexts -ExpectedSpeed $Row.ExpectedSpeed)
    $results = @()
    foreach ($t in $texts) {
        $e = ConvertTo-Expectation -Text $t -Table $hcdTable
        $results += [pscustomobject]@{ Expectation = $e; Test = (Test-Expectation -Expectation $e -Delta $Delta) }
    }
    $enum = if ($NoEnumerationLabels) { @() } else { @(Get-HcdEnumerationLabels) }
    $o = Get-RowOutcome -Results $results -Delta $Delta -HarnessError "" -Table $hcdTable -Driver hcd `
             -ClaimedLabel (Get-RowClaimLabel -Row $Row) -EnumerationLabels $enum
    if ($Why) { return $o.Why }
    return $o.Outcome
}

# Every line of the set, for every target key it names, parses against the
# stand-in: the vocabulary is consistent, and is the list the counter block
# must publish.
$hcdParseProblems = @()
foreach ($g in $mxHcd.Groups) {
    foreach ($r in $g.Rows) {
        $keys = @("")
        if ($r.ContainsKey('ExpectByTarget')) { $keys += @($r.ExpectByTarget.Keys) }
        foreach ($k in $keys) {
            $texts = @($mxHcd.Always) + @($r.Expect)
            # A row with no usable ExpectedSpeed is the validator's to report
            # (above); here it is one more problem rather than the end of the run.
            try { $texts += @(Get-HcdSpeedExpectationTexts -ExpectedSpeed $r.ExpectedSpeed) } catch { $hcdParseProblems += $_.Exception.Message }
            if ($k -ne "") { $texts += @($r.ExpectByTarget[$k]) }
            foreach ($t in $texts) { try { ConvertTo-Expectation -Text $t -Table $hcdTable | Out-Null } catch { $hcdParseProblems += $_.Exception.Message } }
        }
    }
}
Assert "every HCD row's lines parse"                 0 $hcdParseProblems.Count
foreach ($lbl in @('endpoint speed mismatches', 'endpoint refusals - not ready', 'vhub started')) {
    $threw = $false; try { Resolve-CounterLabel -Table $hcdTable -Label $lbl | Out-Null } catch { $threw = $true }
    Assert ("a retired label '{0}' resolves to nothing in the HCD's table" -f $lbl) $true $threw
}
# ...so the miniport's refusal rule cannot be run against the HCD's table:
# its transient label is retired, and asking for it is an error, not a zero.
$threw = $false
try { Get-DriverRefusalEvidence -Delta (New-HcdDelta @{}) -Table $hcdTable -Driver miniport | Out-Null } catch { $threw = $true }
Assert "the miniport's refusal set does not resolve against the HCD's table" $true $threw

Write-Host "--- the HCD set's validator refuses an undecided or miniport-shaped row ---"
$okRow = @{ Name = 'r'; Model = 'usb-kbd'; Settle = 20; ExpectedSpeed = 'HS'; Expect = @('advance endpoints opened >= 1') }
Assert "a decided row is clean"                      0 @(Get-HcdRowProblems -Row $okRow).Count
$bad = $okRow.Clone(); $bad.Remove('ExpectedSpeed')
Assert "no ExpectedSpeed is refused"                 $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'no ExpectedSpeed')
$bad = $okRow.Clone(); $bad.Remove('Expect')
Assert "no Expect is refused"                        $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'no Expect lines')
$bad = $okRow.Clone(); $bad.Expect = @()
Assert "an empty Expect is refused"                  $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'no Expect lines')
$bad = $okRow.Clone(); $bad.ExpectBySwitch = @{ '0,1,2' = @('zero fatal controller status') }
Assert "ExpectBySwitch is refused"                   $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'ExpectBySwitch')
$bad = $okRow.Clone(); $bad.Expect = @('advance endpoints opened >= 1', 'zero endpoint speed mismatches')
Assert "the usbport mismatch counter is refused by name" $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match "retires - usbport's High-Speed lie")
$bad = $okRow.Clone(); $bad.Expect = @('advance endpoints opened >= 1', 'inert endpoint speed mismatches because the HCD has none')
Assert "...and refused as inert too"                 $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'retires')
$bad = $okRow.Clone(); $bad.ExpectByTarget = @{ '2b' = @('advance topology: TT pairs agreeing with usbport') }
Assert "a retired label in ExpectByTarget is refused" $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'row r \[2b\].*retires')
$bad = $okRow.Clone(); $bad.Expect = @('advance endpoints opened >= 1', 'zero vhub hubs created')
Assert "any vhub label is refused"                   $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'virtual hub')
$bad = $okRow.Clone(); $bad.Expect = @('advance endpoints opened >= 1', 'identity endpoint opens seen == endpoints opened')
Assert "a retired label inside an identity is refused" $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'endpoint opens seen')
$bad = $okRow.Clone(); $bad.Expect = @('advance endpoints opened >= 1', 'advance port speed decoded - high speed == 1')
Assert "a speed line written by hand is refused"     $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'by hand')
Assert "a retired label in Always is refused"        $true (@(Get-HcdSetProblems -Matrix @{ Driver = 'hcd'; Always = @('zero endpoint opens refused - malformed call'); Groups = @() }).Count -eq 1)
foreach ($sp in @('SS', 'fs ', 480, @{}, @{ FS = 0 }, @{ XS = 1 }, @{ FS = 'two' })) {
    $bad = $okRow.Clone(); $bad.ExpectedSpeed = $sp
    Assert ("ExpectedSpeed '{0}' is refused" -f $(if ($sp -is [hashtable]) { "@{" + (($sp.Keys | ForEach-Object { "$_=$($sp[$_])" }) -join ';') + "}" } else { $sp })) $true (@(Get-HcdRowProblems -Row $bad).Count -gt 0)
}
$bad = $okRow.Clone(); $bad.ClaimLabel = ' '
Assert "an empty ClaimLabel is refused"              $true ((@(Get-HcdRowProblems -Row $bad) -join ' ') -match 'ClaimLabel is empty')

Write-Host "--- the speed lines the harness writes ---"
$hsLines = @(Get-HcdSpeedExpectationTexts -ExpectedSpeed 'HS')
Assert "HS: seven lines"                             7 $hsLines.Count
Assert "HS: the port speed advances by exactly one"  $true ($hsLines -contains 'advance port speed decoded - high speed == 1')
Assert "HS: the slot speed advances by exactly one"  $true ($hsLines -contains 'advance slot context speed - high speed == 1')
Assert "HS: full speed must not move"                $true (($hsLines -contains 'zero port speed decoded - full speed') -and ($hsLines -contains 'zero slot context speed - full speed'))
Assert "HS: low speed must not move"                 $true (($hsLines -contains 'zero port speed decoded - low speed') -and ($hsLines -contains 'zero slot context speed - low speed'))
Assert "HS: no slot/port disagreement"               $true ($hsLines -contains 'zero slot speed disagreeing with port speed')
Assert "a counted form: FS == 11"                    $true (@(Get-HcdSpeedExpectationTexts -ExpectedSpeed @{ FS = 11 }) -contains 'advance slot context speed - full speed == 11')

Write-Host "--- a wrong speed is a FAIL, never a silence (usb-kbd/hs) ---"
$kbdHs = Get-HcdRow $mxHcd 'usb-kbd/hs'
$hsOk = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoints opened' = 1; 'select endpoints requested' = 1
           'port speed decoded - high speed' = 1; 'slot context speed - high speed' = 1 }
Assert "HS keyboard at HS, bound: PASS"              "PASS" (Get-HcdOutcome $kbdHs (New-HcdDelta $hsOk))
$v = $hsOk.Clone(); $v.Remove('port speed decoded - high speed'); $v.Remove('slot context speed - high speed')
$v['port speed decoded - full speed'] = 1; $v['slot context speed - full speed'] = 1
Assert "HS keyboard reported FS: FAIL"               "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v))
Assert "...naming the speed lines"                   $true ((Get-HcdOutcome $kbdHs (New-HcdDelta $v) -Why) -match 'port speed decoded - high speed == 1')
$v2 = $hsOk.Clone(); $v2.Remove('slot context speed - high speed'); $v2['slot context speed - full speed'] = 1; $v2['slot speed disagreeing with port speed'] = 1
Assert "port right, Slot Context wrong: FAIL"        "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v2))
$v3 = $hsOk.Clone(); $v3['slot speed disagreeing with port speed'] = 1
Assert "a disagreement alone: FAIL"                  "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v3))
$v4 = $hsOk.Clone(); $v4.Remove('port speed decoded - high speed'); $v4.Remove('slot context speed - high speed')
Assert "no speed counted at all: FAIL, not PASS"     "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v4))
$v5 = $hsOk.Clone(); $v5['port speed decoded - high speed'] = 2; $v5['slot context speed - high speed'] = 2
Assert "counted twice: FAIL"                         "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v5))
# Unbound: the speed still decides. A right speed and no bind is the OS's
# silence; a wrong speed and no bind is this driver's defect.
$nb = $hsOk.Clone(); $nb['endpoints opened'] = 0; $nb['select endpoints requested'] = 0
Assert "right speed, never bound: NODRIVER"          "NODRIVER" (Get-HcdOutcome $kbdHs (New-HcdDelta $nb))
$nbWrong = $v.Clone(); $nbWrong['endpoints opened'] = 0; $nbWrong['select endpoints requested'] = 0
Assert "wrong speed, never bound: FAIL, not NODRIVER" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $nbWrong))
$nbSilent = $v4.Clone(); $nbSilent['endpoints opened'] = 0; $nbSilent['select endpoints requested'] = 0
Assert "no speed counted, never bound: FAIL"         "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $nbSilent))
Assert "...which without the enumeration labels would read NODRIVER" "NODRIVER" (Get-HcdOutcome $kbdHs (New-HcdDelta $nbSilent) -NoEnumerationLabels)
$kbdFsH = Get-HcdRow $mxHcd 'usb-kbd/fs'
$fsOk = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoints opened' = 1; 'select endpoints requested' = 1
           'port speed decoded - full speed' = 1; 'slot context speed - full speed' = 1 }
Assert "FS keyboard at FS: PASS"                     "PASS" (Get-HcdOutcome $kbdFsH (New-HcdDelta $fsOk))
Assert "FS keyboard reported HS (the miniport's lie): FAIL" "FAIL" (Get-HcdOutcome $kbdFsH (New-HcdDelta $hsOk))

Write-Host "--- the HCD's refusal set, its URB refusals and its identity ---"
$f3h = $nb.Clone(); $f3h['select endpoints requested'] = 1; $f3h['select endpoints refused'] = 1; $f3h['endpoint refusals - ring pool'] = 1
Assert "a ring-pool refusal, nothing opened: FAIL, not NODRIVER" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $f3h))
Assert "...naming it"                                $true ((Get-HcdOutcome $kbdHs (New-HcdDelta $f3h) -Why) -match 'refused.*ring pool \+1')
foreach ($lbl in @($script:HcdRefusalLabelsPermanent)) {
    $v = $nb.Clone(); $v['select endpoints requested'] = 1; $v['select endpoints refused'] = 1; $v[$lbl] = 1
    Assert ("'{0}' with no bind: FAIL, not NODRIVER" -f $lbl) "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $v))
}
$cf = $hsOk.Clone(); $cf['endpoint configure failures'] = 1
Assert "an open then a configure failure: FAIL, not PASS" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $cf))
$sf = $hsOk.Clone(); $sf['selects failed'] = 1
Assert "endpoints opened, then the select failed at the device: FAIL, not PASS" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $sf))
$sn = $nb.Clone(); $sn['selects failed'] = 1
Assert "a malformed select, nothing counted: FAIL, not NODRIVER" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $sn))
$ub = $hsOk.Clone(); $ub['URBs refused - malformed'] = 1
Assert "a malformed-URB refusal: FAIL"               "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $ub))
$id = $hsOk.Clone(); $id['select endpoints requested'] = 2
Assert "an endpoint requested and neither opened nor refused: FAIL" "FAIL" (Get-HcdOutcome $kbdHs (New-HcdDelta $id))
$ud = New-HcdDelta $hsOk; $ud.Values.Remove((Resolve-CounterLabel -Table $hcdTable -Label 'endpoint refusals - params'))
Assert "an unread HCD refusal counter: ERROR"        "ERROR" (Get-HcdOutcome $kbdHs $ud)

Write-Host "--- hub rows: the claim is the bus's hub start, not a function driver's pipe ---"
$hubFs = Get-HcdRow $mxHcd 'usb-hub/fs'
Assert "usb-hub/fs names its claim"                  "hubs started by the bus" (Get-RowClaimLabel -Row $hubFs)
Assert "a row with no ClaimLabel reads endpoints opened" "endpoints opened" (Get-RowClaimLabel -Row $kbdHs)
$hubOk = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'hubs started by the bus' = 1; 'port speed decoded - full speed' = 1; 'slot context speed - full speed' = 1
            'topology: hub descriptors folded' = 1; 'topology: hub slots marked' = 1 }
Assert "hub served by the bus: PASS"                 "PASS" (Get-HcdOutcome $hubFs (New-HcdDelta $hubOk))
$hubTt = $hubOk.Clone(); $hubTt['topology: TT pairs programmed'] = 1
Assert "an FS hub on a root port with a TT programmed: FAIL" "FAIL" (Get-HcdOutcome $hubFs (New-HcdDelta $hubTt))
$hub26 = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'port speed decoded - full speed' = 1; 'slot context speed - full speed' = 1 }
Assert "Phase 26's hub, offered with no driver: NODRIVER" "NODRIVER" (Get-HcdOutcome $hubFs (New-HcdDelta $hub26))
$hubNoFold = $hubOk.Clone(); $hubNoFold['topology: hub descriptors folded'] = 0
Assert "hub started, descriptor never folded: FAIL"  "FAIL" (Get-HcdOutcome $hubFs (New-HcdDelta $hubNoFold))
$hubFsDefault = $hubFs.Clone(); $hubFsDefault.Remove('ClaimLabel')
Assert "...which the default claim would misread as NODRIVER" "NODRIVER" (Get-HcdOutcome $hubFsDefault (New-HcdDelta $hubNoFold))
$churnH = Get-HcdRow $mxHcd 'usb-hub/churn'
$churn27 = @{ 'devices addressed' = 11; 'slots enabled' = 11; 'hubs started by the bus' = 6
              'port speed decoded - full speed' = 11; 'slot context speed - full speed' = 11
              'topology: hub descriptors folded' = 6; 'topology: hub slots marked' = 6; 'topology: behind-hub opens' = 10; 'topology: behind-hub devices addressed' = 10
              'endpoints opened' = 5; 'select endpoints requested' = 5 }
Assert "churn, five tiers deep and the tier-5 mouse addressed: PASS" "PASS" (Get-HcdOutcome $churnH (New-HcdDelta $churn27))
$churnUnbound = $churn27.Clone(); $churnUnbound['topology: behind-hub opens'] = 9; $churnUnbound['endpoints opened'] = 4; $churnUnbound['select endpoints requested'] = 4
Assert "churn with the tier-5 mouse addressed but not bound: FAIL" "FAIL" (Get-HcdOutcome $churnH (New-HcdDelta $churnUnbound))
$churnHubShort = $churn27.Clone(); $churnHubShort['hubs started by the bus'] = 5
Assert "churn with a hub of the chain never started: FAIL" "FAIL" (Get-HcdOutcome $churnH (New-HcdDelta $churnHubShort))
$churnVhub = $churn27.Clone(); $churnVhub['devices addressed'] = 10; $churnVhub['slots enabled'] = 10
$churnVhub['port speed decoded - full speed'] = 10; $churnVhub['slot context speed - full speed'] = 10; $churnVhub['topology: behind-hub devices addressed'] = 9
Assert "churn stopping at the virtual hub's tier: FAIL" "FAIL" (Get-HcdOutcome $churnH (New-HcdDelta $churnVhub))
Assert "churn on Phase 26's bus (hub only): FAIL"    "FAIL" (Get-HcdOutcome $churnH (New-HcdDelta $hub26))
$churnTt = $churn27.Clone(); $churnTt['topology: TT pairs programmed'] = 1
Assert "churn with a TT programmed on QEMU: FAIL (the inert claim broke)" "FAIL" (Get-HcdOutcome $churnH (New-HcdDelta $churnTt))

Write-Host "--- the audio row keeps its per-target guest facts ---"
$audio = Get-HcdRow $mxHcd 'usb-audio/fs'
$aud = @{ 'devices addressed' = 1; 'slots enabled' = 1; 'endpoints opened' = 2; 'select endpoints requested' = 2
          'port speed decoded - full speed' = 1; 'slot context speed - full speed' = 1 }
Assert "audio on 2b, idle: PASS"                     "PASS" (Get-HcdOutcome $audio (New-HcdDelta $aud) -TargetKey '2b')
$audMiss = $aud.Clone(); $audMiss['iso missed service errors'] = 1
Assert "audio on 2b with a missed service: FAIL"     "FAIL" (Get-HcdOutcome $audio (New-HcdDelta $audMiss) -TargetKey '2b')

Write-Host "--- the broken HCD set fails a healthy HS keyboard ---"
$kbdBroken = Get-HcdRow $mxHcdBroken 'usb-kbd/hs-broken'
Assert "a healthy HS keyboard under the broken set: FAIL" "FAIL" (Get-HcdOutcome $kbdBroken (New-HcdDelta $hsOk) -Matrix $mxHcdBroken)
$brokenNoAlways = @{ Driver = 'hcd'; Always = @('advance devices addressed'); Groups = @() }
Assert "...on its ExpectedSpeed alone"               "FAIL" (Get-HcdOutcome @{ Name = 'x'; ExpectedSpeed = $kbdBroken.ExpectedSpeed; Expect = @('advance endpoints opened >= 1') } (New-HcdDelta $hsOk) -Matrix $brokenNoAlways)

Write-Host "--- matrix-hcd.psd1 carries matrix.psd1's device population unchanged ---"
$mxMini = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "matrix.psd1")
function Get-PopulationKey {
    param($Group, $Row)
    $parts = @($Group.Name, [string]$Group.Pump, $Row.Name, $Row.Model, [string]$Row.AddArgs, [string]$Row.Child, [string]$Row.Settle,
               [string]$Row.NeedsNetdev, [string]$Row.NeedsChardev, (@($Row.MayWedgeGuest) -join ','))
    foreach ($k in @('ExcludedOnTarget', 'ExpectNoDriver')) {
        if ($Row.ContainsKey($k)) { $parts += (($Row[$k].Keys | Sort-Object | ForEach-Object { "{0}={1}" -f $_, $Row[$k][$_] }) -join ';') } else { $parts += "" }
    }
    if ($Row.ContainsKey('Steps')) { $parts += (($Row.Steps | ForEach-Object { "{0}|{1}|{2}|{3}" -f $_.Do, $_.Spec, $_.Id, $_.Wait }) -join ';') } else { $parts += "" }
    return ($parts -join '#')
}
$popMini = @(); foreach ($g in $mxMini.Groups) { foreach ($r in $g.Rows) { $popMini += (Get-PopulationKey $g $r) } }
$popHcd = @(); foreach ($g in $mxHcd.Groups) { foreach ($r in $g.Rows) { $popHcd += (Get-PopulationKey $g $r) } }
Assert "the same number of rows"                     $popMini.Count $popHcd.Count
$popDiff = @(Compare-Object -ReferenceObject $popMini -DifferenceObject $popHcd -SyncWindow 0)
Assert "every row's population fields, in order, unchanged" 0 $popDiff.Count
if ($popDiff.Count -gt 0) { $popDiff | Select-Object -First 2 | ForEach-Object { Write-Host ("    {0} {1}" -f $_.SideIndicator, $_.InputObject) } }

Write-Host "--- the HCD's identity: the counter block's start, size and VA lines ---"
$cbLog = Join-Path $env:TEMP ("xhci98-selftest-cb-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".log")
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001'
    'xhci98: counters size=00000468')
Assert "start and size alone are no identity"        $null (Find-CounterBlockIdentity -DebugconLog $cbLog).Va
Add-Content -LiteralPath $cbLog -Encoding ascii -Value 'xhci98: counters VA low=81A2C400'
$cb = Find-CounterBlockIdentity -DebugconLog $cbLog
Assert "x86: the low line completes the record"      "81A2C400" $cb.Va
Assert "...with its size"                            0x468 $cb.Size
Assert "...and its start number"                     1 ([int]$cb.Start)
Assert "...and its driver"                           "hcd" $cb.Driver
Assert "one load is no span"                         $false $cb.Spans
Assert "Find-DriverIdentity hcd reads the same"      "81A2C400" (Find-DriverIdentity -Driver hcd -DebugconLog $cbLog).Va
Assert "the miniport reader finds nothing in it"     $null (Find-DriverIdentity -Driver miniport -DebugconLog $cbLog).Va
Assert "a drift check on an unchanged log is clean"  "" (Get-ExtensionIdentityDrift -Ident $cb -DebugconLog $cbLog)
# A stop and start keeps the FDO, so the VA repeats with a new start number.
Add-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000002'
    'xhci98: counters size=00000468'
    'xhci98: counters VA low=81A2C400')
Assert "a restart at the same VA is a span"          $true (Find-CounterBlockIdentity -DebugconLog $cbLog).Spans
Assert "...and the drift check says so"              $true ((Get-ExtensionIdentityDrift -Ident $cb -DebugconLog $cbLog) -match 'more than one driver load|restarted')
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000002', 'xhci98: counters size=00000468', 'xhci98: counters VA low=81A2C400')
Assert "a new start number alone is drift"           $true ((Get-ExtensionIdentityDrift -Ident $cb -DebugconLog $cbLog) -match 'restarted.*start 1 -> 2')
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000468', 'xhci98: counters VA low=81A2C400'
    'xhci98: counters start=00000001', 'xhci98: counters size=0000046C', 'xhci98: counters VA low=81A2C400')
Assert "two sizes are a span (two binaries)"         $true (Find-CounterBlockIdentity -DebugconLog $cbLog).Spans
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000468', 'xhci98: counters VA low=81A2C400'
    'xhci98: counters start=00000001', 'xhci98: counters size=00000468', 'xhci98: counters VA low=81A2C400')
Assert "a repeated record is a span (a second lifetime)" $true (Find-CounterBlockIdentity -DebugconLog $cbLog).Spans
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000468', 'xhci98: counters VA low=01A2C400')
Assert "a non-kernel x86 address is not taken"       $null (Find-CounterBlockIdentity -DebugconLog $cbLog).Va
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000530', 'xhci98: counters VA low=CE2F5DC8')
Assert "amd64: the low half alone is no identity"    $null (Find-CounterBlockIdentity -DebugconLog $cbLog -Arch amd64).Va
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000530'
    'xhci98: counters VA high=FFFFFADF', 'xhci98: counters VA low=CE2F5DC8')
Assert "amd64: the completed pair is read"           "FFFFFADFCE2F5DC8" (Find-CounterBlockIdentity -DebugconLog $cbLog -Arch amd64).Va
Set-Content -LiteralPath $cbLog -Encoding ascii -Value @(
    'xhci98: counters start=00000001', 'xhci98: counters size=00000530'
    'xhci98: counters VA high=0000FADF', 'xhci98: counters VA low=CE2F5DC8')
Assert "amd64: a high half without bit 31 is not taken" $null (Find-CounterBlockIdentity -DebugconLog $cbLog -Arch amd64).Va
Remove-Item -LiteralPath $cbLog -Force -ErrorAction SilentlyContinue
Assert "no log: no identity"                         $null (Find-CounterBlockIdentity -DebugconLog $cbLog).Va

Write-Host "--- the HCD's table names, and its size in the freshness messages ---"
Assert "x86 HCD table"                               "offsets-hcd.txt" (Get-CounterTableFileName -Driver hcd -Arch x86)
Assert "amd64 HCD table"                             "offsets-hcd-amd64.txt" (Get-CounterTableFileName -Driver hcd -Arch amd64)
Assert "the miniport's table is unchanged"           "offsets.txt" (Get-CounterTableFileName -Driver miniport -Arch x86)
$threw = ""
try { Import-CounterTable -OffsetsFile (Join-Path $env:TEMP "xhci98-no-such-table.txt") -Driver hcd | Out-Null } catch { $threw = $_.Exception.Message }
Assert "a missing HCD table names the regeneration"  $true ($threw -match 'gen-offsets\.ps1 -Driver hcd')
Assert "the HCD's size is called by its own name"    "counters size" (Get-DriverSizeName -Driver hcd)
$stampHcd = @{ Port = 56694; PortFree = $true; Image = 'D:\vm\fresh-2a.img'; ImageExists = $true; DebugconLog = 'x.log'; IdentSize = 1128; TableSizeof = 1132; SizeName = 'counters size' }
Assert "a stamp against the wrong HCD build names its size" $true ((@(Get-StampProblems @stampHcd) -join ' ') -match 'counters size=1128')
$tmpOffHcd = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-selftest-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".txt")
Set-Content -LiteralPath $tmpOffHcd -Value @("SIZEOF 1132", "F0 0") -Encoding ascii
$threw = ""
try { Assert-OffsetsFresh -OffsetsFile $tmpOffHcd -ExtensionSizeFromTrace 1128 -SizeName 'counters size' | Out-Null } catch { $threw = $_.Exception.Message }
Remove-Item -LiteralPath $tmpOffHcd -Force -ErrorAction SilentlyContinue
Assert "stale HCD offsets are refused by name"       $true ($threw -match 'STALE OFFSETS.*counters size=1128')

Write-Host ""
if ($failures -eq 0) {
    Write-Host ("selftest: {0} checks, all passed" -f $checks)
    exit 0
}
Write-Host ("selftest: {0} checks, {1} FAILED" -f $checks, $failures)
exit 1
