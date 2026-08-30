<#
.SYNOPSIS
Batch 11-V stage G (roadmap task 11-V.1) - the STOP half of the lifecycle with
traffic: commit an orderly guest shutdown while three classes are transferring,
keep an armed-callback window open across it, and read the teardown's own
witnesses out of the stopped guest.

.DESCRIPTION
THIS SCRIPT DRIVES AN ALREADY-RUNNING GUEST, and it does not attach anything.
The bus population and the guest-side load are stage F's machinery and are
reused verbatim:

    powershell -File scripts\vm-matrix\soak-11v.ps1 -Monitor 55555 `
        -DebugconLog vm\win98-debugcon.log -Target 2a `
        -Classes "hid,storage,net" -Cycles 0 -LoadSeconds 900 -NoRepin

soak holds the population and prints the identity every 15 s; this script is
pointed at the same monitor when the load is running.  Two monitor clients is
the limit on these launchers (a third gets an empty reply, which is a counter
read that FAILS), so soak plus this script is the whole budget: hidpump-11v.ps1
must have finished before this starts.

WHAT STAGE G OWES AND WHAT THIS MEASURES.  Of task 11-V.1's clauses only two are
reachable here - the orderly shutdown with traffic in flight, and the restart
after it.  Windows 98 disable/re-enable bugchecks that guest through every door,
`ResumeController` is measured unreachable in QEMU and needs a bare-metal
Windows 2000 machine this project has no vehicle for,
Windows 2000's disable/re-enable was exercised in stage B4, and controller
invalidation has no trigger in this vehicle.  This script covers the stop; the
restart is another soak run after the relaunch, whose settled identity is the
reading.

THREE THINGS IT DOES THAT A HAND-DRIVEN STOP DOES NOT.

1. IT REFUSES TO FIRE THE STOP AT AN IDLE BUS.  "With traffic in flight" is the
   whole difference between this stage and the Phase 4 lifecycle runs, and a
   stop fired after the guest-side load died is not the clause - it is stage F's
   own `+0`-is-not-a-pass error moved one stage along.  So `transfers submitted`
   is sampled first and the run stops unless it is moving.

2. IT CHURNS A DEVICE ACROSS THE WHOLE SHUTDOWN, not once at the click.  The run
   sheet says to have the stop ready and fire it into a plug; that shape assumes
   the stop reaches the miniport when the operator commits it, and the measured
   shutdown says otherwise - the guest runs its own shutdown for SECONDS before
   `cb SuspendController` arrives, by which time a single plug's port reset and
   its Enable Slot / Address Device commands are long retired.  So the arming is
   a loop: a `usb-hub` added and removed on a free root port, -ChurnIntervalMs
   after each add and each del plus the monitor's own turnaround (about two
   seconds per add/del pair at the default), from the moment the stop is
   committed until the teardown appears in the debug console.  A hub is used
   because it needs no class install on
   either target (stage D attached them on 2a with no wizard) and because it is
   NOT the storage device - pulling that one mid-write raises Windows 98's
   full-screen Disk Write Error box, which owns the console absolutely and would
   swallow the shutdown itself.

   This still does not GUARANTEE the overlap, and the report says so.  What it
   guarantees is that the window was open across the whole teardown rather than
   for one moment several seconds before it.

3. IT READS THE STOP-TIME COUNTERS OUT OF THE STOPPED GUEST.  Both stage G
   launchers carry `-no-shutdown`, so a guest that powers itself off leaves QEMU
   holding the machine with its memory intact, and Windows 2000 without ACPI
   does not even get that far - it ends on "It is now safe to turn off your
   computer" with the VM still running.  Either way the miniport extension is
   still in RAM and `x/Nwx` still reads it.  That matters most on Windows 98,
   where the run sheet's advice is to read these from the trace because the next
   start zeroes the extension: it is only the NEXT START that zeroes it, and
   there is a window between the teardown and that start where the counters are
   simply readable.  Both channels are reported here, and they are independent.

