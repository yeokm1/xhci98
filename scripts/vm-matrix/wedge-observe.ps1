<#
.SYNOPSIS
Finding 3's wedge, as an OBSERVATORY - drive the bench recipes in QEMU with
every relevant trace channel open, and record what each plug and each pull did.

.DESCRIPTION
`docs/contributing/runs/run-13e.md`, "Finding H", reduced Finding 3 to one named
variable on the E460 - a device that is NOT High Speed using a root port in
between two High-Speed enumerations - from four conditions with device
identity, plug count and the port held constant:

    one High-Speed flash drive, repeated              30 events   clean
    two different High-Speed flash drives             16 events   clean
    High-Speed -> Low-Speed mouse    -> High-Speed     5 events   WEDGE
    High-Speed -> Full-Speed C-Media -> High-Speed     5 events   WEDGE

**THAT READING IS CONFOUNDED, and two bench observations say so.**
The project owner reported, at the same position D:

  * the **Low-Speed mouse alone**, plugged and unplugged and plugged again,
    fails on the SECOND plug - three events, no speed change anywhere, the same
    device both times.  So the trigger is not a *transition*; Finding H's table
    simply has no row for repeat-plugging a sub-High-Speed device.
  * the **`0B95:7720` ASIX Ethernet adapter** wedges after two plug/unplug
    cycles - and batch 13-H characterised that adapter as **High Speed**
    (docs/contributing/test-equipment.md, "Requirements the Phase 13 clauses
    set", row 10).  The speed reading predicts it cannot wedge,
    because it is the same speed class as the flash drives that survived thirty
    plugs.

What actually separates the five devices is whether they carry a **PERIODIC
ENDPOINT**, and in the bench data speed and periodicity were perfectly
confounded - every sub-High-Speed device tried happened to be periodic and every
High-Speed device tried happened to be bulk-only:

    device at D            speed   periodic endpoint          result
    HS flash drive         High    none (bulk only)           clean, 30 events
    two HS flash drives    High    none                       clean, 16 events
    046D:C077 mouse        Low     int IN, bInterval=10       fails on plug 2
    0B95:7720 ASIX         HIGH    int IN, bInterval=11       wedges in 3 cycles
    0D8C:0014 C-Media      Full    isochronous                WEDGE

It also re-unifies bench session 1's audio route, which the record carries as open
("one fault with two routes, or two faults with one end state"): isochronous is
periodic.

**A rival formulation fits every row equally well and is NOT separated here**:
not "a periodic endpoint" but "**a transfer in flight at the moment of
disconnect**" - a periodic endpoint always has one queued and an idle flash
drive has none.  QEMU cannot separate those two either, because the arms below
detach an idle device in both cases; the separating test is a bench one
(unplug a flash drive during a file copy) and is named in the report.

THIS SCRIPT IS AN OBSERVATORY FIRST AND A REPRODUCER SECOND, and the difference
is the whole design:

  * **The halt is EXPECTED NOT to reproduce.**  Phase 10's `hid` group already
    alternates High and Full Speed six times on the pinned DUT port with a
    `device_del` between rows, and those rows PASS - so the obvious experiment
    has been run by accident and came back negative.  Both driver defects found were likewise invisible here.  The likely reading is that
    QEMU's xHCI model TOLERATES a malformed slot or port context where real
    silicon halts.
  * **So the deliverable is the TRACES, not the verdict.**  A bug can be plainly
    visible in a trace on a vehicle too forgiving to die of it.  Every step's
    slice of the QEMU trace, of the driver's own 0xE9 debug console, and of the
    counter block is cut out and written beside the report, so the question that
    matters can be answered whichever way the outcome falls: **is the slot
    properly disabled on the disconnect, and does the next enumeration
    re-derive its context or inherit one?**

Per arm: one boot, because a wedge is not recoverable and a boot is the blast
radius.  Per step: read counters, act, settle, read counters, and mark the byte
offsets of both logs so the step owns its own slice of each.

.PARAMETER Arm
Which sequences to run, one boot each.  Every arm presents its devices one at a
time on the SAME root port, each plugged and then pulled before the next.

    repeat-int    usb-mouse/hs x3        ONE High-Speed device with an INTERRUPT
                  endpoint, repeat-plugged.  The QEMU form of the owner's mouse
                  observation, with speed held at High so periodicity is the only
                  thing left.  THE DISCRIMINATOR: the speed reading calls this
                  clean, the periodic reading calls it a wedge.
    repeat-bulk   usb-storage/hs x3      ONE High-Speed BULK-ONLY device,
                  repeat-plugged.  The control, and the QEMU form of the bench's
                  clean 30-event row.  Both readings call this clean; an arm that
                  wedges HERE refutes both and means the vehicle, not the driver.
    fs-mid        storage/hs, mouse/FS, storage/hs   Finding H's recipe verbatim.
    hs-mid        storage/hs, mouse/HS, storage/hs   Finding H's control - speed
                  held constant, periodicity introduced in the middle.  Reads as
                  a control under the speed hypothesis and as a positive under
                  the periodic one, which is why it is not optional.

.PARAMETER Repeat
How many times to run the arm's whole device sequence inside one boot.  The
bench failed on the FIRST alternation and on the SECOND plug, so 1 is the honest
default; more is for giving QEMU every chance to fail.

.EXAMPLE
powershell -File scripts\vm-matrix\wedge-observe.ps1
powershell -File scripts\vm-matrix\wedge-observe.ps1 -Arm repeat-int -Repeat 3
powershell -File scripts\vm-matrix\wedge-observe.ps1 -Target 2b
#>
[CmdletBinding()]
param(
    [string]$Config = "",
    [string]$Qemu = "",
    [string]$Target = "2a",
    [string[]]$Arm = @("repeat-int", "repeat-bulk", "fs-mid", "hs-mid"),
    [int]$Repeat = 1,
    [string]$OutDir = "out\obs-13q",
    # The root port every device in every arm is presented on.  It is ONE port
    # for the same reason the bench used one socket: the damage is a property of
    # a port whose last occupant left something behind, so a sequence that
    # wandered between ports would not be the recipe.  2 is also the port the 2a
    # image was taught these classes at - see run-matrix.ps1's comment on
    # -DutPort, which is not cosmetic on Windows 98.
    [int]$DutPort = 2,
    [int]$Settle = 25,
    # Windows 98 idle-suspends the controller about half a second after the last
    # transfer and an attach onto a halted controller is invisible to the whole
    # stack (batch 7a-V), so a keep-alive pointer sits on ANOTHER root port for
    # the whole run and is pumped before each attach.  It is a variable this run
    # does not control and cannot remove on 2a; it never leaves, so it
    # contributes no disconnect of its own.
    #
    # **Its port is now DERIVED from -DutPort rather than left to QEMU** (the pre-cut audit,
    # pre-cut audit, item E6).  The `device_add` had no port= at all, so QEMU
    # gave it the lowest free root port - 1 - and this comment read "it is on
    # port 1, never on the DUT port", which was true of the default -DutPort and
    # of nothing else.  At `-DutPort 1` the keep-alive would have landed on the
    # port under test and every arm would have been measuring a socket that was
    # already occupied.  The default is unchanged: DutPort 2 still puts it on 1.
    # -NoPump drops it for a vehicle that does not need it.
    [switch]$NoPump,
    [switch]$KeepGuest
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

# ------------------------------------------------------------------- config ---
if ($Config -eq "") {
    foreach ($c in @("scripts\vm-matrix\matrix.config.psd1")) {
        if (Test-Path (Join-Path $repo $c)) { $Config = Join-Path $repo $c; break }
    }
}
if ($Config -eq "" -or -not (Test-Path -LiteralPath (Resolve-RepoPath $Config))) {
    throw "no configuration found. Copy scripts\vm-matrix\config.sample.psd1, edit the paths, and pass it with -Config."
}
$cfg = Import-PowerShellDataFile -LiteralPath (Resolve-RepoPath $Config)
$OutDir = Resolve-RepoPath $OutDir
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$qemuHint = if ($Qemu -ne "") { $Qemu } else { $cfg.Qemu }
$qemuBin = Resolve-QemuBinary -Hint $qemuHint
$qemuVer = Get-QemuVersion -Qemu $qemuBin
$vmDir = Resolve-RepoPath $cfg.VmDir

$tgt = $cfg.Targets | Where-Object { $_.Id -eq $Target }
if ($null -eq $tgt) { throw ("no target '{0}' in {1}" -f $Target, $Config) }

$table = Import-CounterTable
$available = Get-QemuUsbModels -Qemu $qemuBin
foreach ($m in @("usb-storage", "usb-mouse")) {
    if ($available -notcontains $m) { throw ("this QEMU build has no device model '{0}'" -f $m) }
}

Write-Host ("qemu   : {0} ({1})" -f $qemuBin, $qemuVer)
Write-Host ("target : {0} ({1})" -f $tgt.Id, $tgt.Name)
Write-Host ("dut    : root port {0}" -f $DutPort)
# Named, not left to QEMU's lowest-free-port rule - see -DutPort above.
$pumpPort = if ($DutPort -eq 1) { 2 } else { 1 }
if (-not $NoPump) { Write-Host ("pump   : root port {0}" -f $pumpPort) }
Write-Host ("out    : {0}" -f $OutDir)

# ---------------------------------------------------------------- the arms ---
# The two device shapes every arm is built from.  What separates them is the
# ENDPOINT TYPE, which is the variable the owner's two observations promoted:
# `usb-mouse` is HID with one INTERRUPT IN endpoint (periodic), `usb-storage` is
# mass storage BOT with bulk IN and bulk OUT and nothing periodic at all.  QEMU
# has no isochronous model in this population, so the third bench shape - the
# C-Media's iso - has no arm here and that is a limit of the vehicle, stated in
# the report rather than left to be inferred from an absence.
$mouseHs  = @{ Name = "mouse/HS";     Model = "usb-mouse";   AddArgs = "usb_version=2";                 Speed = "High"; Ep = "interrupt IN (periodic)" }
$mouseFs  = @{ Name = "mouse/FS";     Model = "usb-mouse";   AddArgs = "usb_version=1";                 Speed = "Full"; Ep = "interrupt IN (periodic)" }
# ONE BLOCK BACKEND PER STORAGE ATTACH, AND A FIXED SERIAL ACROSS THEM.
#
# **A `-drive if=none` backend is AUTO-DELETED when its guest device is
# unplugged.**  run-matrix.ps1 pays for this in its own comments - `usb-uas/fs`
# died on `Property 'scsi-hd.drive' can't find value 'matrixdrv2'` after
# `usb-bot/fs` had run - and every arm here that presents storage TWICE walked
# straight into it: the second attach was refused with `Property
# 'usb-storage.drive' can't find value 'wedgedrv'`, which cost this run three of
# its four arms.  Sequencing teardown does not help, because the
# deletion IS teardown.  One backend per consumer is the only shape that works.
#
# `{DRIVE}` is replaced per attach with `wedgedrv<n>`, and the launch declares
# exactly as many backends as the arm will consume.
#
# **`serial=` is what keeps the arm honest.**  One backend per attach means a
# different backing FILE each time, and QEMU derives `usb-storage`'s
# iSerialNumber from the drive - so without this the guest would see a different
# device instance on every plug, and `repeat-bulk` would stop being "the same
# device repeat-plugged", which is the whole of what it asserts.  Pinning the
# serial holds the descriptor identical across attaches; only the disk behind it
# differs, and nothing in these arms reads the disk.
$storHs   = @{ Name = "storage/HS";   Model = "usb-storage"; AddArgs = "drive={DRIVE},removable=on,serial=WEDGE0001"; Speed = "High"; Ep = "bulk IN + bulk OUT (no periodic endpoint)"; NeedsDrive = $true }

$armDefs = [ordered]@{
    "repeat-int" = @{
        What    = "THE DISCRIMINATOR - one HIGH-SPEED device with an INTERRUPT endpoint, repeat-plugged. Speed is held constant, so only periodicity is left. The speed reading calls this clean; the periodic reading calls it a wedge."
        Devices = @($mouseHs, $mouseHs, $mouseHs)
    }
    "repeat-bulk" = @{
        What    = "THE CONTROL - one HIGH-SPEED BULK-ONLY device, repeat-plugged. The QEMU form of the bench's clean 30-event row; both readings call it clean, so an arm that wedges here indicts the vehicle rather than the driver."
        Devices = @($storHs, $storHs, $storHs)
    }
    "fs-mid" = @{
        What    = "FINDING H'S RECIPE - a sub-High-Speed device uses the root port between two High-Speed enumerations."
        Devices = @($storHs, $mouseFs, $storHs)
    }
    "hs-mid" = @{
        What    = "FINDING H'S CONTROL, WHICH IS ALSO A DISCRIMINATOR - the same five events with the speed held constant. A control under the speed reading; a positive under the periodic one."
        Devices = @($storHs, $mouseHs, $storHs)
    }
}
foreach ($a in $Arm) { if (-not $armDefs.Contains($a)) { throw ("unknown arm '{0}'; known: {1}" -f $a, (($armDefs.Keys) -join ", ")) } }

# The trace events this hypothesis names, plus the ones that say whether the
# controller is still running at all.  Deliberately WIDER than the matrix's
# list - this is a single hand-driven sequence rather than a seventeen-row walk,
# so the volume is affordable here and is not there.  Still no `fetch_trb`, no
# `xfer_start`/`xfer_success` and no `port_read`: those bury a run's own evidence
# under hundreds of thousands of lines.
$traceEventList = @(
    # Slot lifecycle - the question itself.  Is the slot disabled on the
    # disconnect, and is the next one a fresh enable/address or an inherited
    # context?
    'usb_xhci_slot_enable'
    'usb_xhci_slot_disable'
    'usb_xhci_slot_address'
    'usb_xhci_slot_configure'
    'usb_xhci_slot_evaluate'
    'usb_xhci_slot_reset'
    # Endpoint lifecycle - a PERIODIC endpoint that is enabled and never
    # disabled is the shape the promoted hypothesis predicts, and these are the
    # events that would show it.
    'usb_xhci_ep_enable'
    'usb_xhci_ep_disable'
    'usb_xhci_ep_stop'
    'usb_xhci_ep_reset'
    'usb_xhci_ep_set_dequeue'
    # The port itself: link state, reset, and the driver's PORTSC writes.
    'usb_xhci_port_reset'
    'usb_xhci_port_link'
    'usb_xhci_port_notify'
    'usb_xhci_port_write'
    'usb_port_attach'
    'usb_port_detach'
    # Is the controller still running?  A halt is what the E460 does, and these
    # three are how it would be seen here.
    'usb_xhci_reset'
    'usb_xhci_run'
    'usb_xhci_stop'
    # Errors.
    'usb_xhci_xfer_error'
    'usb_xhci_unimplemented'
    'usb_xhci_enforced_limit'
)

# The counters this experiment is about, in the order a reader wants them.
# Everything else is still read - the per-step dump holds every one that moved -
# but these are the ones the report puts side by side, and they are the slot,
# address and port-change counters open item 1 asks for on metal.
$watch = @(
    'devices addressed'
    'slots enabled'
    'slots disabled'
    'slots reset to Default'
    'SET_ADDRESS interceptions'
    'devices torn down'
    'devices torn down by a port disable'
    'devices disowned by a port disable'
    'incompatible devices, slot disabled'
    'endpoints opened'
    'endpoint speed mismatches'
    # THE TEARDOWN PRECONDITION, and `teardowns without a stop` is the single
    # most important counter in this list.  `xhciDevTeardown` issues the Disable
    # Slot even when an endpoint could not be shown stopped (4.6.4 p.109's
    # precondition unmet), and the release decision then consults only the
    # completion code - so a Success drains the transfers and returns the rings
    # to a FIRST-FIT pool that hands them straight to the next device.  A device
    # with a periodic endpoint is the only kind that reaches that path at all,
    # which is exactly Finding K's split.  Nonzero here on metal is the whole
    # diagnosis; zero here in QEMU is why QEMU is clean.  The bench reading that
    # would have taken it had no vehicle and was never taken.
    'teardowns without a stop'
    'endpoint stop failures'
    'endpoint quiescence lost'
    'endpoints released'
    'endpoint stops'
    'endpoint dequeue sets'
    'transfers cancelled'
    'transfers failed - endpoint gone'
    'devices abandoned without evidence'
    'port event changes'
    'port events unmapped'
    'RH first decodes'
    'RH ports reset'
    'RH resets completed'
    'RH resets not confirmed'
    'RH reset timeouts'
    'RH ports disabled'
    'RH changes cleared'
    'topology: disconnects seen'
    'topology: swaps only the change word saw'
    'commands issued'
    'commands completed'
    'commands with a bad completion code'
    'device command failures'
    'slot states unreadable'
    # THE RECOVERY LADDER, and it is here because the Disable Slot in
    # `xhciDevTeardown` is OWED rather than issued - `xhciDevOwedOp` queues it
    # behind the Stop Endpoints, and the command engine carries one command in
    # flight.  A Stop Endpoint aimed at an endpoint whose device has physically
    # left, and which never completes, therefore blocks the command ring for the
    # WHOLE controller - which is the E460's signature exactly: nothing
    # enumerates anywhere afterwards, other root ports included, cold boot only.
    #
    # `src\xhci_cmd.c` has a three-rung answer to that (timeout -> abort ->
    # HCRST), so the interesting reading is not just "a command is stuck" but
    # WHETHER THE LADDER RAN.  Two branches that look identical from outside the
    # machine and are opposite inside it:
    #
    #   issued > completed and every counter below 0  => the command never timed
    #     out because THE TIMER NEVER FIRED - which is the UP Windows 98
    #     interrupt livelock the record has named but never established.
    #   the counters below nonzero  => the ladder ran, reset the controller, and
    #     service still did not come back: a different defect, above the engine.
    'commands timed out'
    'commands aborted'
    'command abort waits'
    'command reset requests'
    'command ring stops'
    'commands abandoned'
    'commands the engine gave up on'
    'commands outstanding past every watchdog'
)
# Resolved once, loudly: a label this driver no longer has must not read as a
# counter that never moved.
$watchFields = @{}
foreach ($w in $watch) { $watchFields[$w] = (Resolve-CounterLabel -Table $table -Label $w) }

# ------------------------------------------------------------------ helpers ---

function Get-FileLength {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    return (Get-Item -LiteralPath $Path).Length
}

# A step's slice of a log, by byte range.  QEMU's trace and debug-console files
# carry no timestamps and no markers, so "which lines belong to this step" is not
# answerable after the fact - it has to be recorded WHILE the step runs.  Marking
# the offsets at the boundaries is exact and needs nothing from QEMU.
function Get-LogSlice {
    param([string]$Path, [long]$From, [long]$To)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    if ($To -le $From) { return @() }
    $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $fs.Seek($From, [IO.SeekOrigin]::Begin) | Out-Null
        $buf = New-Object byte[] ([int]($To - $From))
        $got = $fs.Read($buf, 0, $buf.Length)
        $text = [Text.Encoding]::ASCII.GetString($buf, 0, $got)
    } finally { $fs.Dispose() }
    return (($text -split "`r?`n") | Where-Object { $_.Trim() -ne "" })
}

function Get-TraceEventTally {
    param([string[]]$Lines)
    $tally = [ordered]@{}
    foreach ($l in $Lines) {
        # log-backend lines are `<pid>@<time> <event> <args...>` on some builds
        # and bare `<event> <args...>` on others.  Take the first token that IS
        # one of our events rather than assuming a column.
        $name = ""
        foreach ($tok in ($l.Trim() -split '\s+')) {
            if ($traceEventList -contains $tok) { $name = $tok; break }
        }
        if ($name -eq "") { continue }
        if (-not $tally.Contains($name)) { $tally[$name] = 0 }
        $tally[$name]++
    }
    return $tally
}

# ------------------------------------------------------------------ the run ---
$reportLines = @()
$reportLines += "xhci98 - Finding 3's wedge, observed in QEMU"
$reportLines += ""
$reportLines += ("qemu         : {0}" -f $qemuVer)
$reportLines += ("target       : {0} ({1})" -f $tgt.Id, $tgt.Name)
$reportLines += ("dut port     : {0}" -f $DutPort)
$reportLines += ("settle       : {0} s per attach" -f $Settle)
$reportLines += ("cycles/arm   : {0}" -f $Repeat)
$reportLines += ("keep-alive   : {0}" -f $(if ($NoPump) { "none" } else { ("usb-mouse (High Speed) on root port {0} for the whole run, pumped before each attach" -f $pumpPort) }))
$reportLines += ""
$reportLines += "VEHICLE LIMIT, stated rather than left to be inferred from an absence: this"
$reportLines += "population has no isochronous model, so the C-Media's shape has no arm here;"
$reportLines += "and every arm detaches an IDLE device, so QEMU cannot separate 'has a periodic"
$reportLines += "endpoint' from 'had a transfer in flight at the disconnect'.  That separation is"
$reportLines += "a bench test - unplug a flash drive DURING a file copy - and is not owed here."
$reportLines += ""

foreach ($armName in $Arm) {
    # $armDef, NOT $arm.  PowerShell variable names are CASE-INSENSITIVE, so
    # `$arm` is the `[string[]]$Arm` parameter - and a typed parameter keeps its
    # constraint, so assigning this hashtable to it COERCES it to a string array.
    # `.What` and `.Devices` then read $null, the arm announced itself with a
    # blank description, and the device loop iterated nothing: a boot spent on an
    # arm that ran no steps and reported no error. Measured here.
    # The assertion below is what turns a recurrence into a line instead of a
    # silent empty arm.
    $armDef = $armDefs[$armName]
    if ($null -eq $armDef -or -not ($armDef -is [hashtable]) -or $armDef.Devices.Count -lt 2) {
        throw ("arm '{0}' did not resolve to a definition with at least two devices - it came back as [{1}]. An arm with no devices boots a guest and measures nothing." -f `
            $armName, $(if ($null -eq $armDef) { "null" } else { $armDef.GetType().Name }))
    }
    Write-Host ""
    Write-Host ("=== arm {0}: {1}" -f $armName, $armDef.What)

    Reset-MonitorErrors
    if (-not (Test-MonitorPortFree -Port $tgt.Monitor)) {
        throw ("something is already listening on monitor port {0}. That is almost certainly a guest left over from an earlier run, and this run would have driven IT. Stop it first." -f $tgt.Monitor)
    }

    $tag = "{0}-{1}" -f $tgt.Id, $armName
    $dbgLog   = Join-Path $OutDir ("wedge-{0}-debugcon.log" -f $tag)
    $traceLog = Join-Path $OutDir ("wedge-{0}-trace.log" -f $tag)
    $evFile   = Join-Path $OutDir ("wedge-{0}-events.txt" -f $tag)
    $stderrF  = Join-Path $OutDir ("wedge-{0}-qemu-stderr.log" -f $tag)
    foreach ($f in @($dbgLog, $traceLog)) {
        if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
    }
    Set-Content -LiteralPath $evFile -Value $traceEventList -Encoding ascii

    # As many scratch images and backends as this arm has storage attaches -
    # see the comment on $storHs for why one cannot be shared.
    $driveCount = 0
    foreach ($d in $armDef.Devices) { if ($d.ContainsKey('NeedsDrive') -and $d.NeedsDrive) { $driveCount++ } }
    $driveCount = $driveCount * $Repeat
    $driveArgs = @()
    for ($k = 1; $k -le $driveCount; $k++) {
        $scratch = Join-Path $OutDir ("wedge-{0}-scratch{1}.img" -f $tag, $k)
        if (-not (Test-Path -LiteralPath $scratch)) {
            $fsx = [IO.File]::Create($scratch); $fsx.SetLength(64MB); $fsx.Close()
        }
        $driveArgs += @("-drive", ("if=none,id=wedgedrv{0},file={1},format=raw" -f $k, $scratch))
    }

    # The launch is run-matrix.ps1's, minus the rows it does not have: no netdev
    # and no chardevs (nothing here needs them, and a command-line -netdev
    # changes the guest's PCI layout enough to stop Windows 2000 starting the
    # driver at all - bisected in Phase 10's matrix).  The block backends are appended
    # below rather than declared here, because how many there are is a property
    # of the arm; -snapshot goes after them all so it covers every one.
    $qargs = @(
        "-name", ("xhci98 wedge observatory - {0} {1}" -f $tgt.Id, $armName),
        "-machine", $tgt.Machine,
        "-cpu", $tgt.Cpu,
        "-m", "$($tgt.Memory)",
        "-drive", ("file={0},format={1},if=ide" -f (Join-Path $vmDir $tgt.Image), $tgt.Format),
        "-device", "qemu-xhci,id=xhci,p3=0",
        "-chardev", ("file,id=dbgcon,path={0}" -f $dbgLog),
        "-device", "isa-debugcon,iobase=0xe9,chardev=dbgcon",
        # ONE -trace argument with both keys; QEMU keeps the LAST -trace and
        # would silently discard the event list.
        "-trace", ("events={0},file={1}" -f $evFile, $traceLog),
        "-boot", "c",
        "-action", "reboot=reset", "-no-shutdown",
        "-monitor", ("tcp:127.0.0.1:{0},server=on,wait=off" -f $tgt.Monitor)
    )
    $qargs += $driveArgs
    if ($tgt.Accel -ne "") { $qargs += @("-accel", $tgt.Accel) }
    if ($tgt.ContainsKey('Smp') -and [int]$tgt.Smp -gt 1) { $qargs += @("-smp", "$([int]$tgt.Smp)") }
    # Never write to the guest image.  These arms are expected to be able to
    # wedge the guest; that must cost an arm, not an image.
    $useSnapshot = $true
    if ($cfg.ContainsKey('Snapshot')) { $useSnapshot = [bool]$cfg.Snapshot }
    if ($useSnapshot) { $qargs += "-snapshot" }
    Assert-SingleTraceArg -QemuArgs $qargs

    $proc = Start-Qemu -Qemu $qemuBin -QemuArgs $qargs -StderrFile $stderrF
    $armError = ""
    $script:armSteps = @()
    try {
        if (-not (Wait-Monitor -Port $tgt.Monitor -TimeoutSeconds 60)) {
            $err = Get-QemuStderr -StderrFile $stderrF
            if ($err -eq "") { $err = "(QEMU wrote nothing to stderr)" }
            throw ("the guest's monitor never answered. QEMU said: {0}" -f $err)
        }

        # The keep-alive goes on BEFORE the driver starts: attached afterwards it
        # lands on a controller Windows 98 has already idle-suspended, is never
        # enumerated, and then cannot keep anything awake.
        if (-not $NoPump) {
            Send-Checked -Port $tgt.Monitor -Command ("device_add usb-mouse,id=keepalive,bus=xhci.0,port={0}" -f $pumpPort) | Out-Null
        }

        Write-Host ("waiting for the driver to start (deadline {0} s)..." -f $tgt.BootSeconds)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $ident = $null
        while ($sw.Elapsed.TotalSeconds -lt $tgt.BootSeconds) {
            $ident = Find-ExtensionIdentity -DebugconLog $dbgLog
            if ($null -ne $ident.Va) { break }
            Start-Sleep -Seconds 3
        }
        if ($null -eq $ident -or $null -eq $ident.Va) {
            throw ("no 'cb ... a=<VA>' line in {0} after {1} s. Either the guest did not boot, or the driver did not load, or it is not the QEMU build - and this run needs that flavour for the 0xE9 mirror as much as for the counters, since task 13-L.1 left the mirror in no other one." -f $dbgLog, $tgt.BootSeconds)
        }
        Write-Host ("driver up: extension at 0x{0}, MiniPortExtensionSize={1}" -f $ident.Va, $ident.Size)
        if ($ident.Spans) {
            throw ("this arm's debug console log already spans more than one driver load or binary (VAs: {0}; sizes: {1})." -f ($ident.AllVas -join ", "), ($ident.AllSizes -join ", "))
        }
        if ($null -eq $ident.Size) {
            throw "the driver never printed MiniPortExtensionSize, so the offset table cannot be checked against it. A run on unchecked offsets is a run of wrong values."
        }
        Assert-OffsetsFresh -OffsetsFile $table.OffsetsFile -ExtensionSizeFromTrace $ident.Size | Out-Null

        $alive = Test-GuestAlive -Port $tgt.Monitor -Process $proc
        if (-not $alive.Alive) {
            throw ("the guest is not alive after boot [{0}]: {1} ({2})" -f $alive.Verdict, $alive.Why, $alive.Detail)
        }

        # "The driver started" is not "the guest is ready": StartController is
        # about 8 s into the boot, long before the PnP stack will bind anything.
        # Wait for the guest's own signal - a function driver opening a
        # non-default endpoint on the keep-alive.
        if (-not $NoPump) {
            $readySeconds = 300
            if ($tgt.ContainsKey('ReadySeconds')) { $readySeconds = [int]$tgt.ReadySeconds }
            Write-Host ("waiting for the guest to claim the keep-alive (deadline {0} s)..." -f $readySeconds)
            $epField = Resolve-CounterLabel -Table $table -Label 'endpoints opened'
            $sw2 = [Diagnostics.Stopwatch]::StartNew()
            $ready = $false
            while ($sw2.Elapsed.TotalSeconds -lt $readySeconds) {
                $probe = Read-Counters -Port $tgt.Monitor -BaseVa $ident.Va -Table $table -Process $proc
                if ($probe.Values[$epField] -ge 1) { $ready = $true; break }
                Start-Sleep -Seconds 10
            }
            if (-not $ready) {
                $shot = Save-GuestScreenshot -Port $tgt.Monitor -Path (Join-Path $OutDir ("wedge-{0}-notready.ppm" -f $tag))
                throw ("no function driver opened an endpoint on the keep-alive within {0} s - on Windows 98 that is almost certainly the modal Add New Hardware Wizard. Screenshot: {1}" -f $readySeconds, $shot)
            }
            Write-Host ("guest ready after {0} s" -f [int]$sw2.Elapsed.TotalSeconds)
        }

        # --- the sequence -------------------------------------------------
        # One step = one attach or one detach, each with its own counter window
        # and its own slice of both logs.  A detach is a step in its own right
        # and not the tail of the attach before it, because the bench's
        # localisation says the damage is DONE by the disconnect and is only WORN
        # by the enumeration after it.
        function Invoke-WedgeStep {
            param(
                [Parameter(Mandatory = $true)][string]$Label,
                [Parameter(Mandatory = $true)][ValidateSet("add", "del")][string]$Do,
                [Parameter(Mandatory = $true)][string]$Id,
                [string]$Model = "",
                [string]$AddArgs = "",
                [Parameter(Mandatory = $true)][int]$Wait,
                [int]$Ordinal = 0
            )
            Write-Host ""
            Write-Host ("--- {0}" -f $Label)
            if (-not $NoPump) { Invoke-Pump -Port $tgt.Monitor -Seconds 2 }

            $tFrom = Get-FileLength -Path $traceLog
            $dFrom = Get-FileLength -Path $dbgLog
            $before = Read-Counters -Port $tgt.Monitor -BaseVa $ident.Va -Table $table -Process $proc

            $err = ""
            $errBefore = Get-MonitorErrors
            if ($Do -eq "add") {
                $cmd = "device_add {0},id={1},bus=xhci.0,port={2}" -f $Model, $Id, $DutPort
                if ($AddArgs -ne "") { $cmd = "{0},{1}" -f $cmd, $AddArgs }
                Send-Checked -Port $tgt.Monitor -Command $cmd | Out-Null
                if ((Get-MonitorErrors) -gt $errBefore) {
                    $err = "device_add was refused; nothing was attached"
                } else {
                    # An attach is only an attach if the device arrived.
                    $onBus = $false
                    for ($w = 0; $w -lt 20; $w++) {
                        $now = (Get-MonitorText -Port $tgt.Monitor -Command "info usb") -join " "
                        if ($now -match ('ID:\s*' + [regex]::Escape($Id) + '\b')) { $onBus = $true; break }
                        Start-Sleep -Milliseconds 500
                    }
                    if (-not $onBus) {
                        $err = "device_add returned no error but the device never appeared on the bus"
                    } else {
                        # `info usb` lists a device that OCCUPIES a port whether
                        # or not it is electrically attached; the attach state is
                        # the other half, and only the pair is sufficient.
                        $path = "/machine/peripheral/$Id"
                        $state = ((Get-MonitorText -Port $tgt.Monitor -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
                        if ($state -notmatch "(?i)\btrue\b") {
                            Send-Checked -Port $tgt.Monitor -Command ("qom-set {0} attached true" -f $path) | Out-Null
                            Start-Sleep -Milliseconds 500
                            $state = ((Get-MonitorText -Port $tgt.Monitor -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
                            if ($state -notmatch "(?i)\btrue\b") {
                                $err = ("id={0} occupies the root port but is not electrically attached (now '{1}')" -f $Id, $state)
                            }
                        }
                    }
                }
            } else {
                Send-Checked -Port $tgt.Monitor -Command ("device_del {0}" -f $Id) | Out-Null
                # A pull is only a pull if the device left.
                # Only a COMPLETE reply that omits the id says it left; an
                # unanswered `info usb` (a wedged guest, here of all places)
                # prints the same nothing an empty bus does.
                $gone = $false
                $unanswered = 0
                for ($w = 0; $w -lt 40; $w++) {
                    $listed = Test-UsbDeviceListed -Port $tgt.Monitor -Id $Id
                    if ($listed -eq $false) { $gone = $true; break }
                    if ($null -eq $listed) { $unanswered++ }
                    Start-Sleep -Milliseconds 500
                }
                if (-not $gone) {
                    $err = if ($unanswered -gt 0) {
                        ("device_del could not be confirmed - `info usb` gave no complete reply {0} time(s) in 20 s" -f $unanswered)
                    } else {
                        "device_del did not take - the device was still on the bus 20 s later"
                    }
                }
            }

            Start-Sleep -Seconds $Wait

            # LIVENESS IS PART OF EVERY STEP, not only of the one expected to
            # fail.  On the E460 the wedge is machine-wide and terminal, so the
            # step that first fails to answer IS the finding.
            $aliveNow = Test-GuestAlive -Port $tgt.Monitor -Process $proc
            $after = $null
            if ($aliveNow.Alive) {
                $after = Read-Counters -Port $tgt.Monitor -BaseVa $ident.Va -Table $table -Process $proc
            }

            $tTo = Get-FileLength -Path $traceLog
            $dTo = Get-FileLength -Path $dbgLog
            $traceSlice = Get-LogSlice -Path $traceLog -From $tFrom -To $tTo
            $dbgSlice   = Get-LogSlice -Path $dbgLog   -From $dFrom -To $dTo

            $delta = $null
            if ($null -ne $after) { $delta = Get-CounterDelta -Before $before -After $after }

            $step = [pscustomobject]@{
                Arm        = $armName
                Label      = $Label
                Do         = $Do
                Ordinal    = $Ordinal
                Id         = $Id
                Error      = $err
                Alive      = $aliveNow.Alive
                AliveWhy   = ("{0}: {1}" -f $aliveNow.Verdict, $aliveNow.Why)
                Delta      = $delta
                TraceLines = $traceSlice
                DbgLines   = $dbgSlice
                TraceTally = (Get-TraceEventTally -Lines $traceSlice)
            }
            $script:armSteps += $step

            if ($err -ne "") { Write-Host ("  *** {0}" -f $err) }
            if (-not $aliveNow.Alive) { Write-Host ("  *** GUEST NOT ALIVE: {0}" -f $step.AliveWhy) }
            if ($null -ne $delta) {
                foreach ($w in @('devices addressed', 'slots enabled', 'slots disabled', 'endpoints opened', 'port event changes')) {
                    Write-Host ("    {0,-24} {1,6}" -f $w, $delta.Values[$watchFields[$w]])
                }
            }
            return $step
        }

        $stop = $false
        $attachOrdinal = 0
        $driveOrdinal = 0
        for ($cycle = 1; $cycle -le $Repeat -and -not $stop; $cycle++) {
            $slot = 0
            foreach ($dev in $armDef.Devices) {
                $slot++
                $attachOrdinal++
                $id = "d{0}s{1}" -f $cycle, $slot
                $mark = ""
                if ($attachOrdinal -eq 1) { $mark = "  (the baseline enumeration)" }
                elseif ($slot -eq $armDef.Devices.Count) { $mark = "  (*** the enumeration that fails on metal ***)" }

                # `{DRIVE}` resolves here and nowhere else: which backend this
                # attach gets is only known once we know how many storage
                # attaches came before it in this boot.
                $addArgs = $dev.AddArgs
                if ($dev.ContainsKey('NeedsDrive') -and $dev.NeedsDrive) {
                    $driveOrdinal++
                    $addArgs = $addArgs -replace '\{DRIVE\}', ("wedgedrv{0}" -f $driveOrdinal)
                }
                $stAdd = Invoke-WedgeStep -Ordinal $attachOrdinal `
                    -Label ("cycle {0} attach {1}: {2} IN  [{3}]{4}" -f $cycle, $slot, $dev.Name, $dev.Ep, $mark) `
                    -Do "add" -Id $id -Model $dev.Model -AddArgs $addArgs -Wait $Settle
                if (-not $stAdd.Alive) { $armError = ("the guest stopped executing at '{0}' - {1}" -f $stAdd.Label, $stAdd.AliveWhy); $stop = $true; break }
                if ($stAdd.Error -ne "") { $armError = ("step '{0}' did not complete: {1}" -f $stAdd.Label, $stAdd.Error); $stop = $true; break }

                $stDel = Invoke-WedgeStep -Ordinal $attachOrdinal `
                    -Label ("cycle {0} attach {1}: {2} OUT  (*** the disconnect the bench says does the damage ***)" -f $cycle, $slot, $dev.Name) `
                    -Do "del" -Id $id -Wait 8
                if (-not $stDel.Alive) { $armError = ("the guest stopped executing at '{0}' - {1}" -f $stDel.Label, $stDel.AliveWhy); $stop = $true; break }
                if ($stDel.Error -ne "") { $armError = ("step '{0}' did not complete: {1}" -f $stDel.Label, $stDel.Error); $stop = $true; break }
            }
        }

        $shot = Save-GuestScreenshot -Port $tgt.Monitor -Path (Join-Path $OutDir ("wedge-{0}-final.ppm" -f $tag))
        if ($null -ne $shot) { Write-Host ("screenshot: {0}" -f $shot) }
    } catch {
        $armError = $_.Exception.Message
        Write-Host ("*** arm {0} ended: {1}" -f $armName, $armError)
    } finally {
        if ($KeepGuest -and $armError -ne "") {
            Write-Host ("-KeepGuest: leaving the guest running on monitor port {0}" -f $tgt.Monitor)
        } else {
            Send-Mon -Port $tgt.Monitor -Command "quit" -Quiet | Out-Null
            Start-Sleep -Seconds 2
            if ($null -ne $proc -and -not $proc.HasExited) { try { $proc.Kill() } catch { } }
        }
    }

    $armSteps = @($script:armSteps)

    # ------------------------------------------------------------- report ---
    $reportLines += "================================================================"
    $reportLines += ("ARM {0}" -f $armName)
    $reportLines += ("  {0}" -f $armDef.What)
    $reportLines += ("  sequence (all on root port {0}, one at a time, each pulled before the next):" -f $DutPort)
    $i = 0
    foreach ($d in $armDef.Devices) { $i++; $reportLines += ("    {0}. {1,-12} {2,-5} Speed   {3}" -f $i, $d.Name, $d.Speed, $d.Ep) }
    if ($armError -ne "") { $reportLines += ("  ARM ENDED EARLY: {0}" -f $armError) }
    $reportLines += ""

    # The side-by-side that answers the question: the FIRST attach against the
    # LAST attach.  Both are attaches of a device on the same port; everything
    # between them is the variable.
    $adds  = @($armSteps | Where-Object { $_.Do -eq 'add' })
    $dels  = @($armSteps | Where-Object { $_.Do -eq 'del' })
    $first = if ($adds.Count -ge 1) { $adds[0] } else { $null }
    $last  = if ($adds.Count -ge 2) { $adds[-1] } else { $null }
    $lastDel = if ($dels.Count -ge 1) { $dels[-1] } else { $null }
    $midDel  = if ($dels.Count -ge 2) { $dels[-2] } else { $null }

    $reportLines += "  counter deltas: the first attach, the disconnect before the last, and the last attach"
    $reportLines += ("  {0,-42} {1,10} {2,10} {3,10}" -f "counter", "first IN", "prev OUT", "last IN")
    foreach ($w in $watch) {
        $f = $watchFields[$w]
        $v1 = if ($null -ne $first   -and $null -ne $first.Delta)   { $first.Delta.Values[$f]   } else { "-" }
        $v4 = if ($null -ne $midDel  -and $null -ne $midDel.Delta)  { $midDel.Delta.Values[$f]  } else { "-" }
        $v5 = if ($null -ne $last    -and $null -ne $last.Delta)    { $last.Delta.Values[$f]    } else { "-" }
        if ("$v1$v4$v5" -match '^0*$') { continue }   # counters that never moved stay out of the diff
        $reportLines += ("  {0,-42} {1,10} {2,10} {3,10}" -f $w, $v1, $v4, $v5)
    }
    $reportLines += ""

    # The verdict, stated as what was measured rather than as a hope.  A NEGATIVE
    # here is a RESULT - see the header - and is written as one.
    #
    # TWO FALSE-POSITIVE GUARDS, AND THE WEDGE RUN PAID FOR BOTH.  The
    # `hs-mid` arm printed "REPRODUCED - the last enumeration did not address a
    # device" and it was a VEHICLE ARTEFACT: a modal Add New Hardware Wizard for
    # `USB Mass Storage Device` had blocked the OS bind on the arm's FIRST
    # attach, and a blocked bind also silently kills the keep-alive pump -
    # `mouse_move` only reaches the wire once a function driver holds the
    # pointer's interrupt endpoint - so every later hot-plug was invisible to
    # the stack.  The screenshot said so and the verdict line did not.
    #
    #   1. A BASELINE THE OS NEVER BOUND IS NOT A BASELINE.  `devices addressed`
    #      alone says this driver enumerated the device; `endpoints opened` is
    #      what says the OS claimed it.  The wizard arm had the first and not
    #      the second, which is exactly the matrix's NODRIVER outcome, and an
    #      arm in that state can only ever compare a bound attach against an
    #      unbound one.
    #   2. THE FIRST FAILING ATTACH IS THE FINDING, NOT THE LAST.  Comparing
    #      first against last named attach 3 when the break was at attach 2, so
    #      even the step it reported was wrong.  Every attach after the first is
    #      checked, and the earliest failure is the one named.
    #
    # A guest still announcing port changes is also the OPPOSITE of the E460's
    # signature, where port-change events stop arriving - so that is said in the
    # reading rather than left for a reader to notice.
    $verdict = "INCONCLUSIVE"
    $why = ""
    $addrF = $watchFields['devices addressed']
    $slotF = $watchFields['slots enabled']
    $epF   = $watchFields['endpoints opened']
    $pecF  = $watchFields['port event changes']
    if ($armSteps.Count -gt 0 -and -not ($armSteps[-1].Alive)) {
        $verdict = "REPRODUCED - the guest stopped executing"
        $why = $armError
    } elseif ($null -eq $first -or $null -eq $first.Delta -or $adds.Count -lt 2) {
        $why = "the arm did not reach two attaches with a readable counter window"
    } elseif ($first.Delta.Values[$addrF] -lt 1) {
        $verdict = "VOID - no baseline"
        $why = ("the FIRST attach addressed {0} devices, so this arm never established the comparison it exists to make" -f $first.Delta.Values[$addrF])
    } elseif ($first.Delta.Values[$epF] -lt 1) {
        # NODRIVER, in the matrix's sense, and it voids the arm rather than
        # failing it: this driver did its half and the OS did not do its.
        $verdict = "VOID - the baseline was never bound by the guest"
        $why = ("the first attach addressed a device ({0}) and opened {1} endpoints, so the OS never claimed it - on Windows 98 that is the modal Add New Hardware Wizard, which blocks the bind AND kills the keep-alive pump, making every later hot-plug invisible. Read the final screenshot before reading anything else in this arm; every attach after this one measures a stalled PnP stack rather than this driver." -f `
            $first.Delta.Values[$addrF], $first.Delta.Values[$epF])
    } else {
        $failed = $null
        foreach ($a in $adds[1..($adds.Count - 1)]) {
            if ($null -eq $a.Delta -or $a.Delta.Values[$addrF] -lt 1) { $failed = $a; break }
        }
        if ($null -ne $failed) {
            $verdict = "REPRODUCED - an enumeration after the baseline did not address a device"
            $stillAnnouncing = ($null -ne $failed.Delta -and $failed.Delta.Values[$pecF] -ge 1)
            $why = ("the baseline addressed {0} (slots enabled {1}); the FIRST attach that did not is '{2}'{3}" -f `
                $first.Delta.Values[$addrF], $first.Delta.Values[$slotF], $failed.Label, `
                $(if ($stillAnnouncing) { " - and note that port changes were STILL being announced there, which is the OPPOSITE of the E460's signature and points at the guest's PnP stack rather than at the controller. Check the final screenshot for a modal wizard before reading this as the fault." } else { "" }))
        } else {
            $lAddr = $last.Delta.Values[$addrF]
            $verdict = "NEGATIVE - every enumeration after the baseline succeeded as the first did"
            $why = ("devices addressed {0} then {1} on the last; slots enabled {2} then {3}. On this vehicle the sequence that fails on the E460 does not fail, which is a RESULT and not a silence: it means QEMU's xHCI model tolerates whatever real silicon halts on, so the per-step traces beside this report - not this line - are what the run is for." -f `
                $first.Delta.Values[$addrF], $lAddr, $first.Delta.Values[$slotF], $last.Delta.Values[$slotF])
        }
    }
    $reportLines += ("  VERDICT: {0}" -f $verdict)
    $reportLines += ("           {0}" -f $why)

    # THE ACCOUNTING THAT MATTERS EVEN WHEN THE VERDICT IS NEGATIVE, and it is
    # the whole reason the run is worth taking on a forgiving vehicle: over the
    # arm as a whole, did every slot that was enabled get disabled, and did every
    # endpoint that was enabled get disabled?  A residue here is the defect
    # showing itself on a controller too tolerant to die of it.
    $tally = [ordered]@{}
    foreach ($st in $armSteps) {
        foreach ($k in $st.TraceTally.Keys) {
            if (-not $tally.Contains($k)) { $tally[$k] = 0 }
            $tally[$k] += $st.TraceTally[$k]
        }
    }
    function TallyOf { param($T, [string]$K) if ($T.Contains($K)) { return [int]$T[$K] } else { return 0 } }
    $slEn = TallyOf $tally 'usb_xhci_slot_enable'
    $slDi = TallyOf $tally 'usb_xhci_slot_disable'
    $epEn = TallyOf $tally 'usb_xhci_ep_enable'
    $epDi = TallyOf $tally 'usb_xhci_ep_disable'
    $reportLines += ""
    $reportLines += "  arm-wide lifecycle accounting, from QEMU's own events:"
    $reportLines += ("    slot_enable {0}  slot_disable {1}   {2}" -f $slEn, $slDi, `
        $(if ($slDi -ge $slEn) { "every slot enabled was disabled" } else { ("*** {0} slot(s) enabled and never disabled ***" -f ($slEn - $slDi)) }))
    $reportLines += ("    ep_enable   {0}  ep_disable   {1}   {2}" -f $epEn, $epDi, `
        $(if ($epDi -ge $epEn) { "every endpoint enabled was disabled" } else { ("*** {0} endpoint(s) enabled and never disabled ***" -f ($epEn - $epDi)) }))
    $reportLines += ("    (slot_disable also fires 64 times per controller reset, so a surplus there is a reset, not a leak)")
    $reportLines += ""

    # Per-step detail, and the trace tally that is the actual deliverable.
    foreach ($st in $armSteps) {
        $reportLines += ("  --- {0}" -f $st.Label)
        if ($st.Error -ne "") { $reportLines += ("      ERROR: {0}" -f $st.Error) }
        if (-not $st.Alive)   { $reportLines += ("      GUEST NOT ALIVE: {0}" -f $st.AliveWhy) }
        if ($st.TraceTally.Count -eq 0) {
            $reportLines += "      qemu trace: (no events in this step's slice)"
        } else {
            $reportLines += "      qemu trace:"
            foreach ($k in $st.TraceTally.Keys) { $reportLines += ("        {0,-26} {1}" -f $k, $st.TraceTally[$k]) }
        }
        if ($st.DbgLines.Count -gt 0) {
            $reportLines += "      driver 0xE9 mirror:"
            foreach ($l in $st.DbgLines) { $reportLines += ("        {0}" -f $l.Trim()) }
        }
        if ($null -ne $st.Delta) {
            $moved = @()
            foreach ($w in $watch) {
                $v = $st.Delta.Values[$watchFields[$w]]
                if ($v -ne 0) { $moved += ("{0} {1:+#;-#;0}" -f $w, $v) }
            }
            if ($moved.Count -gt 0) { $reportLines += ("      counters: {0}" -f ($moved -join "; ")) }
        }
        $reportLines += ""
    }

    # Every step's raw trace slice, whole, beside the report.  The tally says how
    # many; this says which, in order, and it is what the slot and endpoint
    # questions are actually read out of.
    $sliceFile = Join-Path $OutDir ("wedge-{0}-steps.txt" -f $tag)
    $sliceOut = @()
    foreach ($st in $armSteps) {
        $sliceOut += ("========== {0}" -f $st.Label)
        $sliceOut += "---------- qemu trace"
        $sliceOut += $st.TraceLines
        $sliceOut += "---------- driver 0xE9 mirror"
        $sliceOut += $st.DbgLines
        $sliceOut += ""
    }
    Set-Content -LiteralPath $sliceFile -Value $sliceOut -Encoding ascii
    Write-Host ("per-step slices: {0}" -f $sliceFile)
}

$reportFile = Join-Path $OutDir ("wedge-observe-{0}.txt" -f $tgt.Id)
Set-Content -LiteralPath $reportFile -Value $reportLines -Encoding ascii
Write-Host ""
Write-Host ($reportLines -join [Environment]::NewLine)
Write-Host ""
Write-Host ("report: {0}" -f $reportFile)
