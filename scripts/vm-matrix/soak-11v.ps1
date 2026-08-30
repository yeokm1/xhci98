<#
.SYNOPSIS
Batch 11-V stage F (roadmap task 11-V.5) - the stability matrix's mechanical
half: N unplug/replug cycles per representative class, then a sustained
concurrent multi-class load, with the transfer identity read at a settled bus.

.DESCRIPTION
Phase 10's `run-matrix.ps1` attaches each device ONCE and detaches it.  Stage F
needs the opposite shape - the same device twenty times, and several classes at
once - so this is a separate driver rather than a matrix flag.  It shares the
matrix's libraries, and therefore its traps: the offset-freshness gate, the
"a pull is only a pull if the device left" wait, the attach-state confirmation,
and the liveness probe are all the matrix's own code.

THIS SCRIPT DRIVES AN ALREADY-RUNNING GUEST.  It does not launch one, and that
is deliberate: each stage of batch 11-V has a hand launcher in scripts\local\
that already carries that stage's drives, trace event list, `-smp`, and the
throttled block backend the in-flight cancel clause needs.  Duplicating a launch
block here would mean two descriptions of the vehicle that drift apart.  Start
the launcher, then point this at its monitor.

    powershell -File scripts\vm-matrix\soak-11v.ps1 -Monitor 55557 `
        -DebugconLog vm\win2k-smp-debugcon.log -Target 2d

WHAT A PASS LOOKS LIKE, AND WHY IT IS NOT `submitted == completed`.
Phase 8 settled the identity as

    transfers submitted = transfers completed + transfers cancelled

and stage E then measured why that is only exactly true at a SETTLED bus: a live
bus parks interrupt IN requests on HID and hub status-change endpoints, and
those are neither completed nor cancelled while they sit there.  Stage E ended
at 358/348/2 with a residual gap of 8 that did not move across two readings.
So this script:

  * detaches everything and waits for the bus to empty before the final read;
  * reads the identity TWICE, and reports whether the gap MOVED between them;
  * prints the abort block beside it, because an abort in progress is the other
    legitimate reason for a gap.

A stable nonzero gap at a settled bus with a quiet abort block is a reading to
explain, not automatically a leak - and a gap that is still CHANGING means the
bus had not settled and the read was taken too early.  Neither is decided here;
both are reported with the evidence beside them.

.PARAMETER Classes
Comma-separated: hid, storage, net, audio.  Default hid,storage,net.

`net` is the PHYSICAL ASIX AX88772 by `usb-host` passthrough, resolved fresh
from `info usbhost` on every single attach.  It has to be resolved every time:
`hostaddr` is libusb's device address and it changes on every replug, so a
number captured once is wrong by cycle 2.  QEMU's emulated `usb-net` is NOT a
substitute - it is RNDIS, Windows 2000 has no in-box driver for it, and no
Phase 10 matrix row uses it.

.PARAMETER Cycles
Unplug/replug cycles per class.  Task 11-V.5 says "at least 20".

.PARAMETER LoadSeconds
How long to hold every class attached at once for the concurrent-load clause.
The GUEST-SIDE traffic for that window is started by the operator (STAGEF.BAT on
the transfer volume); this script holds the bus population and takes the
readings around it.  0 skips the load phase.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$Monitor,
    [Parameter(Mandatory = $true)][string]$DebugconLog,
    [string]$Target = "",
    [string]$Classes = "hid,storage,net",
    [int]$Cycles = 20,
    [int]$LoadSeconds = 0,
    [string]$OutDir = "",
    # The root port every cycled device is pinned to.  Windows 98 keys a USB
    # devnode by its BUS LOCATION, so a class taught at one root port raises a
    # fresh (modal) wizard at another - see run-matrix.ps1's comment at its own
    # $DutPort.  Windows 2000 does not care, but pinning costs nothing and keeps
    # one shape across targets.
    [int]$DutPort = 2,
    # The scratch medium for the cycled `usb-storage`.  Throttled on every
    # drive_add - see Add-StorageBackend.
    [string]$StorageImage = "",
    # Seconds to let a freshly attached device settle before the cycle's read.
    [int]$SettleSeconds = 6,
    # THE LOAD PHASE USES A DIFFERENT MEDIUM FROM THE CYCLE PHASE, and the
    # difference is not cosmetic.  Cycling only needs the device to enumerate and
    # open endpoints, which usbstor does on a raw unformatted disk - so the cycle
    # medium is a blank scratch file this script creates.  The LOAD phase needs
    # the guest to actually copy files onto it, which needs a FILESYSTEM.
    #
    # Rather than format one, this names the backend the stage launcher already
    # declared on its command line: `usbdisk`, pointing at the batch 8-V medium,
    # already throttled to iops-total=30 (the setting that makes an in-flight
    # cancel reachable at all - see Add-StorageBackend).  Set it to "" to fall
    # back to a drive_add of the cycle medium.
    #
    # It is usable ONCE: a `device_del` of its usb-storage destroys the backend,
    # and the load phase runs once, so that is sufficient rather than lucky.
    [string]$LoadStorageDrive = "usbdisk",
    # Read and print the counters every N cycles.  1 is what a bisect wants:
    # the 2d run read every 5 and could only say the enumeration
    # stopped "somewhere between cycle 5 and 10", which is not a cycle number.
    [int]$ReadEvery = 5,
    # THE `audio` CLASS NEEDS THIS AND CANNOT DEFAULT TO NOTHING.  Measured
    # guestlessly on QEMU 11.1.0: `device_add usb-audio` with no
    # `audiodev=` is REFUSED outright -
    #
    #   Error: no default audio driver available
    #   Perhaps you wanted to use -audio or set audiodev=cap?
    #
    # - even with exactly one `-audiodev` declared on the command line.  A
    # declared backend is not a default one.  So the id here must match the
    # `-audiodev <driver>,id=...` the stage launcher declares (`cap`, the wav
    # capture backend, in every launcher this project has written since Phase 9).
    # Send-Checked catches the refusal, so this would have failed loudly rather
    # than silently - but it would have failed all twenty audio cycles.
    [string]$AudioDev = "cap",
    # Skip the pointer re-pin after a HID attach.  Only for an unattended run
    # where nobody needs the guest's mouse back.
    [switch]$NoRepin
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
if ($OutDir -eq "") { $OutDir = Join-Path $repo "out\phase11-stageF" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$DebugconLog = Resolve-RepoPath $DebugconLog
if ($StorageImage -eq "") { $StorageImage = Join-Path $OutDir ("soak-{0}-scratch.img" -f $(if ($Target -ne "") { $Target } else { "x" })) }
$StorageImage = Resolve-RepoPath $StorageImage
if (-not (Test-Path -LiteralPath $StorageImage)) {
    $fs = [IO.File]::Create($StorageImage); $fs.SetLength(64MB); $fs.Close()
}

$classList = @($Classes -split ',' | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ -ne "" })
$known = @('hid', 'storage', 'net', 'audio')
foreach ($c in $classList) {
    if ($known -notcontains $c) { throw ("unknown class '{0}'. Known: {1}" -f $c, ($known -join ", ")) }
}

# ---------------------------------------------------------------- readings ---

# The identity's three terms, the abort block beside them, and the counters that
# say a cycle went wrong rather than merely differently.  Labels are resolved
# against the generated table, so a renamed counter fails loudly here instead of
# silently reporting a zero for a field that no longer exists.
$IDENTITY = @('transfers submitted', 'transfers completed', 'transfers cancelled')
$ABORTS   = @('transfers aborted', 'aborts before the endpoint stopped',
              'aborts racing a completion', 'aborts taken off the completion list',
              'aborts unmatched', 'transfers No-Opped by a cancellation')
$HEALTH   = @('devices addressed', 'slots enabled', 'endpoints opened',
              'endpoint halts', 'endpoint resets on a ring not halted',
              'transfers on a halted endpoint', 'command timer failures',
              'DMA failures closed', 'EP0 opens refused', 'transfers refused - ring full')

$script:table = $null
$script:baseVa = $null

function Get-Identity {
    param($Snapshot)
    $v = @{}
    foreach ($n in $IDENTITY) { $v[$n] = $Snapshot.Values[(Resolve-CounterLabel -Table $script:table -Label $n)] }
    $v['gap'] = $v['transfers submitted'] - ($v['transfers completed'] + $v['transfers cancelled'])
    return $v
}

function Read-Now {
    # Re-derives the extension VA from the debug console every time.  It is NOT
    # constant: stage D measured that the extension base MOVES across a restart,
    # and a two-controller machine has two of them.  A cached VA would read the
    # counter table out of whatever now occupies the old pool block - a
    # plausible set of numbers, never an error.
    $ident = Find-ExtensionIdentity -DebugconLog $DebugconLog
    if ($null -eq $ident.Va) { throw ("no extension address in {0} - is the driver up, and is it the QEMU build? Since task 13-L.1 that trace exists in no other flavour." -f $DebugconLog) }
    if ($ident.Spans) {
        throw ("the debug console log now spans more than one driver load or binary (VAs: {0}; sizes: {1}). The driver restarted mid-soak, so every delta across that point is void." -f `
            ($ident.AllVas -join ", "), ($ident.AllSizes -join ", "))
    }
    $script:baseVa = $ident.Va
    return (Read-Counters -Port $Monitor -BaseVa $ident.Va -Table $script:table)
}