WHY THE TEARDOWN IS DETECTED FROM THE LOG AND NOT FROM `info status`.  Windows
2000 on `-machine pc,acpi=off` never asks QEMU to power off at all, so `info
status` says `running` through the entire shutdown and past it.  `cb
StopController` in the debug console is the event this stage is about, and it is
the same witness on both targets.  Only lines written AFTER the commit are
considered - Windows 98 idle-suspends produce `cb SuspendController` too, and on
an image without task 11-V.6's registry value there can be one every second.

.PARAMETER Commit
How the stop is delivered.  `sendkey` sends Return into a shutdown dialog the
operator has already brought up and focused - the precise, reproducible form,
and the one to use.  `powerdown` sends ACPI `system_powerdown`, which is only
meaningful on a launcher whose machine has ACPI (2a does, 2b's `acpi=off` does
not) and which Windows 98 SWALLOWS if a critical-error box is up.  `none` fires
nothing and starts the churn immediately, for an operator clicking OK by hand.

.PARAMETER ChurnBus
Which QEMU USB bus the churn is delivered on.  Defaults to `xhci.0`, which is
the bus every stage-G/H run used and the only one that existed when this script
was written.

It became a parameter for roadmap task 12.5, whose whole content is a control:
run the SAME churn against Microsoft's own EHCI miniport in the SAME guest, so
that the miniport carrying it is the only difference between the two legs.  A
launcher that also carries `-device usb-ehci,id=ehci` then takes `-ChurnBus
ehci.0` for the control leg.  Driving the control leg from the monitor by hand
instead would have made the harness a second difference between the legs, which
is what this parameter removes.

Prove the other stack's non-involvement the way the batch 8-V control did:
`usb_xhci_xfer_start` and `usb_xhci_slot_enable` must stay at 0 while the churn
is on `ehci.0`.

