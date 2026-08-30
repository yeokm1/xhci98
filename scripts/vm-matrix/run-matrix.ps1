<#
.SYNOPSIS
Phase 10 task 10.3 - run the USB device matrix unattended and emit a diffable
per-device report.

.DESCRIPTION
Boots each target VM, walks every row of scripts\vm-matrix\matrix.psd1,
and decides each row's outcome against the expectations that file states in
advance.  The design is docs\contributing\design\06-device-matrix-verdict.md; this
script implements it and adds nothing to it.

Per group: one boot.  Per row inside a group: read counters, attach the device,
let it settle, read counters, detach, wait for the bus to empty, read counters,
evaluate.  A group is the blast radius - a device that wedges the guest ends its
group and not the run.

WHAT THE GUEST MUST ALREADY HAVE.  The QEMU build of xhci98.sys, installed and
bound.  *(It was the DEBUG build until task 13-L.1 split a third flavour out.  This harness reads `cb ... a=<VA>` off the port-0xE9 debug console,
and BOTH halves of that are now qemu-only: XHCI_DBG_CB compiles to nothing
outside qemu, and the 0xE9 sink itself is gated on the same define in
src\sources.  A debug-flavour guest reaches the identity wait below and times
out with no line at all.)*  The harness reads counters out of the miniport
extension by offset
and cross-checks the offset table's SIZEOF against the MiniPortExtensionSize the
running driver prints - so a guest running a different build than the source
tree was built from is REFUSED rather than measured.  That check is the whole
reason a stale reading cannot be mistaken for a wrong value.

.PARAMETER ReportName
The Phase 10 report's file name under the output directory.  The post-release
run ignores it: that run writes one report per target, named
post-release-<target>.txt, and says so when the switch is given anyway.

.PARAMETER ValidateOnly
Parse the matrix, resolve every expectation against the counter table, check
the config's images and QEMU, test that every monitor port can be bound, and
stop.  Boots nothing.  Run this after any change to the driver's counters or
to the matrix.

.PARAMETER PostRelease
THE POST-RELEASE RUN (Phase 16, task 16.1; design record
docs\contributing\design\09-post-release-unattended-run.md).  The same walk,
on the FRESH targets only - those the config marks with `CloneFrom` - with
four differences the design states: every image's `base-<DriverVer>-qemu`
stamp is checked before anything boots and the run refuses without it; every
row is attached, detached and attached AGAIN, and the replug is judged against
the same expectations; a NODRIVER row counts against the target unless the
matrix carries an `ExpectNoDriver` entry for it; and each target gets its own
report with a header block, under out\post-release\<DriverVer>\.  The exit
code is the targets' verdicts.  -ValidateOnly with this switch runs the stamp
checks too and boots nothing.

.EXAMPLE
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -ValidateOnly
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -Target 2b -Group hid
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -PostRelease
#>
[CmdletBinding()]
param(
    [string]$Config = "",
    [string]$Matrix = "",
    [string]$Qemu = "",
    [string[]]$Target = @(),
    [string[]]$Group = @(),
    [string]$OutDir = "",
    [string]$ReportName = "device-matrix.txt",
    [switch]$ValidateOnly,
    [switch]$PostRelease,
    [switch]$KeepGuestOnFailure,
    # The root port every device under test is attached to. Windows 98 keys a
    # devnode by bus location, so this must match where a prep pass taught the
    # device - see the comment at the device_add.
    [int]$DutPort = 2
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\monitor.ps1")
. (Join-Path $PSScriptRoot "lib\qemu.ps1")
. (Join-Path $PSScriptRoot "lib\counters.ps1")
. (Join-Path $PSScriptRoot "lib\verdict.ps1")
. (Join-Path $PSScriptRoot "lib\fresh.ps1")

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
    throw ("no configuration found. Copy scripts\vm-matrix\config.sample.psd1, edit the paths, and pass it with -Config.")
}
$cfg = Import-PowerShellDataFile -LiteralPath (Resolve-RepoPath $Config)
if ($Matrix -eq "") { $Matrix = Join-Path $PSScriptRoot "matrix.psd1" }
$mx = Import-PowerShellDataFile -LiteralPath (Resolve-RepoPath $Matrix)
# The version under test, from the single source the packager reads.  The
# post-release run's output directory and every stamp check are keyed by it.
$version = Get-DriverVersionUnderTest -RepoRoot $repo
if ($OutDir -eq "") {
    if ($PostRelease) {
        $base = if ($cfg.ContainsKey('PostReleaseOutDir') -and $cfg.PostReleaseOutDir) { $cfg.PostReleaseOutDir } else { 'out\post-release' }
        $OutDir = Join-Path $base $version
    } else {
        $OutDir = $cfg.OutDir
    }
}
$OutDir = Resolve-RepoPath $OutDir
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$qemuHint = if ($Qemu -ne "") { $Qemu } else { $cfg.Qemu }
$qemuBin = Resolve-QemuBinary -Hint $qemuHint
$qemuVer = Get-QemuVersion -Qemu $qemuBin
$vmDir = Resolve-RepoPath $cfg.VmDir

Write-Host ("qemu   : {0} ({1})" -f $qemuBin, $qemuVer)
Write-Host ("vm dir : {0}" -f $vmDir)
Write-Host ("matrix : {0}" -f (Resolve-RepoPath $Matrix))
Write-Host ("out    : {0}" -f $OutDir)
Write-Host ("version: {0}{1}" -f $version, $(if ($PostRelease) { "  (post-release run)" } else { "" }))
if ($PostRelease -and $PSBoundParameters.ContainsKey('ReportName')) {
    Write-Host ("note   : -ReportName '{0}' is ignored by the post-release run, which writes post-release-<target>.txt per target" -f $ReportName)
}

# ----------------------------------------------------------------- validate ---
# Everything checkable without a boot is checked before one is spent.  A matrix
# whose expectations do not resolve, a model this QEMU build does not have, or a
# missing image are all one line here and a wasted twenty minutes otherwise.
$table = Import-CounterTable
$available = Get-QemuUsbModels -Qemu $qemuBin
$problems = @()
$rowCount = 0
$parsed = @{}

# Every key a per-target entry may use: the target ids, plus whatever a fresh
# target inherits from through `Like` (lib\fresh.ps1, Get-TargetKeys).
$targetIds = @()
foreach ($t in $cfg.Targets) { $targetIds += (Get-TargetKeys -Target $t) }
$targetIds = @($targetIds | Sort-Object -Unique)

foreach ($g in $mx.Groups) {
    foreach ($r in $g.Rows) {
        $rowCount++
        if ($available -notcontains $r.Model) {
            $problems += ("row {0}: this QEMU build has no device model '{1}'" -f $r.Name, $r.Model)
        }
        $problems += (Get-RowWedgeProblems -Row $r -TargetIds $targetIds)
        $problems += (Get-RowNoDriverProblems -Row $r -KnownKeys $targetIds)
        $texts = @()
        $texts += $mx.Always
        if ($r.ContainsKey('Expect')) { $texts += $r.Expect }
        foreach ($t in $cfg.Targets) {
            $per = @()
            if ($r.ContainsKey('ExpectByTarget')) {
                $perKey = Find-TargetKey -Table $r.ExpectByTarget -Target $t
                if ($null -ne $perKey) { $per = $r.ExpectByTarget[$perKey] }
            }
            $key = "{0}|{1}" -f $r.Name, $t.Id
            $parsed[$key] = @()
            foreach ($txt in ($texts + $per)) {
                try {
                    $parsed[$key] += (ConvertTo-Expectation -Text $txt -Table $table)
                } catch {
                    $problems += ("row {0} [{1}]: {2}" -f $r.Name, $t.Id, $_.Exception.Message)
                }
            }
        }
    }
}

# A FILTER THAT MATCHES NOTHING IS AN ERROR, NOT AN EMPTY RUN.
#
# `powershell -File run-matrix.ps1 -Group storage,hub` passes ONE string
# "storage,hub" - `-File` does not parse array arguments - so the filter matched
# no group, the runner walked nothing, printed an empty summary and exited 0.
# A green run that did nothing is worse than a red one: it is hub7bv0's defect 2
# in a new place.  Both filters are split on commas so the natural CLI form
# works, and both are checked against what the matrix actually holds.
$Target = @($Target | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne "" } | ForEach-Object { $_.Trim() })
$Group  = @($Group  | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne "" } | ForEach-Object { $_.Trim() })