# THROUGH Add-Line, NOT Write-Host.  Until stage E this wrote to the console
# only, so the persisted report carried the identity and NOTHING BESIDE IT - the
# baseline, the abort block and the health block were all dropped.  That is
# precisely backwards: this stage's own reading rule is that a gap is not a leak
# until the abort counters are read NEXT TO it, and the load clause's verdict is
# "gap zero WITH every abort counter 0".  The blocks survived only when the
# operator happened to tee the console, which is not a property of the harness.
# Every report written before that date has the hole - see the 2b legs.
function Write-Block {
    param([string]$Title, $Snapshot, [string[]]$Names)
    Add-Line ("  {0}" -f $Title)
    foreach ($n in $Names) {
        try {
            $f = Resolve-CounterLabel -Table $script:table -Label $n
            Add-Line ("    {0,-42} {1}" -f $n, $Snapshot.Values[$f])
        } catch {
            Add-Line ("    {0,-42} (no such counter - table changed?)" -f $n)
        }
    }
}

# ------------------------------------------------------------ bus plumbing ---

# `device_del` OF A usb-storage DESTROYS ITS `-drive if=none` BACKEND, so every
# replug needs a fresh drive_add or cycles 2..N test nothing at all.  This is the
# single most expensive trap in the stage and it fails SILENTLY: the device_add
# refuses, the cycle records no attach, and a loop written without it looks like
# it ran twenty times.
#
# The throttle is not tuning.  QEMU completes about 99% of transfers
# synchronously inside the doorbell kick, so a blind unplug essentially never
# catches a transfer in flight - two pulls at 215 and 597 writes deep caught
# nothing.  At iops-total=30 the same pull catches one 3 times out of 3, which is
# what makes the in-flight cancellation path reachable at all in this vehicle.
function Add-StorageBackend {
    param([string]$Id)
    # The option string is one HMP argument; a medium path with a space in it
    # would split it, so it goes through ConvertTo-HmpArgument (quoted, with
    # forward slashes) when it needs to.
    $opts = "if=none,id={0},file={1},format=raw,throttling.iops-total=30" -f $Id, $StorageImage
    Send-Checked -Port $Monitor -Command ("drive_add 0 " + (ConvertTo-HmpArgument -Text $opts)) | Out-Null
}