.PARAMETER SelfTest
Run the log scanner against -DebugconLog and print what it finds, touching no
monitor.  This is how the scanner was validated against the recorded teardowns
of earlier stages before a boot was spent on it.
#>
[CmdletBinding()]
param(
    [int]$Monitor = 0,
    [Parameter(Mandatory = $true)][string]$DebugconLog,
    [string]$Target = "",
    [string]$OutDir = "",
    [ValidateSet('none', 'sendkey', 'powerdown')][string]$Commit = 'sendkey',
    [int]$ChurnPort = 4,
    [string]$ChurnDevice = "usb-hub",
    [string]$ChurnBus = "xhci.0",
    [int]$ChurnIntervalMs = 600,
    [switch]$NoChurn,
    [int]$TrafficSampleSeconds = 12,
    [switch]$SkipTrafficGate,
    [int]$TimeoutSeconds = 420,
    [int]$SettleAfterStopSeconds = 8,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\monitor.ps1")
. (Join-Path $PSScriptRoot "lib\qemu.ps1")
. (Join-Path $PSScriptRoot "lib\counters.ps1")

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
function Resolve-RepoPath {
    param([string]$P)
    if ([string]::IsNullOrWhiteSpace($P)) { return "" }
    if ([IO.Path]::IsPathRooted($P)) { return $P }
    return (Join-Path $repo $P)
}
$DebugconLog = Resolve-RepoPath $DebugconLog
if ($OutDir -eq "") { $OutDir = Join-Path $repo "out\phase11-stageG" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# ------------------------------------------------------------------ readings ---

# The teardown's OWN witnesses.  Five drafts of the run sheet looked at the
# callback end - what a stale timer does when it arrives late - and every one of
# them named an oracle that ordinary successful completion also produces.  The
# question is what the QUIESCE does when it finds an operation still armed, and
# these two are what it writes.
$ARMED = @('commands abandoned', 'RH operations retired by the quiesce')

# The failure-shaped zeros.  A witness above moving is half the clause; these
# staying at zero is the other half, and neither alone is the reading.
$CLEAN = @('RH resumes abandoned', 'command timer failures', 'RH resets not confirmed',
           'DMA failures closed', 'suspend failures', 'teardowns without a stop',
           'devices abandoned without evidence')

# The shape of the stop itself.  `port teardowns skipped - suspended` is
# EXPECTED TO MOVE and a zero is the suspicious reading: the stop arrives on a
# controller the suspend has already halted, PORTSC is unwritable once it stops,
# so the port pass cannot run.  What the counter proves is that the skip is
# counted rather than silent - the defect a Phase 4 task 8 review round found.
$SHAPE = @('suspends', 'teardowns', 'port teardowns skipped - suspended',
           'RH ports driven out of U3', 'resume reinitialisations', 'resume failures')

$IDENTITY = @('transfers submitted', 'transfers completed', 'transfers cancelled',
              'transfers aborted')

$report = @()
function Add-Line { param([string]$S) $script:report += $S; Write-Host $S }

# ------------------------------------------------------------- the verdict ---
#
# **Five outcomes, five exit codes, because they mean different things and a
# wrapper that cannot tell them apart will read the wrong one as a pass.** This
# script prints a clause-by-clause verdict a human can read; these are the same
# conclusions in a form a caller can act on, since a run whose failures only
# scrolled past is a run that reports success.
#
#   0  every clause this run is asked to observe was observed
#   1  FAILED       - a clause was observed and came back wrong (no stop reached
#                     the driver; a failure-shaped counter is nonzero)
#   2  INVALID      - the run could not be read (the stop-time counter read
#                     failed, the monitor errored), so the clauses are unknown
#                     rather than passed
#   3  UNESTABLISHED - a clause this stage OWES was not established. Two ways
#                     in, and both belong here rather than under 1: the
#                     armed-callback window was set up and not entered, which
#                     task 11-V.1's record (docs/contributing/runs/run-11v.md,
#                     stage G) treats as attempted-and-not-witnessed and is an
#                     honest outcome of this vehicle; or a bypass switch
#                     (-NoChurn, -SkipTrafficGate) removed the clause's setup,
#                     so this run never put the question. The clause is owed
#                     either way - that is what makes both cases the same
#                     verdict - and neither is a defect, nor a pass, which is
#                     the whole reason this is not 0. The report distinguishes
#                     them in words; the exit code deliberately does not.
#   4  ABORTED      - the run ended on a terminating error and is incomplete.
#
# Precedence is 1, then 2, then 3 - the strongest statement the run supports.
# The report is written before any of them is taken.
$script:failed    = @()
$script:invalid   = @()
$script:attempted = @()

# One spelling of "write the evidence out" - the ordinary end and the trap
# below both need it.
#
# It cannot throw, for the reason given in soak-11v.ps1: the trap calls it, and
# a second exception there would both bury the original error and skip the exit
# code the trap exists to produce.
$script:reportPath = ""
function Save-Report {
    try {
        if ($OutDir -eq "") { return }
        $script:reportPath = Join-Path $OutDir ("stageG-{0}-stop.txt" -f $(if ($Target -ne "") { $Target } else { "run" }))
        Set-Content -LiteralPath $script:reportPath -Value $script:report -Encoding ascii
        Write-Host ""
        Write-Host ("report: {0}" -f $script:reportPath)
    } catch {
        Write-Host ""
        Write-Host ("*** THE REPORT COULD NOT BE WRITTEN: {0}" -f $_.Exception.Message)
        Write-Host "    The console output above is the only record of this run - keep it."
    }
}

# Same argument as soak-11v.ps1's: this script drives a shutdown, so the run
# that dies mid-way is exactly the one whose partial evidence is worth a boot.
# A trap rather than try/finally so the body keeps the shape it was verified in.
trap {
    Add-Line ""
    Add-Line ("*** THE RUN ENDED ON AN ERROR AND IS INCOMPLETE: {0}" -f $_.Exception.Message)
    Add-Line  "    Everything above it was measured; everything after it was not reached."
    Save-Report
    Write-Host "VERDICT: ABORTED"
    exit 4
}

function Write-Block {
    param([string]$Title, $Snapshot, [string[]]$Names, $Before = $null)
    Add-Line ("  {0}" -f $Title)
    foreach ($n in $Names) {
        $f = Resolve-CounterLabel -Table $script:table -Label $n
        $now = $Snapshot.Values[$f]
        if ($null -ne $Before) {
            $was = $Before.Values[$f]
            Add-Line ("    {0,-42} {1,12}   (was {2}, moved {3})" -f $n, $now, $was, ($now - $was))
        } else {
            Add-Line ("    {0,-42} {1,12}" -f $n, $now)
        }
    }
}

if ($SelfTest) {
    Write-Host ""
    Write-Host ("--- self-test: scanning {0} whole, with no monitor" -f $DebugconLog)
    $t = Find-Teardown -Path $DebugconLog -From 0
    Write-Host ("  lines                 {0}" -f $t.Total)
    Write-Host ("  DriverEntry           {0}" -f $t.DriverEntries)
    Write-Host ("  callback sequence     {0}" -f $(if ($t.Sequence.Count) { $t.Sequence -join " -> " } else { "(none)" }))
    Write-Host ("  suspend / stop        {0} / {1}" -f $t.HasSuspend, $t.HasStop)
    Write-Host ("  stop irql             {0}" -f $(if ($t.StopIrqls.Count) { $t.StopIrqls -join ", " } else { "(none)" }))
    Write-Host ("  suspended-stop line   {0}" -f $t.SuspendedStop)
    Write-Host ("  commands abandoned    {0} trace line(s)" -f $t.CommandsTrace.Count)
    Write-Host ("  RH ops retired        {0} trace line(s)" -f $t.RhRetiredTrace.Count)
    foreach ($l in $t.Lines) { Write-Host ("    | {0}" -f $l) }
    Write-Host ""
    return
}

if ($Monitor -le 0) { throw "-Monitor is required unless -SelfTest is given." }

# ---------------------------------------------------------------- preflight ---

Add-Line ""
Add-Line ("=== batch 11-V stage G - the stop with traffic in flight ({0})" -f $(if ($Target -ne "") { $Target } else { "target unnamed" }))
Add-Line ("started: {0}   host: {1}   monitor: {2}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $env:COMPUTERNAME, $Monitor)
Add-Line ""

$script:table = Import-CounterTable
$ident = Find-ExtensionIdentity -DebugconLog $DebugconLog
if ($null -eq $ident.Va) {
    throw ("no extension address in {0} - is the driver up, and is it the QEMU build? Since task 13-L.1 that trace exists in no other flavour." -f $DebugconLog)
}
if ($ident.Spans) {
    throw ("{0} spans more than one driver load or binary (VAs: {1}; sizes: {2}). Relaunch so this stop has one load behind it - a before/after pair across a restart is void." -f `
        $DebugconLog, ($ident.AllVas -join ", "), ($ident.AllSizes -join ", "))
}
if ($null -eq $ident.Size) {
    # Without it there is no cross-check at all, and every counter below would
    # be read from an offset table nothing had confirmed describes this binary.
    throw ("{0} carries no MiniPortExtensionSize line, so the offset table cannot be checked against the running driver. That is the one check this stage may not skip." -f $DebugconLog)
}
Assert-OffsetsFresh -OffsetsFile (Join-Path $PSScriptRoot "offsets.txt") -ExtensionSizeFromTrace $ident.Size | Out-Null
Add-Line ("binary identity: MiniPortExtensionSize={0} (0x{0:X8}) = offsets SIZEOF; extension VA {1}" -f $ident.Size, $ident.Va)

$alive = Test-GuestAlive -Port $Monitor
Add-Line ("guest: {0} - {1}" -f $alive.Verdict, $alive.Detail)
if (-not $alive.Alive) { throw ("the guest is not executing before the stop was even fired: {0}" -f $alive.Why) }

$busReply = Get-MonitorText -Port $Monitor -Command "info usb"
if ($null -eq $busReply) {
    # A third monitor client behind two others gets no complete reply, and
    # "0 device lines" would read as an empty bus.
    Add-Line "bus population: `info usb` gave no complete reply (a third monitor client?), so it is not recorded"
} else {
    $bus = @($busReply | Where-Object { $_ -match 'Device' })
    Add-Line ("bus population ({0} device lines):" -f $bus.Count)
    foreach ($b in $bus) { Add-Line ("    {0}" -f $b.Trim()) }
}

# ------------------------------------------------------------- traffic gate ---

function Read-Now {
    $i = Find-ExtensionIdentity -DebugconLog $DebugconLog
    if ($i.Spans) { throw "the driver restarted mid-run - every delta across that point is void." }
    return (Read-Counters -Port $Monitor -BaseVa $i.Va -Table $script:table)
}

$submittedField = Resolve-CounterLabel -Table $script:table -Label 'transfers submitted'
$before = Read-Now
if (-not $SkipTrafficGate) {
    Start-Sleep -Seconds $TrafficSampleSeconds
    $sample = Read-Now
    $moved = $sample.Values[$submittedField] - $before.Values[$submittedField]
    $rate = [int]($moved / $TrafficSampleSeconds)
    Add-Line ("traffic gate: {0} transfers submitted in {1} s ({2}/s)" -f $moved, $TrafficSampleSeconds, $rate)
    if ($moved -le 0) {
        throw ("NOTHING IS MOVING - {0} transfers were submitted across {1} s, so the guest-side load is not running and this stop would not be the clause. Start the load, then re-run." -f `
            $moved, $TrafficSampleSeconds)
    }
    $before = $sample
} else {
    Add-Line "traffic gate: SKIPPED by -SkipTrafficGate - this run cannot claim traffic was in flight."
    # Recorded, not merely printed. The clause this script exists for is a stop
    # that lands on a BUSY bus; with the gate skipped, the run cannot say the bus
    # was busy, so its central claim is unavailable however clean everything else
    # looks. A caller reading the exit code must not get 0 for that.
    $script:attempted += "the traffic gate was skipped (-SkipTrafficGate), so 'with traffic in flight' is unverified for this run"
}

Add-Line ""
Add-Line "--- counters immediately before the stop"
Write-Block -Title "identity" -Snapshot $before -Names $IDENTITY
Write-Block -Title "the teardown's own witnesses" -Snapshot $before -Names $ARMED
Write-Block -Title "shape" -Snapshot $before -Names $SHAPE
Write-Block -Title "failure-shaped, expected 0" -Snapshot $before -Names $CLEAN
Add-Line ""

# --------------------------------------------------------- the stop, and the ---
# ------------------------------------------------------- window it lands in ---

$logMark = @(Get-Content -LiteralPath $DebugconLog).Count
Add-Line ("debug console is {0} lines at the commit - only what follows is this teardown" -f $logMark)

Reset-MonitorErrors
switch ($Commit) {
    'sendkey'   { Add-Line "committing the stop: sendkey ret into the focused shutdown dialog"
                  Send-Mon -Port $Monitor -Command "sendkey ret" -Quiet | Out-Null }
    'powerdown' { Add-Line "committing the stop: ACPI system_powerdown"
                  Send-Mon -Port $Monitor -Command "system_powerdown" -Quiet | Out-Null }
    'none'      { Add-Line "no stop committed from here - the operator is clicking it" }
}

$churnAdds = 0; $churnDels = 0; $churnRefusals = 0
$sw = [Diagnostics.Stopwatch]::StartNew()
$td = $null
$i = 0
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    $td = Find-Teardown -Path $DebugconLog -From $logMark
    if ($td.HasStop) { break }
    if ($NoChurn) { Start-Sleep -Milliseconds 500; continue }

    $i++
    $id = "armhub$i"
    # Quiet, not Send-Checked: this loop runs tens of times across a teardown
    # and its errors are counted rather than printed one by one.  The count is
    # in the report, and a refusal on every attempt would show there.
    # Short monitor windows: the default -Reply timing (900 ms idle) put more
    # than three seconds between an add and its del, and a churn that slow
    # does not keep the window open across the teardown.  A reply that never
    # completed (`$null`) is counted as a refusal, not as an add.
    $r = Send-Mon -Port $Monitor -Command ("device_add {0},id={1},bus={2},port={3}" -f $ChurnDevice, $id, $ChurnBus, $ChurnPort) -Reply -IdleMs 200 -HardMs 2000
    if ($null -eq $r -or $r -match "(?i)error|failed|cannot|unable|invalid|duplicate") { $churnRefusals++ } else { $churnAdds++ }
    Start-Sleep -Milliseconds $ChurnIntervalMs

    $td = Find-Teardown -Path $DebugconLog -From $logMark
    if ($td.HasStop) { break }

    $r = Send-Mon -Port $Monitor -Command ("device_del {0}" -f $id) -Reply -IdleMs 200 -HardMs 2000
    if ($null -eq $r -or $r -match "(?i)error|failed|cannot|unable|no such") { $churnRefusals++ } else { $churnDels++ }
    Start-Sleep -Milliseconds $ChurnIntervalMs
}
$sw.Stop()

$td = Find-Teardown -Path $DebugconLog -From $logMark
Add-Line ""
Add-Line ("--- the teardown, {0:N1} s after the commit" -f $sw.Elapsed.TotalSeconds)
if (-not $NoChurn) {
    # The bus is named because -ChurnBus makes it a variable: a leg driven on
    # ehci.0 and one driven on xhci.0 produce reports that are otherwise
    # identical in shape, and which controller carried the churn is the whole
    # discriminator of task 12.5's control.
    Add-Line ("arming churn: {0} {1} attach(es), {2} detach(es), {3} refusal(s) on {4} root port {5}" -f `
              $churnAdds, $ChurnDevice, $churnDels, $churnRefusals, $ChurnBus, $ChurnPort)
} else {
    Add-Line "arming churn: DISABLED by -NoChurn - this run cannot claim the stop landed on an armed callback."
}
Add-Line ("callback sequence: {0}" -f $(if ($td.Sequence.Count) { $td.Sequence -join " -> " } else { "(nothing reached the driver)" }))
foreach ($l in $td.Lines) { Add-Line ("    | {0}" -f $l) }
if (-not $td.HasStop) {
    Add-Line ("*** NO 'cb StopController' IN {0} s. The shutdown did not reach this miniport - which on Windows 98 is what a swallowed keypress looks like (a critical-error box owns the console), and is NOT a driver reading." -f $TimeoutSeconds)
}
Add-Line ("stop irql: {0}   'the stop arrived on a suspended controller' line: {1}" -f `
          $(if ($td.StopIrqls.Count) { $td.StopIrqls -join ", " } else { "n/a" }), $td.SuspendedStop)
Add-Line ("trace 'command: abandoned outstanding TRB': {0} line(s)" -f $td.CommandsTrace.Count)
foreach ($l in $td.CommandsTrace) { Add-Line ("    | {0}" -f $l) }
Add-Line ("trace 'port operations retired by the quiesce': {0} line(s)" -f $td.RhRetiredTrace.Count)
foreach ($l in $td.RhRetiredTrace) { Add-Line ("    | {0}" -f $l) }
Add-Line ""

# ------------------------------------------- the counters out of the stopped ---
# ------------------------------------------------------------------- guest ---

Start-Sleep -Seconds $SettleAfterStopSeconds
$status = ((Get-MonitorText -Port $Monitor -Command "info status") -join " ").Trim()
Add-Line ("after the stop, QEMU says: {0}" -f $status)
Add-Line "(-no-shutdown keeps the machine and its memory, so the stop-time extension is still readable here."
Add-Line " That is the one window in which Windows 98's stop-time counters exist - the NEXT start zeroes them.)"

$after = $null
try {
    $after = Read-Counters -Port $Monitor -BaseVa $ident.Va -Table $script:table
} catch {
    Add-Line ("*** the stop-time counter read FAILED: {0}" -f $_.Exception.Message)
    Add-Line "    The trace lines above are then the only channel, and they are read on their own."
    $script:invalid += ("the stop-time counter read failed: {0}" -f $_.Exception.Message)
}

if ($null -ne $after) {
    Add-Line ""
    Add-Line "--- counters after the stop (read out of the stopped guest)"
    Write-Block -Title "the teardown's own witnesses" -Snapshot $after -Names $ARMED -Before $before
    Write-Block -Title "shape" -Snapshot $after -Names $SHAPE -Before $before
    Write-Block -Title "failure-shaped, expected 0" -Snapshot $after -Names $CLEAN -Before $before
    Write-Block -Title "identity" -Snapshot $after -Names $IDENTITY -Before $before
    Add-Line ""
}

# ------------------------------------------------------------------ verdict ---

Add-Line "--- what this run establishes, clause by clause"

$armedMoved = @()
if ($null -ne $after) {
    foreach ($n in $ARMED) {
        $f = Resolve-CounterLabel -Table $script:table -Label $n
        if ($after.Values[$f] -gt $before.Values[$f]) { $armedMoved += ("{0} +{1}" -f $n, ($after.Values[$f] - $before.Values[$f])) }
    }
}
$armedTrace = ($td.CommandsTrace.Count + $td.RhRetiredTrace.Count) -gt 0

Add-Line ("  orderly shutdown with traffic in flight: {0}" -f $(
    if ($td.HasSuspend -and $td.HasStop) { "the teardown reached the driver in the measured shape" }
    elseif ($td.HasStop) { "a stop reached the driver, with NO suspend ahead of it - a shape to explain" }
    else { "NOT OBSERVED - no stop reached the driver" }))
if (-not $td.HasStop) {
    # The clause this whole script exists for. Note the caveat the line above
    # carries: on Windows 98 this shape is also what a shutdown blocked by a
    # modal box looks like, so it is a failed RUN rather than proof of a driver
    # defect - which is exactly why it must not exit 0.
    $script:failed += ("no 'cb StopController' within {0} s - the shutdown never reached this miniport" -f $TimeoutSeconds)
}

if ($null -ne $after) {
    $dirty = @()
    foreach ($n in $CLEAN) {
        $f = Resolve-CounterLabel -Table $script:table -Label $n
        if ($after.Values[$f] -ne 0) { $dirty += ("{0}={1}" -f $n, $after.Values[$f]) }
    }
    Add-Line ("  nothing recorded as broken in it: {0}" -f $(
        if ($dirty.Count -eq 0) { "every failure-shaped counter is 0" } else { "NO - " + ($dirty -join ", ") }))
    if ($dirty.Count -gt 0) {
        $script:failed += ("failure-shaped counters are nonzero after the stop: {0}" -f ($dirty -join ", "))
    }
}

$armedWitnesses = @()
$armedWitnesses += $armedMoved
if ($armedTrace) {
    $armedWitnesses += ("{0} trace line(s)" -f ($td.CommandsTrace.Count + $td.RhRetiredTrace.Count))
}
if ($armedWitnesses.Count -gt 0) {
    Add-Line ("  the stop landed on an armed callback: WITNESSED - {0}" -f ($armedWitnesses -join "; "))
} elseif ($NoChurn) {
    # **Not "attempted": nothing was attempted.** -NoChurn disables the arming
    # window outright, so this run never set the clause up. Recording it as
    # attempted-and-not-entered would claim a try that did not happen, and would
    # make a deliberately reduced run indistinguishable from one whose oracle
    # stayed silent - which is the distinction the ATTEMPTED code exists to draw.
    Add-Line "  the stop landed on an armed callback: NOT ATTEMPTED - -NoChurn disabled the arming window, so this run never set the clause up."
    $script:attempted += "the armed-callback clause was not attempted (-NoChurn) - the arming window was disabled"
} else {
    Add-Line ("  the stop landed on an armed callback: NOT WITNESSED. The window was held open across the whole teardown ({0} attach/detach pairs) and no witness moved, so this clause was ATTEMPTED and not entered. It is not a pass, and the clean counters above are not a substitute for one." -f $churnDels)
    $script:attempted += "the armed-callback clause was attempted and not entered - no witness moved"
}

Add-Line ("  DMA ownership released: {0}" -f $(
    if ($null -eq $after) { "unread - the stop-time counter read failed" }
    else { "'DMA failures closed' = {0}" -f $after.Values[(Resolve-CounterLabel -Table $script:table -Label 'DMA failures closed')] }))

Add-Line "  restart after this shutdown: NOT THIS SCRIPT - relaunch and run soak-11v.ps1 again; its settled identity is that clause's reading."
Add-Line "  ResumeController: not reached - no resumable power transition in this vehicle, and no bare-metal Windows 2000 exists in this project; published as a limitation."
Add-Line "  restart after controller invalidation: not reached - nothing in this vehicle injects one."
Add-Line ""

$shot = Save-GuestScreenshot -Port $Monitor -Path (Join-Path $OutDir ("stageG-{0}-after-stop.ppm" -f $(if ($Target -ne "") { $Target } else { "run" })))
Add-Line ("screenshot after the stop: {0}" -f $shot)
$monErrors = Get-MonitorErrors
Add-Line ("monitor errors during the stop window: {0}" -f $monErrors)
if ($monErrors -gt 0) {
    # The monitor is the only channel to the guest, so an error on it means a
    # read this report may have printed anyway - from an earlier sample.
    $script:invalid += ("{0} monitor error(s) during the stop window - a reading above may be a stale sample" -f $monErrors)
}

# The debug console is the channel that survives the next start, so it is copied
# out now rather than left to be overwritten by the relaunch the restart clause
# needs.
$logCopy = Join-Path $OutDir ("stageG-{0}-debugcon.log" -f $(if ($Target -ne "") { $Target } else { "run" }))
Copy-Item -LiteralPath $DebugconLog -Destination $logCopy -Force
Add-Line ("debug console copied to: {0}" -f $logCopy)
Add-Line ("finished: {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))

Add-Line ""
Add-Line "--- verdict"
if ($script:failed.Count -gt 0) {
    Add-Line "  FAILED:"
    foreach ($v in $script:failed) { Add-Line ("    - {0}" -f $v) }
}
if ($script:invalid.Count -gt 0) {
    Add-Line "  INVALID (the run could not be read, so its clauses are unknown):"
    foreach ($v in $script:invalid) { Add-Line ("    - {0}" -f $v) }
}
if ($script:attempted.Count -gt 0) {
    # "Owed", not "asked for": this list holds both a clause that was set up and
    # not entered and one a bypass switch removed before it could be put, and the
    # second was never asked. The rows themselves say which is which.
    Add-Line "  UNESTABLISHED (owed by this stage, not established here - recorded, not passed):"
    foreach ($v in $script:attempted) { Add-Line ("    - {0}" -f $v) }
}
if ($script:failed.Count -eq 0 -and $script:invalid.Count -eq 0 -and $script:attempted.Count -eq 0) {
    Add-Line "  PASS - every clause this run is asked to observe was observed."
}

Save-Report

# Report first, verdict second - see the note beside the accumulators.
if ($script:failed.Count -gt 0)    { Write-Host "VERDICT: FAILED";        exit 1 }
if ($script:invalid.Count -gt 0)   { Write-Host "VERDICT: INVALID";       exit 2 }
if ($script:attempted.Count -gt 0) { Write-Host "VERDICT: UNESTABLISHED"; exit 3 }
Write-Host "VERDICT: PASS"
exit 0