# THE TWO KINDS OF RUN BOOT DISJOINT SETS OF TARGETS.  A fresh target (one with
# `CloneFrom`) is the post-release run's and nobody else's: the ordinary matrix
# never boots it, because its verdict rules differ, and the post-release run
# boots nothing else, because Phase 10's images are not fresh.  Naming a target
# from the other set with -Target is an error, not a silent skip.  The split
# and its refusals are Select-RunTargets in lib\fresh.ps1, which selftest.ps1
# drives without a config.
$targetsToRun = @(Select-RunTargets -Targets $cfg.Targets -PostRelease ([bool]$PostRelease) -Requested $Target)
if ($Group.Count -gt 0) {
    $known = @($mx.Groups | ForEach-Object { $_.Name })
    $unknown = @($Group | Where-Object { $known -notcontains $_ })
    if ($unknown.Count -gt 0) {
        throw ("-Group '{0}' names no group in this matrix. It has: {1}" -f `
            ($unknown -join ","), ($known -join ", "))
    }
}
# The stamp each fresh image carried at validation, for the report header.
# Read once, here: at report time the image may still be held by a guest kept
# with -KeepGuestOnFailure, and qemu-img cannot open it then.
$imageStampByTarget = @{}
foreach ($t in $targetsToRun) {
    # The monitor port must be bindable, or the boot ends sixty seconds later
    # on "the monitor never answered" with the real cause in QEMU's stderr.
    $bind = Test-MonitorPortBindable -Port ([int]$t.Monitor)
    if ($bind -ne "") { $problems += ("target {0}: monitor {1}" -f $t.Id, $bind) }
    $img = Join-Path $vmDir $t.Image
    if (-not (Test-Path -LiteralPath $img)) { $problems += ("target {0}: image not found: {1}" -f $t.Id, $img); continue }
    # THE STAMP CHECK, before a boot is spent (design record 09 section 3.3).
    # Read with qemu-img, which opens the image without booting it; every
    # refusal it can make is listed by Get-FreshImageProblems and each is
    # shown to fire in selftest.ps1.
    if ($PostRelease) {
        $qemuImg = Join-Path (Split-Path -Parent $qemuBin) "qemu-img.exe"
        if (-not (Test-Path -LiteralPath $qemuImg)) { $problems += ("qemu-img.exe not found beside {0}; the stamp cannot be read" -f $qemuBin); continue }
        $snaps = @(Get-ImageSnapshots -QemuImg $qemuImg -Image $img)
        foreach ($p in (Get-FreshImageProblems -ImagePath $img -Snapshots $snaps -Version $version -Flavour "qemu")) {
            $problems += ("target {0}: {1}" -f $t.Id, $p)
        }
        if ($snaps.Count -gt 0) { $imageStampByTarget[$t.Id] = $snaps[$snaps.Count - 1].Tag }
        # Design record 09 section 6: refuse to write to the image it
        # booted.  Not overridden quietly - a config that says "persist"
        # is a config for the preparation, and this is the run.
        $snapshotOff = Get-SnapshotOffProblem -Config $cfg -TargetId $t.Id
        if ($null -ne $snapshotOff) { $problems += $snapshotOff }
    }
}

Write-Host ""
Write-Host ("validation: {0} rows, {1} counters in the table, {2} problem(s)" -f $rowCount, $table.Offsets.Count, $problems.Count)
foreach ($p in $problems) { Write-Host ("  *** {0}" -f $p) }
if ($problems.Count -gt 0) { throw "the matrix does not validate; nothing was booted." }
if ($ValidateOnly) {
    Write-Host "validate-only: nothing was booted."
    exit 0
}

# --------------------------------------------------------------- the runner ---
$report = @()

# Backends a row's device needs, added through the monitor rather than declared
# on the command line, and added ONCE per boot.  See the comment on the QEMU
# argument list for why a command-line `-netdev` is not an option: it removes
# QEMU's default NIC and the resulting PCI layout change stops Windows 2000
# starting the driver at all.  A monitor `netdev_add` after the guest is up
# creates no PCI device and disturbs nothing.
#
# ONCE PER BOOT, which is what the reset at the top of the group loop below
# enforces.  Declaring it here only initialises it; the cache must not outlive
# the QEMU process whose backends it is a record of.
$script:backendsAdded = @{}
function Add-RowBackends {
    param([Parameter(Mandatory = $true)][int]$Port, [Parameter(Mandatory = $true)]$Row)
    $wanted = @()
    if ($Row.ContainsKey('NeedsNetdev') -and $Row.NeedsNetdev) { $wanted += "netdev_add user,id=matrixnet" }
    # A FILE CHARDEV, NOT `null`.  `usb-serial` and `usb-braille` attach
    # themselves to the bus only when their chardev is open, and a `null`
    # chardev never is: with it both sat in their port with `attached=false`,
    # `qom-set attached true` is refused for these models ("not writable"),
    # and the row read an empty bus.  Phase 10 recorded that as `FAIL +0` on
    # both targets and wrote `ExpectNoDriver` entries on it; the first
    # post-release run (2026-08-30) read it as ERROR, which is the honest word.
    # Measured with qom-get on QEMU 11: null -> false, file -> true, for both.
    if ($Row.ContainsKey('NeedsChardev')) {
        $chrPath = Join-Path $OutDir ("matrix-{0}-chr{1}.log" -f $tag, $Row.NeedsChardev)
        $wanted += ("chardev-add file,id=matrixchr{0},path={1}" -f $Row.NeedsChardev, $chrPath)
    }
    foreach ($cmd in $wanted) {
        if ($script:backendsAdded.ContainsKey($cmd)) { continue }
        # **Cached only if it actually took** (repo audit D4).  The cache exists
        # to stop a second `netdev_add` for the same id, and it used to record
        # the attempt rather than the outcome - so a backend QEMU REFUSED was
        # remembered as present, every later row that needed it skipped the
        # retry, and each of them failed on a missing backend with nothing
        # naming the one refusal that caused all of them.  `Send-Checked`
        # already counts a refusal; the delta is what says whether this one was
        # refused.
        $errBefore = Get-MonitorErrors
        Send-Checked -Port $Port -Command $cmd | Out-Null
        if ((Get-MonitorErrors) -eq $errBefore) {
            $script:backendsAdded[$cmd] = $true
        }
    }
}

# RELEASE THE ROW'S SCSI CHILD, BEFORE ITS ADAPTER.
#
# WHAT THIS DOES NOT FIX, because a first version of it claimed to.  When
# `usb-uas/fs` failed on `Property 'scsi-hd.drive' can't find value
# 'matrixdrv2'` after `usb-bot/fs` had run, the obvious reading was that the
# previous row's child still HELD the drive and nothing ever deleted it.  It
# did not: QEMU's own message is the discriminator, because a backend that
# exists but is taken says `Drive '...' is already in use`, and `can't find
# value` means the id does not resolve AT ALL.  A `-drive if=none` backend is
# auto-deleted when its guest device is unplugged, so it was gone the moment
# either the child or the adapter went.  The scratch disks are `-blockdev`
# nodes now (see the launch), which survive an unplug, so a row's replug leg
# and a later row can both use them.
#
# This is kept on its own smaller merit: a child added on the adapter's bus is
# torn down explicitly and in order, rather than implicitly by the adapter's
# removal.  Quiet and unconditional - a row that never added a child must not
# report a failure to remove one.
function Remove-RowChild {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Dut
    )
    if (-not $Row.ContainsKey('Child') -or $Row.Child -eq "") { return }
    Send-Mon -Port $Port -Command ("device_del {0}_lun" -f $Dut) -Quiet | Out-Null
    Start-Sleep -Milliseconds 500
}

# OCCUPYING A PORT IS NOT BEING ATTACHED, AND `info usb` DOES NOT TELL THEM
# APART.  This is what the `usb-bot`/`usb-uas` `devices addressed +0` was, and it
# was read as a possible defect in this driver for the whole of Phase 10 and into
# Phase 11.  Measured on 2b (batch 11-V stage E):
#
#   qom-get /machine/peripheral/bot1 attached   ->  false
#   qom-get /machine/peripheral/stor attached   ->  true
#
# Both models set `auto_attach = 0` at realize, because a SCSI host adapter is
# meant to be presented to the guest only once its LUN exists - so QEMU puts the
# device in the port and never electrically attaches it.  The controller agrees:
# the QEMU-side trace shows `usb_xhci_port_link port 2, pls 5` (RxDetect, i.e.
# nothing there) and no connect ever, so no `PORTSC` change reached the driver
# and there was nothing for it to refuse.  `qom-set ... attached true` after the
# child add enumerates both rows immediately - `devices addressed` 1 -> 3, twelve
# endpoint opens, zero refusals.
#
# THE MEASUREMENT IS `qom-get`, NOT THE SET.  A device that attached by itself
# must not be poked, and a model with no settable `attached` property must fail
# loudly rather than silently: read the state, repair it only when it is false,
# and read it back.  Returns an error string, or "" when the device is attached.
function Confirm-DeviceAttached {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Dut
    )
    $path = "/machine/peripheral/$Dut"
    $state = ((Get-MonitorText -Port $Port -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
    if ($state -match "(?i)\btrue\b") { return "" }
    if ($state -notmatch "(?i)\bfalse\b") {
        return ("could not read the attach state of id={0} - the monitor said '{1}'" -f $Dut, $state)
    }

    Write-Host ("  id={0} is in its port but NOT attached - attaching it" -f $Dut)
    Send-Checked -Port $Port -Command ("qom-set {0} attached true" -f $path) | Out-Null
    Start-Sleep -Milliseconds 500
    $state = ((Get-MonitorText -Port $Port -Command ("qom-get {0} attached" -f $path)) -join " ").Trim()
    if ($state -match "(?i)\btrue\b") { return "" }
    return ("id={0} occupies a root port but is not electrically attached, and a qom-set of attached=true did not change that (now '{1}'). No connect can reach the driver, so the row would have measured a bus with nothing on it." -f $Dut, $state)
}

# The ROW column is wider in the post-release report, where a row name can
# carry `/replug`; the Phase 10 report keeps its width so old reports still
# diff line for line.
$script:rowWidth = if ($PostRelease) { 28 } else { 22 }
$script:rowFormat = "{0,-6} {1,-" + $script:rowWidth + "} {2,-9} {3,-62} {4}"
function Add-Result {
    param([string]$TargetId, [string]$Row, [string]$Outcome, [string]$Expectation, [string]$Reading)
    $script:report += ($script:rowFormat -f $TargetId, $Row, $Outcome, $Expectation, $Reading)
}

# The post-release run's per-target tally: what the header's verdict line is
# computed from.  Reset per target; the row loop and the group-failure path
# both add to it.
$script:tgtTally = @{ Rows = 0; NoDriverExpected = 0; NotReached = 0; Against = 0 }

# THE SUMMARY COUNTS ROWS; $summary COUNTS REPORT LINES, AND THEY ARE NOT THE
# SAME NUMBER.  A row emits one report line per expectation, so a three-clause
# row that passed added 3 to "PASS" - and a row that failed split itself across
# two keys, "FAIL" for the clauses that held and "-> FAIL" for the one that did
# not, so neither number was the count of anything a reader wants.  The report
# body keeps the per-expectation lines (they are the evidence); the console
# tally is per row.
$rowSummary = @{}
function Add-RowOutcome {
    param([Parameter(Mandatory = $true)][string]$Outcome)
    if (-not $script:rowSummary.ContainsKey($Outcome)) { $script:rowSummary[$Outcome] = 0 }
    $script:rowSummary[$Outcome]++
}

# ONE ATTACH LEG: pump, baseline, attach, settle, churn, liveness, detach,
# wait for the bus to empty, read.  Returns Before/After readings, or an
# Error with After = $null.  The device is off the bus when this returns,
# whatever happened - the unconditional cleanup is here, not in the caller.
#
# It is a function because the post-release run takes the same leg twice per
# row (design record 09 section 5) and the two must be the same measurement.
function Invoke-AttachLeg {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string]$Dut,
        [Parameter(Mandatory = $true)]$Ident,
        [Parameter(Mandatory = $true)]$Table,
        $Process,
        [bool]$Pump = $true,
        [int]$DutPort = 2
    )
    $legError = ""
    $attached = $false

    # Wake the controller immediately before the attach.  Windows 98
    # idle-suspends about half a second after the last transfer and
    # an attach onto a halted controller is invisible to the whole
    # stack.  This is why no row asserts a controller-wide traffic
    # counter - see matrix.psd1's header.
    if ($Pump) { Invoke-Pump -Port $Port -Seconds 2 }

    # Backends before the baseline, so adding one is never inside a
    # row's measured window.
    Add-RowBackends -Port $Port -Row $Row

    $before = Read-Counters -Port $Port -BaseVa $Ident.Va -Table $Table -Process $Process

    # THE DEVICE UNDER TEST ALWAYS GOES ON THE SAME ROOT PORT.
    #
    # Windows 98 keys a USB devnode by its BUS LOCATION, so a device
    # class taught to the image at one root port is not recognised at
    # another - it raises a fresh Add New Hardware Wizard, which is
    # modal, blocks the bind, and turns the row into a NODRIVER that
    # says nothing about this driver.  Measured on the prepared 2a
    # image, and the correlation is clean:
    #
    #   usb-hub      taught at port 2 -> presented at 2 -> PASS
    #   usb-kbd/hs   taught at port 2 -> presented at 2 -> PASS
    #   usb-storage  taught at port 3 -> presented at 2 -> NODRIVER
    #   usb-mouse/fs taught at port 4 -> presented at 2 -> NODRIVER
    #
    # Without a pin the port depends on what QEMU happened to have
    # free, so which rows pass depends on the order of a prep pass
    # weeks earlier.  Port 1 is the keep-alive's, so the DUT takes 2
    # and a prep pass teaches at 2 as well.
    $add = "device_add {0},id={1},bus=xhci.0,port={2}" -f $Row.Model, $Dut, $DutPort
    if ($Row.ContainsKey('AddArgs') -and $Row.AddArgs -ne "") {
        $add = "{0},{1}" -f $add, $Row.AddArgs
    }
    $errBefore = Get-MonitorErrors
    Send-Checked -Port $Port -Command $add | Out-Null
    $attached = ((Get-MonitorErrors) -eq $errBefore)
    if (-not $attached) {
        $legError = "device_add was refused; nothing was attached"
    } else {
        # AN ATTACH IS ONLY AN ATTACH IF THE DEVICE ARRIVED - the
        # inverse of this harness's own rule about device_del, and
        # it earns its place immediately: `usb-bot` and `usb-uas`
        # are SCSI HOST ADAPTERS, `device_add` accepts them without
        # complaint, and the row then failed `advance devices
        # addressed` as though this driver had ignored a device on
        # the bus.  Whether the device is on the bus at all is a
        # question the monitor can answer, and it is a different
        # diagnosis from every other reason that assertion fails.
        #
        # THIS CHECK IS NECESSARY AND IT IS NOT SUFFICIENT, which is
        # the whole of the `+0` those two rows kept reading.  `info
        # usb` lists a device that OCCUPIES a port whether or not it
        # is electrically attached, so it answered "yes, it arrived"
        # about a device the controller could not see.  The
        # attach-state confirmation below is the other half; neither
        # replaces the other.
        $onBus = $false
        for ($w = 0; $w -lt 20; $w++) {
            if ((Test-UsbDeviceListed -Port $Port -Id $Dut) -eq $true) { $onBus = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $onBus) {
            $legError = ("device_add returned no error but the device never appeared on the bus. That is the vehicle, not the driver - `info usb` does not list id={0} ten seconds later." -f $Dut)
        } elseif ($Row.ContainsKey('Child') -and $Row.Child -ne "") {
            # The SCSI child, attached AFTER its adapter is on the USB
            # bus - `{ID}` is the adapter's id, which is only known
            # here.  Without it the adapter has no LUN and the row
            # measures an empty enclosure.
            $childSpec = $Row.Child -replace '\{ID\}', $Dut
            $errC = Get-MonitorErrors
            Send-Checked -Port $Port -Command ("device_add {0},id={1}_lun" -f $childSpec, $Dut) | Out-Null
            if ((Get-MonitorErrors) -gt $errC) {
                $legError = "the adapter attached but its scsi-hd child did not; the row would have measured an adapter with no LUN"
            }
        }

        # ... and only now, with the LUN in place, is the device
        # ready to be presented to the guest.  Every row goes through
        # this: a model that attached itself reads `true` and is left
        # alone, so the cost is one query on the rows that do not
        # need it and a correct row on the two that do.
        if ($legError -eq "") {
            $legError = Confirm-DeviceAttached -Port $Port -Dut $Dut
        }
    }

    if ($legError -eq "") {
        Start-Sleep -Seconds $Row.Settle

        # MULTI-STEP ROWS: a churn sequence, not a single attach.
        #
        # Batch 7b-V's hub numbers - `TtPairsDisagreed` = 10 among
        # them - come from a SEQUENCE (child in, child out, child
        # back, child moved to another hub port, whole hub removed
        # with live children below it), and a row that only attaches
        # and detaches one device cannot reproduce any of it.  Task
        # 10.4 requires the harness to reproduce hand-measured
        # results, so the row model has to be able to express what
        # the hand run did.
        #
        # `{DUT}` is the row's own device id, which is only known
        # here.  Every step's failure is fatal to the row: a churn
        # sequence that half-happened measures something nobody
        # described.
        if ($Row.ContainsKey('Steps')) {
            foreach ($step in $Row.Steps) {
                $errS = Get-MonitorErrors
                if ($step.Do -eq 'add') {
                    # `{DUT}` is the row's device id and `{PORT}` the
                    # root port it was pinned to - the two things a
                    # step cannot know for itself.  `{PORT}` is what
                    # behind-hub steps need, because a child of a hub
                    # is a PORT PATH on the same bus (`port=2.2.1`)
                    # and not a bus of the hub's own.
                    $spec = $step.Spec -replace '\{DUT\}', $Dut -replace '\{PORT\}', $DutPort
                    Send-Checked -Port $Port -Command ("device_add " + $spec) | Out-Null
                } elseif ($step.Do -eq 'del') {
                    Send-Checked -Port $Port -Command ("device_del " + $step.Id) | Out-Null
                } else {
                    $legError = ("unknown step '{0}'" -f $step.Do); break
                }
                if ((Get-MonitorErrors) -gt $errS) {
                    $legError = ("churn step '{0} {1}' failed; the sequence is incomplete and the row measures nothing describable" -f `
                                 $step.Do, $(if ($step.Do -eq 'del') { $step.Id } else { $step.Spec }))
                    break
                }
                Start-Sleep -Seconds $(if ($step.ContainsKey('Wait')) { [int]$step.Wait } else { 10 })
            }
        }

        # NOT ALIVE IS THREE FINDINGS, NOT ONE.  This said "the guest
        # stopped executing while this device was attached" whatever
        # the probe had actually found - including the case where the
        # QEMU process had gone, where there is no guest to have
        # stopped.  Demonstrated by killing QEMU inside this window.
        $alive = Test-GuestAlive -Port $Port -Process $Process
        if (-not $alive.Alive) {
            $legError = ("the guest did not survive this device [{0}]: {1} ({2})" -f `
                         $alive.Verdict, $alive.Why, $alive.Detail)
        }
    }

    $after = $null
    if ($legError -eq "") {
        # Detach FIRST, then read.  The transfer identity is only
        # exactly true once the endpoint has been torn down: a
        # transfer in flight is neither completed nor cancelled, and
        # a submitted/completed gap read during traffic is not a leak.
        #
        # THE SCSI CHILD GOES BEFORE ITS ADAPTER, AND IT HAS TO GO AT
        # ALL.  A row with a `Child` adds a `scsi-hd` on the adapter's
        # bus, and nothing ever deleted it: deleting the USB adapter
        # left the child holding `matrixdrv2`, so the NEXT row that
        # wanted that drive died on `Property 'scsi-hd.drive' can't
        # find value 'matrixdrv2'` and was recorded as its own
        # failure.  Measured on the first whole-matrix 2a run
        # Phase 10's matrix: `usb-bot/fs` ran, and `usb-uas/fs` could never
        # have passed after it.  This is the per-row-id defect
        # ("one row's failure destroyed the five after it") recurring
        # for a DRIVE rather than an id - a shared resource needs its
        # own release, and an id scheme does not cover one.
        Remove-RowChild -Port $Port -Row $Row -Dut $Dut
        Send-Checked -Port $Port -Command ("device_del {0}" -f $Dut) | Out-Null
        # A pull is only a pull if the device left.  unplug-8v.ps1
        # printed a confident FIRED while `info usb` still listed the
        # device (batch 8-V.1).
        # ...and only a COMPLETE reply that omits the device says it left.
        # `info usb` on an empty bus prints nothing, and so does a monitor
        # that never finished answering; Test-UsbDeviceListed tells them
        # apart by the prompt and answers $null for the second.
        $gone = $false
        $unanswered = 0
        for ($w = 0; $w -lt 40; $w++) {
            $listed = Test-UsbDeviceListed -Port $Port -Id $Dut
            if ($listed -eq $false) { $gone = $true; break }
            if ($null -eq $listed) { $unanswered++ }
            Start-Sleep -Milliseconds 500
        }
        if (-not $gone) {
            $legError = if ($unanswered -gt 0) {
                ("device_del could not be confirmed - `info usb` gave no complete reply {0} time(s) in 20 s, so whether the device left is unknown and nothing about this row's teardown was measured" -f $unanswered)
            } else {
                "device_del did not take - the device was still on the bus 20 s later, so nothing about this row's teardown was measured"
            }
        } else {
            Start-Sleep -Seconds 3
            $after = Read-Counters -Port $Port -BaseVa $Ident.Va -Table $Table -Process $Process
        }
    }

    if ($null -eq $after) {
        # UNCONDITIONAL CLEANUP.  Whatever went wrong, this row's
        # device must not survive into the next one's window - as
        # much for the counters as for the id.  The child goes too,
        # and first: it holds a drive the next row may need, and this
        # is the path a failed row takes.
        Remove-RowChild -Port $Port -Row $Row -Dut $Dut
        if ($attached) {
            Send-Mon -Port $Port -Command ("device_del {0}" -f $Dut) -Quiet | Out-Null
        }
    }

    return [pscustomobject]@{
        Error    = $legError
        Before   = $before
        After    = $after
        Attached = $attached
    }
}

$targetVerdicts = @{}
foreach ($tgt in $targetsToRun) {
    $groupsToRun = $mx.Groups
    if ($Group.Count -gt 0) { $groupsToRun = $mx.Groups | Where-Object { $Group -contains $_.Name } }

    # Per target: where this target's report lines start, its tally, and its
    # clock.  The post-release report is one file per target with these in
    # its header; the Phase 10 report is unchanged by any of it.
    $tgtReportStart = $report.Count
    $script:tgtTally = @{ Rows = 0; NoDriverExpected = 0; NotReached = 0; Against = 0 }
    $tgtStarted = Get-Date
    $tgtClock = [Diagnostics.Stopwatch]::StartNew()

    foreach ($grp in $groupsToRun) {
        Write-Host ""
        Write-Host ("=== {0} / group {1} : {2}" -f $tgt.Id, $grp.Name, $grp.Description)
        Reset-MonitorErrors
        # The backend cache is valid for ONE QEMU process, and each group boots a
        # fresh one - so it is reset here, beside Reset-MonitorErrors, and not at
        # script scope.  Initialised once, the whole-matrix invocation documented
        # in the README (no -Target, so 2a then 2b) skipped `netdev_add` and
        # `chardev-add` on the second target, `device_add usb-net,...` was then
        # refused, and `usb-net/fs`, `usb-serial/fs` and `usb-braille/fs` were
        # recorded ERROR - a harness artifact wearing a device row's name.  The
        # checkpoint runs did not see it because they were one invocation per
        # target.
        $script:backendsAdded = @{}

        # Before anything is launched or deleted: nothing may already be on this
        # monitor port.  If something is, this run would drive it instead of the
        # guest it is about to start.
        if (-not (Test-MonitorPortFree -Port $tgt.Monitor)) {
            throw ("something is already listening on monitor port {0} for target {1}. That is almost certainly a guest left over from an earlier run, and this run would have driven IT - attaching devices to it and reporting its counters. Stop it first." -f `
                $tgt.Monitor, $tgt.Id)
        }

        $tag = "{0}-{1}" -f $tgt.Id, $grp.Name
        $dbgLog = Join-Path $OutDir ("matrix-{0}-debugcon.log" -f $tag)
        $traceLog = Join-Path $OutDir ("matrix-{0}-trace.log" -f $tag)
        $traceEvents = Join-Path $OutDir ("matrix-{0}-events.txt" -f $tag)
        # A FRESH debug console log per group, deliberately.  These files append,
        # and one 9-V log holds two different MiniPortExtensionSize values -
        # i.e. two different binaries - which would make the offset freshness
        # check compare against a layout that stopped existing weeks ago.
        foreach ($f in @($dbgLog, $traceLog)) {
            if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
        }
        Set-Content -LiteralPath $traceEvents -Value $cfg.TraceEvents -Encoding ascii

        $scratchDrive = Join-Path $OutDir ("matrix-{0}-scratch.img" -f $tag)
        if (-not (Test-Path -LiteralPath $scratchDrive)) {
            $fs = [IO.File]::Create($scratchDrive); $fs.SetLength(64MB); $fs.Close()
        }
        $scratchDrive2 = Join-Path $OutDir ("matrix-{0}-scratch2.img" -f $tag)
        if (-not (Test-Path -LiteralPath $scratchDrive2)) {
            $fs = [IO.File]::Create($scratchDrive2); $fs.SetLength(64MB); $fs.Close()
        }
        # A THIRD SCRATCH DISK, from when the backends were `-drive if=none`
        # and QEMU deleted one with the device that held it: once `usb-bot/fs`
        # had run and been torn down the id was gone, and `usb-uas/fs` died on
        # `Property 'scsi-hd.drive' can't find value 'matrixdrv2'`, recorded as
        # that row's own refusal through two whole-matrix runs.  The backends
        # are `-blockdev` nodes now, which an unplug does not delete, so the
        # post-release run's second leg can reference the same node; one disk
        # per consumer is kept so the rows stay independent of each other.
        $scratchDrive3 = Join-Path $OutDir ("matrix-{0}-scratch3.img" -f $tag)
        if (-not (Test-Path -LiteralPath $scratchDrive3)) {
            $fs = [IO.File]::Create($scratchDrive3); $fs.SetLength(64MB); $fs.Close()
        }

        $args = @(
            "-name", ("xhci98 device matrix - {0} {1}" -f $tgt.Id, $grp.Name),
            "-machine", $tgt.Machine,
            "-cpu", $tgt.Cpu,
            "-m", "$($tgt.Memory)",
            "-drive", ("file={0},format={1},if=ide" -f (Join-Path $vmDir $tgt.Image), $tgt.Format),
            # See $snapshotArgs below - appended after the drives so it covers
            # them all.
            # p3=0: no SuperSpeed root ports.  USB 3.0 is out of scope and a
            # SuperSpeed port cannot carry any device in the population.
            "-device", "qemu-xhci,id=xhci,p3=0",
            # THE SCRATCH DISKS ARE -blockdev NODES, NOT -drive if=none.  QEMU
            # auto-deletes a `-drive if=none` backend when the device holding
            # it is unplugged, so the post-release run's replug leg (the same
            # `device_add ... drive=matrixdrv` after a `device_del`) answered
            # `Property 'usb-storage.drive' can't find value 'matrixdrv'` on
            # every storage row (repo audit S-1).  A node name declared with
            # -blockdev is accepted by `drive=` and survives the unplug;
            # verified guestless on QEMU 11.0.0 for usb-storage, usb-bot and
            # usb-uas with their scsi-hd children, twice each.
            "-blockdev", ("driver=raw,node-name=matrixdrv,file.driver=file,file.filename={0}" -f $scratchDrive),
            # A SECOND scratch disk, for the SCSI children of usb-bot / usb-uas.
            # Separate from matrixdrv because a node may be attached to exactly
            # one device at a time, and usb-storage holds that one.
            "-blockdev", ("driver=raw,node-name=matrixdrv2,file.driver=file,file.filename={0}" -f $scratchDrive2),
            "-blockdev", ("driver=raw,node-name=matrixdrv3,file.driver=file,file.filename={0}" -f $scratchDrive3),
            # THE NETDEV AND THE NULL CHARDEVS ARE NOT DECLARED HERE.  They are
            # added through the monitor, at the row that needs them - see
            # Add-RowBackends below.  Declaring `-netdev` on the command line
            # SUPPRESSES QEMU's DEFAULT NIC, and losing that PCI device changes
            # the guest's PCI layout enough that Windows 2000 stops starting the
            # already-installed driver: the boot ends at a "Found New Hardware
            # Wizard" with a zero-byte debug console and a trace that stops
            # three seconds in, which reads exactly like the driver failing to
            # load.  Bisected - `-netdev` alone reproduces it, the
            # scratch `-drive if=none` alone does not.
            "-chardev", ("file,id=dbgcon,path={0}" -f $dbgLog),
            "-device", "isa-debugcon,iobase=0xe9,chardev=dbgcon",
            # ONE -trace argument with both keys.  QEMU keeps the LAST -trace, so
            # `-trace events=X -trace file=Y` silently discards the event list
            # and the log reads as "the driver did nothing".
            "-trace", ("events={0},file={1}" -f $traceEvents, $traceLog),
            "-boot", "c",
            "-action", "reboot=reset", "-no-shutdown",
            "-monitor", ("tcp:127.0.0.1:{0},server=on,wait=off" -f $tgt.Monitor)
        )
        if ($tgt.Accel -ne "") { $args += @("-accel", $tgt.Accel) }
        # A TARGET WITHOUT AN `Smp` KEY GETS NO -smp ARGUMENT AT ALL, which is
        # what a uniprocessor guest needs - so this is additive and 2a and 2b
        # launch byte-for-byte as before.  It exists because the 2d SMP guest is
        # a configured target from batch 11-V stage F onward, and a config key
        # nothing honours is worse than an absent one: it reads as support.
        if ($tgt.ContainsKey('Smp') -and [int]$tgt.Smp -gt 1) { $args += @("-smp", "$([int]$tgt.Smp)") }

        # THE MATRIX DOES NOT WRITE TO THE GUEST IMAGE.  Every group boots the
        # same disk and a matrix exists to be re-run, so a run that mutates its
        # own starting state is not reproducible: the second run measures a
        # guest the first one changed.  It also bounds the damage from the rows
        # that are EXPECTED to be able to wedge a guest - batch 7b-V0 killed one
        # outright and batch 9-V wedged another's PnP tree - which would
        # otherwise be a corrupted image rather than a failed group.
        #
        # The cost is that anything done inside the guest is discarded, so
        # INSTALLING the driver is a separate, deliberate step done with
        # Snapshot = $false (or by hand).  That separation is wanted: an install
        # is not something a device matrix should be able to do by accident.
        $useSnapshot = $true
        if ($cfg.ContainsKey('Snapshot')) { $useSnapshot = [bool]$cfg.Snapshot }
        # The post-release run refuses a config that turns this off at
        # validation; this is the same rule at the launch, so no later edit to
        # the validation can boot a fresh image writable.
        if ($PostRelease) { $useSnapshot = $true }
        if ($useSnapshot) { $args += "-snapshot" }

        Assert-SingleTraceArg -QemuArgs $args

        $stderrFile = Join-Path $OutDir ("matrix-{0}-qemu-stderr.log" -f $tag)
        $proc = Start-Qemu -Qemu $qemuBin -QemuArgs $args -StderrFile $stderrFile
        $groupError = ""
        $rowInFlight = ""
        try {
            if (-not (Wait-Monitor -Port $tgt.Monitor -TimeoutSeconds 60)) {
                $err = Get-QemuStderr -StderrFile $stderrFile
                if ($err -eq "") { $err = "(QEMU wrote nothing to stderr)" }
                throw ("the guest's monitor never answered. QEMU said: {0}" -f $err)
            }

            # THE KEEP-ALIVE GOES ON BEFORE THE DRIVER STARTS, not after.
            # Windows 98 idle-suspends the controller about half a second after
            # the last transfer, and StartController happens about 8 seconds
            # into the boot - so a keep-alive attached once the driver is up
            # lands on a controller that has already suspended, is never
            # enumerated, and then cannot keep anything awake.  Measured: with
            # the attach moved before the driver, `devices addressed` and
            # `slots enabled` both reach 1 and the device is on the bus; with it
            # after, every counter stayed 0.
            #
            # A device on the QEMU COMMAND LINE is not the alternative - that
            # wedges SeaBIOS with a black screen and a zero-byte debug console
            # (batch 6-V paid a boot for it).  A monitor attach during the boot
            # is fine.
            if ($grp.Pump) {
                Send-Checked -Port $tgt.Monitor -Command "device_add usb-mouse,id=keepalive,bus=xhci.0" | Out-Null
            }

            # Wait for the DRIVER, not for a clock.  The qemu build writes
            # `cb StartController` the moment usbport starts it, and that same
            # line carries the extension VA every counter read needs.
            Write-Host ("waiting for the driver to start (deadline {0} s)..." -f $tgt.BootSeconds)
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $ident = $null
            while ($sw.Elapsed.TotalSeconds -lt $tgt.BootSeconds) {
                $ident = Find-ExtensionIdentity -DebugconLog $dbgLog
                if ($null -ne $ident.Va) { break }
                Start-Sleep -Seconds 3
            }
            if ($null -eq $ident -or $null -eq $ident.Va) {
                throw ("no `cb ... a=<VA>` line in {0} after {1} s. Either the guest did not boot, or the driver did not load, or it is not the QEMU build - since task 13-L.1 the port-0xE9 trace exists only in that flavour, so a `debug` guest produces this exact silence. Build it with: scripts\build-driver.cmd qemu, then scripts\package\make-package.ps1 -Flavor qemu, then prepare-image.ps1 -Xfer." -f $dbgLog, $tgt.BootSeconds)
            }
            Write-Host ("driver up: extension at 0x{0}, MiniPortExtensionSize={1}" -f $ident.Va, $ident.Size)

            if ($ident.Spans) {
                throw ("this group's debug console log already spans more than one driver load or binary (VAs: {0}; sizes: {1}). The log was deleted at group start, so this is a live restart and every reading in the group would straddle it." -f `
                    ($ident.AllVas -join ", "), ($ident.AllSizes -join ", "))
            }
            if ($null -ne $ident.Size) {
                Assert-OffsetsFresh -OffsetsFile $table.OffsetsFile -ExtensionSizeFromTrace $ident.Size | Out-Null
            } else {
                throw "the driver never printed MiniPortExtensionSize, so the offset table cannot be checked against it. A run on unchecked offsets is a run of wrong values."
            }

            # A healthy trace is not a living guest.  Batch 7b-V0 measured a
            # guest whose clock had stopped while its trace kept growing.
            $alive = Test-GuestAlive -Port $tgt.Monitor -Process $proc
            if (-not $alive.Alive) {
                throw ("the guest is not alive after boot [{0}]: {1} ({2})" -f $alive.Verdict, $alive.Why, $alive.Detail)
            }

            # "THE DRIVER STARTED" IS NOT "THE GUEST IS READY".  StartController
            # is reached about 8 seconds into the boot, long before the desktop
            # and long before the PnP stack will bind anything - so a row driven
            # then measures a machine that is still booting.
            #
            # The readiness signal is the guest's own: wait until a function
            # driver opens a non-default endpoint on the KEEP-ALIVE, which is
            # exactly "this OS's PnP stack is live and binding devices".  It is
            # the same counter the verdict uses for NODRIVER, on a device whose
            # class the target certainly has.
            #
            # On Windows 98 this deliberately TIMES OUT when the image has not
            # already been taught the device class: the Add New Hardware Wizard
            # is modal and blocks the bind indefinitely.  Measured here - ten
            # minutes with `endpoints opened` frozen at 0, the CPU perfectly
            # alive, and a screenshot showing the wizard asking about a "USB
            # Human Interface Device".  The message says so rather than leaving
            # the operator to infer a driver defect, which is what batch 9-V
            # recorded that dialog reads as.
            if ($grp.Pump) {
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
                if ($ready) {
                    Write-Host ("guest ready after {0} s" -f [int]$sw2.Elapsed.TotalSeconds)
                } else {
                    $shot = Save-GuestScreenshot -Port $tgt.Monitor `
                        -Path (Join-Path $OutDir ("matrix-{0}-notready.ppm" -f $tag))
                    throw ("no function driver opened an endpoint on the keep-alive within {0} s. The device WAS enumerated by this driver, so this is the guest's PnP stack, not the miniport - on Windows 98 it is almost certainly the modal Add New Hardware Wizard, which blocks the bind until someone clicks it. Install the device class into the image once (with Snapshot = `$false) and re-run. Screenshot: {1}" -f `
                        $readySeconds, $shot)
                }
            }

            $rowIndex = 0
            foreach ($row in $grp.Rows) {
                Write-Host ""
                Write-Host ("--- row {0}" -f $row.Name)
                # WHICH ROW WAS IN FLIGHT IS PART OF A GROUP-LEVEL FAILURE.
                # The catch below is outside this loop, and it used to report
                # `(group audio)` with no row in it: on the run that ended the
                # audio group, the report never said which device was on the bus
                # when everything stopped, and the row had to be inferred from
                # the fact that the group holds exactly one.
                $rowInFlight = $row.Name
                $key = "{0}|{1}" -f $row.Name, $tgt.Id
                $expectations = $parsed[$key]
                # A UNIQUE id per row, not a shared `dut`.  When the first row
                # errored before its device_del, every later row failed with
                # `Duplicate device ID 'dut'` and reported it as its own
                # refusal - six rows destroyed by one.  A per-row id means a
                # row's failure cannot be inherited by the next, and the
                # unconditional cleanup in Invoke-AttachLeg means it is not
                # left on the bus either.  Both, because either alone still
                # leaves one of the two failure modes.
                $rowIndex++
                $dut = "dut{0}" -f $rowIndex

                # A ROW EXCLUDED ON THIS TARGET IS REPORTED, NOT SKIPPED SILENTLY.
                # Deleting such a row from the matrix would remove the limitation
                # from every future report; printing it keeps it in the diff, for
                # the same reason NODRIVER and INERT are outcomes rather than
                # absences.  The reason is mandatory and is printed verbatim.
                $exclKey = $null
                if ($row.ContainsKey('ExcludedOnTarget')) { $exclKey = Find-TargetKey -Table $row.ExcludedOnTarget -Target $tgt }
                if ($null -ne $exclKey) {
                    $why = $row.ExcludedOnTarget[$exclKey]
                    Add-Result -TargetId $tgt.Id -Row $row.Name -Outcome "EXCLUDED" `
                               -Expectation "(not run on this target)" -Reading $why
                    Add-RowOutcome -Outcome "EXCLUDED"
                    $script:tgtTally.Rows++
                    $script:tgtTally.NotReached++
                    Write-Host ("  EXCLUDED - {0}" -f $why)
                    continue
                }

                # ONE LEG, OR TWO.  The ordinary matrix attaches once.  The
                # post-release run attaches, detaches, and attaches AGAIN with
                # the same device on the same root port, because a device that
                # comes back is a different claim from a device that enumerated
                # once (design record 09 section 5), and each leg is read and
                # judged on its own against the same expectations.
                # The loop itself, and the rule that a leg without a reading ends
                # the row, are Invoke-RowLegs in lib\fresh.ps1, which the
                # self-test drives with stand-ins; the three blocks below are
                # what this runner does on each leg.
                $legRun = Invoke-RowLegs -RowName $row.Name -LegCount $(if ($PostRelease) { 2 } else { 1 }) -RunLeg {
                    param($Leg, $LegName)
                    if ($Leg -eq 2) {
                        Write-Host ("--- row {0} (replug)" -f $row.Name)
                        # Let the OS finish tearing the node down before the
                        # same device reappears at the same location.  Rapid
                        # cycling is a known Windows 98 hazard the acceptance
                        # test warns against, and it is not what this leg
                        # measures.
                        Start-Sleep -Seconds 5
                    }
                    return (Invoke-AttachLeg -Port $tgt.Monitor -Row $row -Dut $dut -Ident $ident -Table $table `
                                             -Process $proc -Pump ([bool]$grp.Pump) -DutPort $DutPort)
                } -OnLegError {
                    param($Leg, $LegName, $LegResult)
                    $shot = Save-GuestScreenshot -Port $tgt.Monitor `
                        -Path (Join-Path $OutDir ("matrix-{0}-{1}.ppm" -f $tag, ($LegName -replace '[\\/:*?"<>|]', '_')))
                    if ($null -ne $shot) { Write-Host ("  screenshot: {0}" -f $shot) }
                    Add-Result -TargetId $tgt.Id -Row $LegName -Outcome "ERROR" -Expectation "(row did not complete)" -Reading $LegResult.Error
                    Write-Host ("  ERROR: {0}" -f $LegResult.Error)
                } -JudgeLeg {
                    param($Leg, $LegName, $LegResult)
                    $delta = Get-CounterDelta -Before $LegResult.Before -After $LegResult.After
                    $results = @()
                    foreach ($e in $expectations) {
                        $results += [pscustomobject]@{ Expectation = $e; Test = (Test-Expectation -Expectation $e -Delta $delta) }
                    }
                    $outcome = Get-RowOutcome -Results $results -Delta $delta -HarnessError "" -Table $table

                    foreach ($r in $results) {
                        # Per-expectation lines carry the ROW's outcome so the report
                        # is readable a line at a time; the per-expectation verdict is
                        # in the reading.
                        $mark = if ($r.Test.Held) { $outcome.Outcome } else { "-> " + $outcome.Outcome }
                        Add-Result -TargetId $tgt.Id -Row $LegName -Outcome $mark `
                                   -Expectation $r.Expectation.Text -Reading $r.Test.Reading
                    }
                    Write-Host ("  {0}{1}" -f $outcome.Outcome, $(if ($outcome.Why -ne "") { " - " + $outcome.Why } else { "" }))

                    # A picture for anything that is not a clean pass.  The two
                    # commonest causes on these targets - a modal Add New Hardware
                    # Wizard on Windows 98, and a wedged PnP tree - are invisible to
                    # every counter and to the liveness probe alike, and one
                    # screenshot separates them from a driver defect immediately.
                    if ($outcome.Outcome -ne "PASS") {
                        $shot = Save-GuestScreenshot -Port $tgt.Monitor `
                            -Path (Join-Path $OutDir ("matrix-{0}-{1}.ppm" -f $tag, ($LegName -replace '[\\/:*?"<>|]', '_')))
                        if ($null -ne $shot) { Write-Host ("  screenshot: {0}" -f $shot) }
                    }
                    return $outcome.Outcome
                }

                $rowOutcome = $legRun.Outcome
                if ($legRun.Why -ne "") {
                    Add-Result -TargetId $tgt.Id -Row $row.Name -Outcome $rowOutcome -Expectation "(both legs)" -Reading $legRun.Why
                    Write-Host ("  row: {0} - {1}" -f $rowOutcome, $legRun.Why)
                }
                Add-RowOutcome -Outcome $rowOutcome
                $script:tgtTally.Rows++

                # THE FRESH-INSTALL NODRIVER RULE (design record 09 section 4.2).
                # A row that reaches NODRIVER with an ExpectNoDriver entry for
                # this target does not count against the target's verdict; one
                # without the entry does.  Both are printed, so the diff between
                # two releases shows every one of them, and an entry that did
                # not apply is printed too, because the entries are guesses the
                # first run exists to correct.
                if ($PostRelease) {
                    $noDriverWhy = Get-RowNoDriverReason -Row $row -Target $tgt
                    if ($rowOutcome -eq "NODRIVER") {
                        if ($null -ne $noDriverWhy) {
                            Add-Result -TargetId $tgt.Id -Row $row.Name -Outcome "NODRIVER" -Expectation "(no class driver expected on this target)" -Reading $noDriverWhy
                            $script:tgtTally.NoDriverExpected++
                        } else {
                            Add-Result -TargetId $tgt.Id -Row $row.Name -Outcome "NODRIVER" -Expectation "(NO ExpectNoDriver entry for this target)" -Reading "a class this matrix said the OS would claim was not claimed; counts against the verdict"
                        }
                    } elseif ($null -ne $noDriverWhy) {
                        Add-Result -TargetId $tgt.Id -Row $row.Name -Outcome $rowOutcome -Expectation "(ExpectNoDriver entry did not apply)" -Reading ("the row reached {0}; correct the entry: {1}" -f $rowOutcome, $noDriverWhy)
                    }
                    # A row the matrix declares may wedge this target is the
                    # composite row's pinned reading (design record 09 section
                    # 4.1) whether the wedge ended the group or only this row's
                    # leg, so the declaration is read here as well as in the
                    # group-failure path below (repo audit S-3).
                    $rowWedgeDeclared = $false
                    if ($row.ContainsKey('MayWedgeGuest')) {
                        $rowWedgeDeclared = Test-TargetInList -List $row.MayWedgeGuest -Target $tgt
                    }
                    if (Test-RowCountsAgainst -Outcome $rowOutcome -NoDriverExpected ($null -ne $noDriverWhy) -WedgeDeclared $rowWedgeDeclared) {
                        $script:tgtTally.Against++
                    }
                }
            }
        } catch {
            $groupError = $_.Exception.Message

            # WHAT WAS LEFT BEHIND IS PART OF THE FINDING, AND IT USED TO BE
            # THROWN AWAY.  This path recorded the exception text and nothing
            # else: not the row that was in flight, not whether the guest was
            # still there, and - alone among every non-PASS path in this file -
            # no screenshot.  On the 2a `audio` group that cost the run its
            # actual cause.  The console had four `connection actively refused`
            # lines on it, QEMU runs with `-no-shutdown` so a bugchecked, wedged
            # or powered-off guest all leave the monitor answering, and the
            # process had therefore ENDED - a host-side failure.  With nothing
            # recorded, the result box that quoted this line attributed it to a
            # guest driver that had never bound, and it took a re-run to see it.
            #
            # So: name the row, ask the monitor whether anything is still there,
            # ask the process handle what became of it, and take the picture if
            # there is anything left to photograph.
            $listening = -not (Test-MonitorPortFree -Port $tgt.Monitor)
            $state = if ($listening) {
                "the monitor still answers, so the guest is there to be looked at"
            } else {
                Get-ProcessStateText -Process $proc
            }
            # The thrower may have established this already - the counter reader
            # does - and a reading that states it twice reads as two findings
            # rather than one said twice.
            if ($groupError.Contains($state)) { $state = "" }

            # A row the matrix DECLARED could do this is a different finding from
            # an ordinary row that killed a guest - opposite ones, in fact - and
            # until this was written the report could not tell them apart.
            $rowObj = $null
            if ($rowInFlight -ne "") {
                $rowObj = $grp.Rows | Where-Object { $_.Name -eq $rowInFlight } | Select-Object -First 1
            }
            $wedgeDeclared = $false
            if ($rowInFlight -ne "" -and $null -ne $rowObj -and $rowObj.ContainsKey('MayWedgeGuest')) {
                $wedgeDeclared = Test-TargetInList -List $rowObj.MayWedgeGuest -Target $tgt
            }
            $declared = if ($rowInFlight -eq "") {
                "no row was in flight, so this is a group-level failure rather than a device's"
            } elseif ($wedgeDeclared) {
                "the matrix declares this row may wedge this target"
            } else {
                "NOTHING in this matrix declares this row may wedge this target"
            }
            # In the post-release run a declared wedge is the composite row's
            # pinned reading on Windows 98 (design record 09 section 4.1) and
            # does not count against the target; an undeclared one does.  The
            # outcome word stays ERROR either way, and the line is in the
            # report, so a run that does NOT reproduce the wedge changes the
            # diff.
            $script:tgtTally.Rows++
            if ($PostRelease -and (Test-RowCountsAgainst -Outcome "ERROR" -WedgeDeclared $wedgeDeclared)) {
                $script:tgtTally.Against++
            }

            $label = if ($rowInFlight -ne "") {
                "(group {0}, row {1})" -f $grp.Name, $rowInFlight
            } else {
                "(group {0})" -f $grp.Name
            }
            $reading = if ($state -eq "") {
                "{0} | {1}" -f $groupError, $declared
            } else {
                "{0} | {1} | {2}" -f $groupError, $state, $declared
            }

            Write-Host ("*** group {0} ended early: {1}" -f $grp.Name, $reading)
            if ($listening) {
                $shot = Save-GuestScreenshot -Port $tgt.Monitor `
                    -Path (Join-Path $OutDir ("matrix-{0}-groupfail.ppm" -f $tag))
                if ($null -ne $shot) { Write-Host ("  screenshot: {0}" -f $shot) }
            }
            Add-Result -TargetId $tgt.Id -Row $label -Outcome "ERROR" `
                       -Expectation "(group did not complete)" -Reading $reading
            Add-RowOutcome -Outcome "ERROR"
        } finally {
            if ($groupError -ne "" -and $KeepGuestOnFailure) {
                Write-Host ("the guest is being LEFT RUNNING on monitor {0} for inspection." -f $tgt.Monitor)
            } else {
                Send-Mon -Port $tgt.Monitor -Command "quit" -Quiet | Out-Null
                Start-Sleep -Seconds 2
                if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
            }
        }
    }

    # THE POST-RELEASE REPORT, ONE PER TARGET, WITH ITS HEADER (design record
    # 09 section 9).  The body is this target's slice of the same lines the
    # Phase 10 report carries; the header holds everything variable, and the
    # verdict is computed from the tally rather than read off the lines.
    if ($PostRelease) {
        $tgtClock.Stop()
        $verdict = Get-TargetVerdict -Tally $script:tgtTally
        $targetVerdicts[$tgt.Id] = $verdict

        # The driver line names the qemu package this tree staged, which is what
        # the preparation carried into the guest.  Absent, it says so; the stamp
        # and the identity line are the witnesses that a driver is installed,
        # and this line is only what it was built from.
        $pkgSys = Join-Path $repo "out\pkg-qemu\xhci98.sys"
        $driverLine = if (Test-Path -LiteralPath $pkgSys) {
            $item = Get-Item -LiteralPath $pkgSys
            ("{0} qemu, {1} B, sha256 {2}" -f $version, $item.Length, (Get-FileHash -LiteralPath $pkgSys -Algorithm SHA256).Hash.Substring(0, 16).ToLowerInvariant())
        } else {
            ("{0} qemu, out\pkg-qemu\xhci98.sys not present on this host" -f $version)
        }
        $imageLine = ("{0}, stamp {1}, from {2} {3}" -f (Join-Path $cfg.VmDir $tgt.Image), $imageStampByTarget[$tgt.Id], $tgt.CloneFrom.Image, $tgt.CloneFrom.Snapshot)
        $accel = if ($tgt.Accel -ne "") { $tgt.Accel } else { "tcg" }
        $hdr = New-PostReleaseHeader -TargetId $tgt.Id -Version $version -DriverLine $driverLine -ImageLine $imageLine `
                   -QemuVersion $qemuVer -Accel $accel -Sizeof $table.Sizeof -Counters $table.Offsets.Count `
                   -Started $tgtStarted -Elapsed $tgtClock.Elapsed -Verdict $verdict -Rows $script:tgtTally.Rows `
                   -NoDriverExpected $script:tgtTally.NoDriverExpected -NotReached $script:tgtTally.NotReached
        $body = @()
        if ($report.Count -gt $tgtReportStart) { $body = @($report[$tgtReportStart..($report.Count - 1)]) }
        $tgtReport = Write-PostReleaseReport -Path (Join-Path $OutDir ("post-release-{0}.txt" -f $tgt.Id)) -Header $hdr `
                         -ColumnLine ($script:rowFormat -f "TARGET", "ROW", "OUTCOME", "EXPECTATION", "READING") -Body $body
        Write-Host ""
        Write-Host ("=== {0}: {1} ({2} rows, {3} NODRIVER expected, {4} not reached, {5} against; {6})" -f `
            $tgt.Id, $verdict, $script:tgtTally.Rows, $script:tgtTally.NoDriverExpected, $script:tgtTally.NotReached, $script:tgtTally.Against, $tgtClock.Elapsed.ToString("h\:mm\:ss"))
        Write-Host ("report: {0}" -f $tgtReport)
    }
}

# ------------------------------------------------------------------ report ---
if ($PostRelease) {
    # The per-target files above are the record.  A run that evaluated no
    # rows on some target has already been given FAIL for it; here the exit
    # code is the worst verdict, so a scheduler needs no report to read.
    Write-Host ""
    Write-Host "summary (rows):"
    foreach ($k in ($rowSummary.Keys | Sort-Object)) { Write-Host ("  {0,-10} {1}" -f $k, $rowSummary[$k]) }
    Write-Host ""
    foreach ($k in ($targetVerdicts.Keys | Sort-Object)) { Write-Host ("verdict {0}: {1}" -f $k, $targetVerdicts[$k]) }
    if ($report.Count -eq 0) {
        Write-Host "*** this run evaluated NO rows at all. That is a failure, not an empty pass."
        exit 2
    }
    if (@($targetVerdicts.Values | Where-Object { $_ -ne "PASS" }).Count -gt 0) { exit 1 }
    exit 0
}

# Diffable: the body has no timestamps, durations or paths, so a regression is a
# CHANGED LINE rather than a re-read.  Everything variable is in the header,
# which a diff can be told to skip.
$header = @()
$header += "# xhci98 Phase 10 - automated VM device matrix"
$header += ("# qemu   : {0}" -f $qemuVer)
$header += ("# host   : {0}" -f $env:COMPUTERNAME)
$header += ("# offsets: SIZEOF {0}, {1} counters" -f $table.Sizeof, $table.Offsets.Count)
$header += ("# matrix : {0} rows" -f $rowCount)
$header += "#"
$header += "# Outcomes: PASS FAIL NODRIVER INERT ERROR - see docs/contributing/design/06-device-matrix-verdict.md"
$header += "# A '-> X' in the outcome column marks the expectation that did not hold."
$header += "#"
$header += ("{0,-6} {1,-22} {2,-9} {3,-62} {4}" -f "TARGET", "ROW", "OUTCOME", "EXPECTATION", "READING")

$reportPath = Join-Path $OutDir $ReportName
Set-Content -LiteralPath $reportPath -Value ($header + $report) -Encoding utf8

# The same rule one level up: a run that evaluated nothing did not pass.
if ($report.Count -eq 0) {
    Write-Host ""
    Write-Host "*** this run evaluated NO rows at all. That is a failure, not an empty pass."
    exit 2
}

Write-Host ""
Write-Host "summary (rows):"
foreach ($k in ($rowSummary.Keys | Sort-Object)) { Write-Host ("  {0,-10} {1}" -f $k, $rowSummary[$k]) }
Write-Host ""
Write-Host ("report: {0}" -f $reportPath)

# A run with any ERROR or FAIL row exits nonzero, so this is usable from a
# scheduler without reading the report.  Read off the row tally, whose keys are
# the outcomes themselves - the report-line tally splits one failing row across
# 'FAIL' and '-> FAIL', which happened to sum to the right verdict here and is
# not a property to depend on.
$bad = 0
foreach ($k in $rowSummary.Keys) { if ($k -match 'FAIL|ERROR') { $bad += $rowSummary[$k] } }
if ($bad -gt 0) { exit 1 }
exit 0