function Get-AsixBusAddr {
    # Resolved from `info usbhost` on EVERY attach.  hostaddr is libusb's device
    # address and moves on every replug; and the vendorid/productid form must
    # never be used, because on this build it attaches an unbound stub and
    # PRINTS NO ERROR - indistinguishable from success until you read the speed.
    $lines = Get-MonitorText -Port $Monitor -Command "info usbhost"
    $pb = $null; $pa = $null; $ps = $null
    foreach ($line in $lines) {
        if ($line -match 'Bus\s+(\d+),\s+Addr\s+(\d+),.*Speed\s+([\d.]+)\s+Mb/s') {
            $pb = $Matches[1]; $pa = $Matches[2]; $ps = $Matches[3]
        }
        elseif ($line -match 'USB device\s+0b95:7720') {
            return [pscustomobject]@{ Bus = $pb; Addr = $pa; Speed = $ps }
        }
    }
    return $null
}

function Get-AttachSpec {
    param([string]$Class, [string]$Id)
    switch ($Class) {
        'hid'     { return "usb-mouse,id=$Id,bus=xhci.0,port=$DutPort" }
        'storage' { return "usb-storage,id=$Id,bus=xhci.0,port=$DutPort,drive=${Id}_drv,removable=on" }
        'audio'   { return "usb-audio,id=$Id,bus=xhci.0,port=$DutPort,audiodev=$AudioDev" }
        'net'     {
            $a = Get-AsixBusAddr
            if ($null -eq $a) { return $null }
            return ("usb-host,id={0},bus=xhci.0,port={1},hostbus={2},hostaddr={3}" -f $Id, $DutPort, $a.Bus, $a.Addr)
        }
    }
    return $null
}

# An attach is only an attach if the device ARRIVED and is ELECTRICALLY
# ATTACHED, and `info usb` answers only the first of those.  Both halves are
# needed: `usb-bot`/`usb-uas` occupy a port with attached=false, which read as a
# driver defect for the whole of Phase 10 and into Phase 11.  A passed-through
# `usb-host` has a third failure mode on top - the unbound stub - so its speed
# and product string are checked too.
function Confirm-Attached {
    param([string]$Id, [string]$Class)
    $onBus = $false; $line = ""
    for ($w = 0; $w -lt 20; $w++) {
        $txt = Get-MonitorText -Port $Monitor -Command "info usb"
        foreach ($l in $txt) {
            if ($l -match ('ID:\s*' + [regex]::Escape($Id) + '\s*$')) { $onBus = $true; $line = $l.Trim() }
        }
        if ($onBus) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $onBus) { return "device_add returned no error but `info usb` never listed id=$Id - nothing was put on the bus" }

    if ($Class -eq 'net') {
        # The stub signature, byte-identical to what an impossible
        # vendorid=0xdead,productid=0xbeef produces.  Without this check a
        # refused claim reads as a successful attach and the cycle measures an
        # empty shell.
        if ($line -match 'Speed\s+1\.5\s+Mb/s' -or $line -match 'Product\s+USB Host Device') {
            return "the passthrough attached the UNCLAIMED STUB, not the adapter: '$line'"
        }
        if ($line -notmatch 'Speed\s+480\s+Mb/s') {
            return "the adapter attached below High Speed: '$line'"
        }
    }

    $path = "/machine/peripheral/$Id"
    $state = ((Get-MonitorText -Port $Monitor -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
    if ($state -match "(?i)\btrue\b") { return "" }
    if ($state -notmatch "(?i)\bfalse\b") { return "could not read the attach state of id=$Id - the monitor said '$state'" }
    Send-Checked -Port $Monitor -Command ("qom-set {0} attached true" -f $path) | Out-Null
    Start-Sleep -Milliseconds 500
    $state = ((Get-MonitorText -Port $Monitor -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
    if ($state -match "(?i)\btrue\b") { return "" }
    return "id=$Id occupies a port but is not electrically attached, and qom-set did not change that (now '$state')"
}

# A PULL IS ONLY A PULL IF THE DEVICE LEFT.  unplug-8v.ps1 printed a confident
# FIRED while `info usb` still listed the device (batch 8-V.1), so departure is
# confirmed from the monitor rather than from device_del returning quietly.
function Confirm-Departed {
    param([string]$Id, [int]$TimeoutSeconds = 20)
    # Only a COMPLETE reply that omits the id is a departure: `info usb` on
    # an empty bus and a monitor that never finished answering both print
    # nothing, and Test-UsbDeviceListed answers $null for the second.
    for ($w = 0; $w -lt ($TimeoutSeconds * 2); $w++) {
        if ((Test-UsbDeviceListed -Port $Monitor -Id $Id) -eq $false) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Restore-Ps2Pointer {
    if ($NoRepin) { return }
    # Attaching a USB pointer takes the operator's mouse: QEMU routes input to
    # the most recently added handler, and a guest that has not bound it yet
    # receives nothing - which is indistinguishable from a hung guest from the
    # outside.  There is no `keyboard_set`, which is why this cycles a MOUSE
    # rather than a keyboard.
    $mice = Get-MonitorText -Port $Monitor -Command "info mice"
    $ps2 = $null
    foreach ($l in $mice) { if ($l -match 'Mouse #(\d+):.*PS/2') { $ps2 = $Matches[1] } }
    if ($null -ne $ps2) { Send-Mon -Port $Monitor -Command ("mouse_set " + $ps2) -Quiet | Out-Null }
}

# ------------------------------------------------------------------ report ---

$report = @()
function Add-Line { param([string]$S) $script:report += $S; Write-Host $S }

# ------------------------------------------------------------- the verdict ---
#
# **A stage that printed its failures and exited 0 is a stage that passed, to
# everything except a human reading the scrollback.** This script is quoted in
# the roadmap as evidence, is run unattended, and is long enough that its
# interesting lines scroll away - so every mandatory outcome below records
# itself here as well as printing, and the process ends nonzero if any did.
#
# The report is written FIRST and the exit code taken afterwards, because the
# evidence is worth more than the verdict: a failed run whose report was
# discarded costs the boot it took to produce it.
#
# What counts as a failure is deliberately narrow - a clause the stage is
# *asked* to observe, not a reading that needs explaining. A nonzero identity
# gap is not here: stage E established it is the expected abort shape on this
# vehicle, and the block above says which shape it got.
$script:verdict = @()
function Add-Failure {
    param([string]$S)
    $script:verdict += $S
    Add-Line ("  *** FAIL: {0}" -f $S)
}

# One spelling of "write the evidence out", because there are now two exits that
# have to do it: the ordinary end and the trap below.
#
# **It cannot throw.** It is called from the trap below, where a second
# exception would replace the original error with a report-writing error and
# skip the exit code the trap exists to produce - so a disk that filled, a
# read-only OutDir or a bad path would turn "the run aborted" into a confusing
# unrelated failure. A report that could not be written is reported and the
# console output remains the record.
#
# `$script:reportSaved` is what the exit lines read, because swallowing the
# failure here must not leave the end of the run telling the operator to go and
# read a file that is not there - which would send them looking for the verdict
# in the one place it certainly is not.
$script:reportPath = ""
$script:reportSaved = $false
function Save-Report {
    try {
        if ($OutDir -eq "") { return }
        $script:reportPath = Join-Path $OutDir ("soak-{0}.txt" -f $(if ($Target -ne "") { $Target } else { "run" }))
        Set-Content -LiteralPath $script:reportPath -Value $script:report -Encoding ascii
        $script:reportSaved = $true
        Write-Host ""
        Write-Host ("report: {0}" -f $script:reportPath)
    } catch {
        Write-Host ""
        Write-Host ("*** THE REPORT COULD NOT BE WRITTEN: {0}" -f $_.Exception.Message)
        Write-Host "    The console output above is the only record of this run - keep it."
    }
}

# **A run that dies mid-way must still leave its evidence on disk.** Several
# conditions in this script are terminating by design - the guest stopping, a
# counter read whose extension VA moved because the driver restarted - and
# every one of them happens at the point where the report is most worth having
# and least likely to be re-creatable, since reproducing it costs another boot.
# Before this trap existed those paths threw past the final Set-Content and the
# whole accumulated report went to the console only.
#
# A `trap` rather than wrapping three hundred lines in try/finally: it catches
# any terminating error after this point without re-indenting the body, so the
# measurement code stays the shape it was verified in.
#
# Exit 4 - ABORTED - is deliberately its own code. It is not "the driver
# failed" (1) and not "a clause could not be read" (2): the run did not finish,
# so most clauses were never reached, and a caller must not read the absence of
# recorded failures as their absence in fact.
trap {
    Add-Line ""
    Add-Line ("*** THE RUN ENDED ON AN ERROR AND IS INCOMPLETE: {0}" -f $_.Exception.Message)
    Add-Line  "    Everything above it was measured; everything after it was not reached."
    Save-Report
    Write-Host "VERDICT: ABORTED"
    exit 4
}

# ---------------------------------------------------------------- preflight ---

Write-Host ""
Write-Host "=== preflight"
if (Test-MonitorPortFree -Port $Monitor) {
    throw ("nothing is listening on monitor port {0}. This script drives an ALREADY-RUNNING guest - start the stage's launcher first." -f $Monitor)
}
$script:table = Import-CounterTable
$ident0 = Find-ExtensionIdentity -DebugconLog $DebugconLog
if ($null -eq $ident0.Va) { throw ("no extension address in {0}" -f $DebugconLog) }
if ($ident0.Spans) {
    throw ("{0} already spans more than one driver load or binary (VAs: {1}; sizes: {2}). Restart the guest so the soak measures one continuous load." -f `
        $DebugconLog, ($ident0.AllVas -join ", "), ($ident0.AllSizes -join ", "))
}
# The whole reason a stale reading cannot be mistaken for a wrong value.
#
# The null guard is its own throw and not a silent skip (repo audit D4): a $null
# Size binds to the [int] parameter as 0, so the freshness check would report
# "STALE OFFSETS ... the running driver reports MiniPortExtensionSize=0" - an
# operator sent to regenerate offsets that are perfectly fine, when the real
# fault is a trace that never printed the line. The two sibling scripts already
# throw here; this one did not.
if ($null -eq $ident0.Size) {
    throw ("{0} carries no MiniPortExtensionSize line, so the offset table cannot be checked against the running driver. A soak on unchecked offsets is a soak of wrong values." -f $DebugconLog)
}
Assert-OffsetsFresh -OffsetsFile $script:table.OffsetsFile -ExtensionSizeFromTrace $ident0.Size | Out-Null
Write-Host ("driver up: extension 0x{0}, MiniPortExtensionSize={1}, table SIZEOF {2} - AGREE" -f $ident0.Va, $ident0.Size, $script:table.Sizeof)

$alive = Test-GuestAlive -Port $Monitor
if (-not $alive.Alive) { throw ("the guest is not alive before the soak even started [{0}]: {1} ({2})" -f $alive.Verdict, $alive.Why, $alive.Detail) }

$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
Add-Line ""
Add-Line ("=== batch 11-V stage F soak - task 11-V.5")
Add-Line ("target          : {0}" -f $(if ($Target -ne "") { $Target } else { "(unnamed)" }))
Add-Line ("started         : {0}" -f $stamp)
Add-Line ("classes         : {0}" -f ($classList -join ", "))
Add-Line ("cycles per class: {0}" -f $Cycles)
Add-Line ("extension size  : {0} (table SIZEOF {1})" -f $ident0.Size, $script:table.Sizeof)
Add-Line ("storage medium  : {0}" -f $StorageImage)
Add-Line ""

$before = Read-Now
Write-Block -Title "baseline" -Snapshot $before -Names ($IDENTITY + $HEALTH)
Add-Line ""

# ------------------------------------------------------------- cycle phase ---

$cycleResults = @()
foreach ($class in $classList) {
    Add-Line ("--- {0}: {1} unplug/replug cycles" -f $class, $Cycles)
    $ok = 0; $failed = 0; $firstError = ""
    $classBefore = Read-Now

    for ($i = 1; $i -le $Cycles; $i++) {
        $id = "soak_{0}_{1}" -f $class, $i
        $err = ""

        if ($class -eq 'storage') { Add-StorageBackend -Id ("{0}_drv" -f $id) }

        $spec = Get-AttachSpec -Class $class -Id $id
        if ($null -eq $spec) {
            $err = "could not resolve the ASIX in `info usbhost` - it is unplugged on the host, or bound to a driver libusb cannot enumerate through"
        } else {
            # Send-Checked, NOT `Send-Mon -Quiet`.  -Quiet does not read the
            # socket at all: it writes, sleeps 120 ms and closes, and only ever
            # increments MonitorErrors when the TCP CONNECT throws.  So a
            # QEMU-side refusal - a duplicate id, a drive that does not resolve,
            # no free port - comes back on the wire and is discarded, and a
            # `Get-MonitorErrors` comparison around it can never fire.  That is
            # this library's own hub7bv0.ps1 defect 2, and a first version of
            # this loop had it: the cycle was still caught downstream by
            # Confirm-Attached, but it was reported as "never appeared on the
            # bus" with QEMU's actual message thrown away - and that message is
            # the whole diagnosis.  NEVER SUPPRESS THE MONITOR'S REPLY.
            $errBefore = Get-MonitorErrors
            Send-Checked -Port $Monitor -Command ("device_add " + $spec) | Out-Null
            Start-Sleep -Milliseconds 400
            if ((Get-MonitorErrors) -gt $errBefore) {
                $err = "device_add was refused - see the monitor reply above"
            } else {
                $err = Confirm-Attached -Id $id -Class $class
            }
        }

        if ($err -eq "") {
            if ($class -eq 'hid') { Restore-Ps2Pointer }
            Start-Sleep -Seconds $SettleSeconds
            Send-Checked -Port $Monitor -Command ("device_del " + $id) | Out-Null
            if (-not (Confirm-Departed -Id $id)) {
                $err = "device_del did not remove id=$id - `info usb` still lists it, so this cycle's pull never happened"
            }
        }

        if ($err -eq "") {
            $ok++
        } else {
            $failed++
            if ($firstError -eq "") { $firstError = "cycle ${i}: $err" }
            # Leave nothing on the bus for the next cycle to inherit, quietly:
            # a cycle that never attached must not report a failure to detach.
            Send-Mon -Port $Monitor -Command ("device_del " + $id) -Quiet | Out-Null
            Start-Sleep -Milliseconds 500
        }

        if (($i % $ReadEvery) -eq 0 -or $i -eq $Cycles) {
            $a = Test-GuestAlive -Port $Monitor
            $snap = Read-Now
            $live = Get-Identity $snap
            # `devices addressed` BESIDE the transfer counters, because without
            # it a frozen transfer count has two opposite readings and no way to
            # tell them apart: either the replugs are reaching the driver and
            # simply moving little traffic, or they are NOT reaching it at all
            # and the cycle loop is exercising nothing.  Measured on 2d
            # The 2d run: HID cycles 10 and 15 reported byte-identical
            # submitted/completed/cancelled, and the run had no counter on the
            # line that could distinguish the two.  `+0` is not a pass.
            $addr = $snap.Values[(Resolve-CounterLabel -Table $script:table -Label 'devices addressed')]
            # `EP0 opens refused` beside it, because those two together are the
            # whole diagnosis of that 2d finding: `addressed` frozen
            # while `refused` climbs is usbport still asking and this driver
            # declining, which is a different defect from usbport having given
            # up (both frozen) or from the replug never reaching us (neither
            # moving, with RH ports reset also flat).
            $ref = $snap.Values[(Resolve-CounterLabel -Table $script:table -Label 'EP0 opens refused')]
            Add-Line ("  cycle {0,3}/{1}: ok={2} failed={3}  addressed={4} ep0refused={5} submitted={6} completed={7} cancelled={8} gap={9}  guest={10}" -f `
                      $i, $Cycles, $ok, $failed, $addr, $ref, $live['transfers submitted'], $live['transfers completed'],
                      $live['transfers cancelled'], $live['gap'], $a.Verdict)
            if (-not $a.Alive) {
                $shot = Save-GuestScreenshot -Port $Monitor -Path (Join-Path $OutDir ("soak-{0}-{1}-dead.ppm" -f $Target, $class))
                throw ("the guest stopped executing during the {0} cycles [{1}]: {2} ({3}). Screenshot: {4}" -f $class, $a.Verdict, $a.Why, $a.Detail, $shot)
            }
        }
    }

    $classAfter = Read-Now
    $d = Get-CounterDelta -Before $classBefore -After $classAfter
    if ($d.Restarted) {
        Add-Line ("  *** the driver RESTARTED during this class ({0}) - every delta above is void" -f ($d.WentBackwards -join ", "))
    }
    Add-Line ("  {0}: {1}/{2} cycles completed, {3} failed" -f $class, $ok, $Cycles, $failed)
    if ($firstError -ne "") { Add-Line ("  first failure - {0}" -f $firstError) }
    Add-Line ""
    $cycleResults += [pscustomobject]@{ Class = $class; Ok = $ok; Failed = $failed; FirstError = $firstError }
}

# -------------------------------------------------------------- load phase ---

if ($LoadSeconds -gt 0) {
    Add-Line ("--- concurrent load: every class attached at once for {0} s" -f $LoadSeconds)
    # WHICH PAIR, because the wrong one fails silently.  scripts\vm-matrix\guest\
    # holds two: LOAD.BAT/STAGEF.BAT are cmd.exe programs for the Windows 2000
    # guests, and LOAD98.BAT/STGF98.BAT are the COMMAND.COM pair for 2a.  Run the
    # Win2000 pair on Windows 98 and the load REPORTS AS RUNNING while nothing
    # moves - `cmd /c`, `%~f0` and `for /L` are exactly the lines that start
    # traffic.  And on 2a a batch file cannot be executed off the `fat:` volume at
    # all; it has to be copied to C:\ first.
    if ($Target -eq "2a") {
        Add-Line "    (start the guest-side traffic NOW - in the guest: copy d:\*.bat c:\  then  c:  cd \  load98 F D 192.168.1.100)"
    } else {
        Add-Line "    (start the guest-side traffic NOW - LOAD.BAT on the transfer volume)"
    }
    $loadIds = @()
    $classIndex = 0
    foreach ($class in $classList) {
        $classIndex++
        $id = "load_$class"
        # Each class gets its OWN root port - this is the one phase where they
        # are on the bus simultaneously, so they cannot share $DutPort.
        #
        # NUMBERED FROM 1, BECAUSE THERE ARE ONLY FOUR AND THE FOURTH CLASS NEEDS
        # THE FIRST.  `qemu-xhci` defaults to `p2=4` (checked with
        # `-device qemu-xhci,help` on QEMU 11.1.0) and every batch 11-V launcher
        # declares `p3=0`, so the root ports are 1..4 exactly.  Based at 2 this
        # loop asks for port 5 on the FOURTH class - which on 2b is `audio`, the
        # one class 2d never ran - and QEMU refuses it.  Four classes, four
        # ports, starting at the first one.
        #
        # The port is the class's position in the list, not the count of
        # classes that made it: a class that failed after its device_add
        # (below) still occupied its port, and the next class asking for the
        # same number was refused for that reason alone.
        $port = $classIndex
        if ($class -eq 'storage' -and $LoadStorageDrive -ne "") {
            $spec = "usb-storage,id=$id,bus=xhci.0,port=$port,drive=$LoadStorageDrive,removable=on"
        } else {
            if ($class -eq 'storage') { Add-StorageBackend -Id ("{0}_drv" -f $id) }
            # The null test comes BEFORE the -replace: `$null -replace` is an
            # empty string, so a spec that could not be resolved used to pass
            # the guard and go out as a bare `device_add`.
            $spec = Get-AttachSpec -Class $class -Id $id
            if ($null -ne $spec) { $spec = $spec -replace "port=$DutPort", ("port={0}" -f $port) }
        }
        if ($null -eq $spec) { Add-Failure ("load class '{0}' NOT ATTACHED - could not resolve it" -f $class); continue }
        # Send-Checked here too, for the reason at the cycle attach above.
        $errBefore = Get-MonitorErrors
        Send-Checked -Port $Monitor -Command ("device_add " + $spec) | Out-Null
        Start-Sleep -Milliseconds 600
        if ((Get-MonitorErrors) -gt $errBefore) {
            Add-Failure ("load class '{0}' NOT ATTACHED - device_add was refused, see the reply above" -f $class); continue
        }
        $err = Confirm-Attached -Id $id -Class $class
        if ($err -ne "") {
            # The device_add took, so the stub is on the bus: it comes off
            # again, or it sits there through the settled reading below.
            Send-Mon -Port $Monitor -Command ("device_del " + $id) -Quiet | Out-Null
            Start-Sleep -Milliseconds 500
            Add-Failure ("load class '{0}' NOT ATTACHED - {1}" -f $class, $err); continue
        }
        if ($class -eq 'hid') { Restore-Ps2Pointer }
        $loadIds += $id
        Add-Line ("    {0}: attached as {1}" -f $class, $id)
    }
    Add-Line ("    {0} of {1} classes are on the bus together" -f $loadIds.Count, $classList.Count)
    # The whole point of this phase is SIMULTANEITY, so a run that got some of
    # the requested classes onto the bus measured something weaker than it was
    # asked for - and the line above alone reads like a fact rather than a
    # shortfall. Each missing class already recorded itself; this catches the
    # case where the count is short for a reason none of them covered.
    if ($loadIds.Count -lt $classList.Count -and $script:verdict.Count -eq 0) {
        Add-Failure ("only {0} of the {1} requested load classes reached the bus" -f $loadIds.Count, $classList.Count)
    }

    $loadBefore = Read-Now
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $LoadSeconds) {
        Start-Sleep -Seconds 15
        $a = Test-GuestAlive -Port $Monitor
        $live = Get-Identity (Read-Now)
        Add-Line ("    t+{0,4}s  submitted={1} completed={2} cancelled={3} gap={4}  guest={5}" -f `
                  [int]$sw.Elapsed.TotalSeconds, $live['transfers submitted'], $live['transfers completed'],
                  $live['transfers cancelled'], $live['gap'], $a.Verdict)
        if (-not $a.Alive) {
            $shot = Save-GuestScreenshot -Port $Monitor -Path (Join-Path $OutDir ("soak-{0}-load-dead.ppm" -f $Target))
            throw ("the guest stopped executing under concurrent load [{0}]: {1} ({2}). Screenshot: {3}" -f $a.Verdict, $a.Why, $a.Detail, $shot)
        }
    }
    $loadAfter = Read-Now
    $ld = Get-CounterDelta -Before $loadBefore -After $loadAfter
    $loadMoved = $ld.Values[(Resolve-CounterLabel -Table $script:table -Label 'transfers submitted')]
    Add-Line ("    load window moved {0} transfers" -f $loadMoved)
    # **Zero is the reading this phase exists to catch, and it looks exactly
    # like success in every other line.** 2a's blocked load reported as running
    # while the guest script never started traffic (28 msd submits against 2d's
    # 10,837): the attach list was right, the window elapsed, the report said
    # so. A load phase that moved nothing measured nothing.
    if ($loadMoved -le 0) {
        Add-Failure "the concurrent-load window moved ZERO transfers - the guest-side traffic never started, so this phase measured nothing"
    }
    # Checked, not quiet: a teardown that fails here leaves a device on the bus
    # for the SETTLED READING that follows, which is the one reading in this
    # script whose whole meaning depends on the bus being empty.
    foreach ($id in $loadIds) {
        Send-Checked -Port $Monitor -Command ("device_del " + $id) | Out-Null
        # Not merely noted: the settled reading below is the one measurement in
        # this script whose whole meaning depends on the bus being empty, so a
        # device still attached makes the result that follows it unreadable.
        if (-not (Confirm-Departed -Id $id)) { Add-Failure ("{0} did not leave the bus, so the settled reading below is taken against a populated bus" -f $id) }
    }
    Add-Line ""
}

# ------------------------------------------------------- the settled reading ---

# THE IDENTITY IS ONLY EXACTLY TRUE AT A SETTLED BUS, so this is taken with
# nothing attached, twice, and what is reported is whether the gap MOVED.
Add-Line "--- settled reading (bus empty)"
Start-Sleep -Seconds 10
$settled1 = Read-Now
$id1 = Get-Identity $settled1
Start-Sleep -Seconds 20
$settled2 = Read-Now
$id2 = Get-Identity $settled2

Add-Line ("  read 1: submitted={0} completed={1} cancelled={2} gap={3}" -f `
          $id1['transfers submitted'], $id1['transfers completed'], $id1['transfers cancelled'], $id1['gap'])
Add-Line ("  read 2: submitted={0} completed={1} cancelled={2} gap={3}" -f `
          $id2['transfers submitted'], $id2['transfers completed'], $id2['transfers cancelled'], $id2['gap'])

$aborted = $settled2.Values[(Resolve-CounterLabel -Table $script:table -Label 'transfers aborted')]
if ($id2['gap'] -eq 0) {
    Add-Line "  IDENTITY EXACT: submitted = completed + cancelled, with nothing on the bus."
} elseif ($id1['gap'] -eq $id2['gap'] -and $id1['transfers submitted'] -eq $id2['transfers submitted']) {
    Add-Line ("  gap of {0}, STABLE across two settled reads with no new submissions." -f $id2['gap'])
    # THE ABORT TERM, CHECKED RATHER THAN LEFT TO BE RE-DERIVED.  A soak PULLS
    # devices, and a pull aborts whatever that endpoint had outstanding - so on
    # this vehicle the gap is expected to be the abort count, and the Phase 8
    # identity needs the third term.  Measured on 2d: two HID cycles
    # gave 42 = 36 + 4 + 2 with `transfers aborted` = 2, gap = 2 exactly.  This
    # says which of the two shapes the run actually produced instead of leaving
    # a nonzero gap to be argued about later.
    if ($id2['gap'] -eq $aborted) {
        # Backticks are PowerShell's ESCAPE character, not a quote: the first
        # version of this line wrote "`t" and emitted a literal TAB into every
        # report, so the sentence read "AND IT IS EXACTLY <tab>ansfers aborted".
        # Name the counter in plain quotes instead.
        Add-Line ("  AND IT IS EXACTLY 'transfers aborted' ({0}): submitted = completed + cancelled + aborted" -f $aborted)
        Add-Line  "  balances. An aborted transfer is neither completed nor cancelled, and a soak"
        Add-Line  "  that pulls devices produces one per pull that had something outstanding."
    } else {
        # Plain quotes, exactly as the if-branch above uses them and for the
        # reason its comment gives: a backtick is PowerShell's escape character,
        # so "`t" here rendered a literal TAB and the persisted report read
        # "...<tab>ansfers aborted is 2...". The fix was applied to one branch
        # and not its sibling (repo audit D3).
        Add-Line ("  'transfers aborted' is {0}, which does NOT account for the gap of {1}." -f $aborted, $id2['gap'])
        Add-Line  "  The residue is the stage E shape - parked interrupt IN requests on HID and hub"
        Add-Line  "  status-change endpoints. A reading to explain, NOT automatically a leak."
    }
} else {
    Add-Line ("  gap MOVED between the two reads ({0} -> {1}) - the bus had not settled," -f $id1['gap'], $id2['gap'])
    Add-Line  "  so this reading is premature rather than a result. Re-read when quiet."
}
Add-Line ""
Write-Block -Title "abort block (read BESIDE the identity - an abort in progress is the other legitimate gap)" -Snapshot $settled2 -Names $ABORTS
Add-Line ""
Write-Block -Title "health" -Snapshot $settled2 -Names $HEALTH
Add-Line ""

$overall = Get-CounterDelta -Before $before -After $settled2
if ($overall.Restarted) {
    Add-Line ("*** THE DRIVER RESTARTED DURING THIS SOAK ({0}) - the whole-run deltas are void." -f ($overall.WentBackwards -join ", "))
    Add-Failure ("the driver restarted mid-soak ({0}) - every delta across that point is void" -f ($overall.WentBackwards -join ", "))
}

Add-Line "--- cycle summary"
foreach ($r in $cycleResults) {
    Add-Line ("  {0,-9} {1}/{2} completed{3}" -f $r.Class, $r.Ok, $Cycles, $(if ($r.Failed -gt 0) { "   ({0} FAILED)" -f $r.Failed } else { "" }))
    if ($r.Failed -gt 0) {
        Add-Failure ("{0}: {1} of {2} cycles FAILED - first: {3}" -f $r.Class, $r.Failed, $Cycles, $r.FirstError)
    }
}
Add-Line ""

# The monitor is this script's only channel to the guest, so errors on it are
# not background noise: a counter read that failed is a reading this report may
# have printed anyway, from the sample before it.
$monErrors = Get-MonitorErrors
if ($monErrors -gt 0) {
    Add-Failure ("{0} monitor error(s) during the run - at least one guest read did not answer, so some line above may be a stale sample" -f $monErrors)
}

# **An invocation that ran nothing must not report PASS.** `-Cycles 0` and
# `-LoadSeconds 0` are each individually legitimate - a cycles-only run and a
# load-only run are both real ways to use this - but a run with no cycles AND no
# load window measured nothing at all, and every clause below it would be
# vacuously satisfied. That is a shape whose green result means strictly
# nothing, so it is refused rather than reported. (An empty `-Classes` is NOT in
# that "individually legitimate" set - see the check below it, which refuses one
# outright whatever the load window says.)
$cyclesRun = 0
foreach ($r in $cycleResults) { $cyclesRun += ($r.Ok + $r.Failed) }
if ($cyclesRun -eq 0 -and $LoadSeconds -le 0) {
    Add-Failure ("this invocation performed no work at all - {0} cycle(s) over {1} class(es) and no load window, so there is nothing here to pass" -f `
                 $Cycles, $classList.Count)
}
# **No classes is a no-work run even with a load window**, and it is the more
# dangerous shape of the two because it looks busy. With an empty class list the
# load phase attaches nothing, "0 of 0 classes are on the bus together" is
# vacuously satisfied, and the load window still measures the bus - so transfers
# from whatever was already attached, or from the guest's own devices, can carry
# it to a PASS that this script did nothing to produce.
if ($classList.Count -eq 0) {
    Add-Failure "no device classes were requested (-Classes is empty), so nothing this script controls was ever put on the bus - any movement measured below belongs to something else"
}

Add-Line "--- verdict"
if ($script:verdict.Count -eq 0) {
    Add-Line "  PASS - every clause this stage is asked to observe was observed."
} else {
    Add-Line ("  FAIL - {0} clause(s):" -f $script:verdict.Count)
    foreach ($v in $script:verdict) { Add-Line ("    - {0}" -f $v) }
}
Add-Line ""
Add-Line ("finished: {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))

Save-Report

# The report is on disk before the exit code is taken - see the note beside
# Add-Failure. A caller that only checks $LASTEXITCODE still gets the truth;
# one that reads the report gets the reason.
if ($script:verdict.Count -gt 0) {
    Write-Host ("VERDICT: FAIL ({0} clause(s)) - see the verdict block {1}." -f `
                $script:verdict.Count,
                $(if ($script:reportSaved) { "in the report" } else { "printed above - THE REPORT WAS NOT WRITTEN" }))
    exit 1
}
Write-Host "VERDICT: PASS"
exit 0
