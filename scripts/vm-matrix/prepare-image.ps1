<#
.SYNOPSIS
One-time, PERSISTED preparation of a target VM image so the device matrix can
run unattended afterwards.

.DESCRIPTION
The matrix harness boots with `-snapshot` and never writes to a guest image.
That is deliberate - a matrix exists to be re-run, and a run that mutates its
own starting state is not reproducible - but it means the harness can never
install anything, and both targets need something installed once:

  * WINDOWS 98 raises a MODAL "Add New Hardware Wizard" for every device class
    its image has not been taught, and it blocks the bind indefinitely.
    Measured: a boot-attached usb-mouse reached `devices addressed`
    = 1 and `slots enabled` = 1, and `endpoints opened` stayed 0 for 776
    seconds with the CPU perfectly alive.  It also silently disables the
    keep-alive pump, because `mouse_move` only reaches the wire once a function
    driver holds the pointer's interrupt endpoint - so the NEXT hot-plug is
    invisible too.  Until each class is installed, 2a cannot produce one
    meaningful matrix row.
  * WINDOWS 2000 binds HID and mass storage itself, but the image carries a
    leftover "Video Controller (VGA Compatible)" devnode that raises its own
    Found New Hardware Wizard on every boot.  Windows 2000 serialises driver
    installation, so an open wizard is the leading suspect for the NODRIVER
    rows the first 2b run produced.

THIS SCRIPT WRITES TO THE IMAGE.  It takes an internal qcow2 snapshot first,
following this repository's existing `pre-<what>-safety-<date>` convention, so
the whole pass is revertible with one command.

The division of labour is the standing one: the OPERATOR drives the guest GUI,
this script keeps the monitor, the counters and the screenshots.

THE POST-RELEASE RUN'S PREPARATION (Phase 16, task 16.1) is this script too,
against a FRESH target - one whose config entry carries `CloneFrom` and
`Like`.  Two modes exist for it and nothing else: -Clone copies the named
pre-driver snapshot out of a Phase 10 image into a new file, and -Stamp takes
the `base-<DriverVer>-qemu` snapshot the run checks before it boots anything.
Between them the pass is the same as above: -Boot -Xfer (which on a fresh
target stages the WHOLE qemu package, INF and usbd.sys included, because the
guest has never had this driver), install by hand from the transfer drive,
-Attach each class, shut down.  docs\contributing\design\09-post-release-unattended-run.md
section 8 is the procedure.

.EXAMPLE
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a -Boot
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a -Attach kbd-hs
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a -Status
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a -Shot
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Clone
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Boot -Xfer
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Boot -Xfer -WorkDir C:\work
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-sweetlow -Boot -Xfer -XferAdd vm\SWEETLOW
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -CopyBack
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Stamp
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Config = "",
    [switch]$Boot,
    # Fresh targets only: clone the config's CloneFrom snapshot into the
    # target's image, and stamp the prepared image.  See the block below.
    [switch]$Clone,
    [switch]$Stamp,
    [string]$Attach = "",
    [string]$Detach = "",
    [switch]$Status,
    [switch]$Shot,
    [switch]$Shutdown,
    [switch]$NoSafetySnapshot,
    # Run the pass against a copy on a local disk (-Boot -WorkDir), then copy
    # it back with -CopyBack when the guest has exited cleanly.  Use this when
    # the image lives in a synced tree - see the warning in the -Boot path.
    # -CopyBack also records that the vm-dir file now holds what the last
    # boot witnessed, which is what -Stamp checks.
    [string]$WorkDir = "",
    [switch]$CopyBack,
    [switch]$NoKeepAlive,
    [switch]$FreshCopy,
    # Attach a VVFAT transfer drive carrying the qemu xhci98.sys. OFF by
    # default: see the comment on $xferDir - it is the prime suspect for the
    # main-loop hangs, and a prep pass does not otherwise need it.
    [switch]$Xfer,
    # A directory whose contents are copied onto the transfer drive AFTER the
    # package is staged, as a subdirectory named after it. For material the
    # guest needs beside the driver and that the package must never carry -
    # the first use is SweetLow's USB 2.0 stack under test (tools\sweetlow-
    # extracted, issue #1), which a fresh 2a guest installs by right-clicking
    # its USB2.INF. Third-party files stay out of out\pkg-qemu this way.
    [string]$XferAdd = "",
    # Pin the device to a specific ROOT port - see the comment at the attach.
    [int]$AtPort = 0,
    # Comma-separated device names to attach AT BOOT, before the driver starts.
    # This is how a prep pass should present devices - see the comment at the
    # preload loop. Up to 7 alongside the keep-alive (p2=8).
    [string]$Preload = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\monitor.ps1")
. (Join-Path $PSScriptRoot "lib\qemu.ps1")
. (Join-Path $PSScriptRoot "lib\counters.ps1")
. (Join-Path $PSScriptRoot "lib\fresh.ps1")

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
function Resolve-RepoPath { param([string]$P)
    if ([string]::IsNullOrWhiteSpace($P)) { return "" }
    if ([IO.Path]::IsPathRooted($P)) { return $P }
    return (Join-Path $repo $P)
}
# The same two places run-matrix.ps1 looks, so one config serves both scripts.
if ($Config -eq "") {
    foreach ($c in @("scripts\vm-matrix\matrix.config.psd1")) {
        if (Test-Path (Join-Path $repo $c)) { $Config = Join-Path $repo $c; break }
    }
}
if ($Config -eq "" -or -not (Test-Path -LiteralPath (Resolve-RepoPath $Config))) {
    throw ("no configuration found. Copy scripts\vm-matrix\config.sample.psd1, edit the paths, and pass it with -Config.")
}
$cfg = Import-PowerShellDataFile -LiteralPath (Resolve-RepoPath $Config)
$tgt = $cfg.Targets | Where-Object { $_.Id -eq $Target }
if ($null -eq $tgt) { throw ("no target '{0}' in {1}" -f $Target, $Config) }

# WHICH OPERATING SYSTEM, AND WHETHER THIS IS A FRESH TARGET.  The Windows 98
# branches below (the CD, the drivers directory, the wizard advice) used to key
# on `-Target 2a` literally.  A fresh target is the same OS under another id
# and inherits them through `Like`; the SMP guest is refused here as it always
# was, because a prep pass is a uniprocessor affair and 2d has never had one.
$isFresh = Test-FreshTarget -Target $tgt
$family  = if ($tgt.ContainsKey('Like') -and $tgt.Like) { [string]$tgt.Like } else { [string]$tgt.Id }
if ($family -notin @('2a', '2b')) {
    throw ("target '{0}' is neither 2a nor 2b nor a fresh target that names one of them in `Like`; this script prepares those and nothing else" -f $Target)
}
$isWin98 = ($family -eq '2a')
if (($Clone -or $Stamp) -and -not $isFresh) {
    throw ("-Clone and -Stamp are for a fresh target (one with `CloneFrom` in the config); '{0}' is not one, and Phase 10's images are never cloned over or stamped" -f $Target)
}

$qemuBin = Resolve-QemuBinary -Hint $cfg.Qemu
$qemuImg = Join-Path (Split-Path -Parent $qemuBin) "qemu-img.exe"
$vmDir   = Resolve-RepoPath $cfg.VmDir
$image   = Join-Path $vmDir $tgt.Image
$outDir  = Resolve-RepoPath $cfg.OutDir
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

# The prep instance gets its OWN monitor port, so it can never be confused with
# a matrix run's guest and the two can coexist.
$port = [int]$tgt.Monitor + 100
$dbg  = Join-Path $outDir ("prep-{0}-debugcon.log" -f $Target)

# WHERE THE RUNNING GUEST'S DEBUG CONSOLE IS, REMEMBERED ACROSS INVOCATIONS.
#
# -Boot may redirect the log onto a local disk (-WorkDir), but -Status and -Shot
# are separate invocations that would recompute the default path and read a
# DIFFERENT, STALE log - and take the extension address out of it.  That is
# exactly what happened: -Status reported every counter as 0 from
# a previous boot's address (`C14668C4`) while the live guest was at `C14658C4`
# with 162 transfers on the wire, and the operator was told, twice, that their
# driver install had not worked when it had.
#
# A stale ADDRESS reads as a plausible set of zeros, never as an error - the
# same failure shape the offset-freshness check exists to refuse. So the boot
# writes down where it put things and every later mode reads that.
$pathsFile = Join-Path $outDir ("prep-{0}-paths.txt" -f $Target)
if (-not $Boot -and (Test-Path -LiteralPath $pathsFile)) {
    $remembered = (Get-Content -LiteralPath $pathsFile | Select-Object -First 1).Trim()
    if ($remembered -ne "" -and (Test-Path -LiteralPath $remembered)) { $dbg = $remembered }
}

# EVERY DEVICE INSTANCE THE MATRIX PRESENTS, because that is the unit Windows
# 98 actually asks about.
#
# A first version of this list had one entry per CLASS, on the theory that
# installing "USB Human Interface Device" once covers every HID model.  The 2a
# run refuted it: with the HID class installed, `usb-kbd/hs` bound silently on a
# fresh port and PASSED, while `usb-kbd/fs` - the SAME MODEL at Full Speed -
# raised an Insert Disk dialog and ended with `RH ports disabled` = 1.  So the
# speed variants do NOT share an install, and the list that has to be taught is
# the matrix's own row set rather than a shorter list of classes.
#
# Kept in step with matrix.psd1 by hand, and kept ordered so that walking it
# with repeated -Attach calls visits the rows in a defined sequence.
$specs = [ordered]@{
    'kbd-hs'   = 'usb-kbd,id=prep_kbd_hs,bus=xhci.0,usb_version=2'
    'kbd-fs'   = 'usb-kbd,id=prep_kbd_fs,bus=xhci.0,usb_version=1'
    'mouse-hs' = 'usb-mouse,id=prep_mouse_hs,bus=xhci.0,usb_version=2'
    'mouse-fs' = 'usb-mouse,id=prep_mouse_fs,bus=xhci.0,usb_version=1'
    'tablet'   = 'usb-tablet,id=prep_tablet,bus=xhci.0,usb_version=2'
    'wacom'    = 'usb-wacom-tablet,id=prep_wacom,bus=xhci.0'
    'storage'  = 'usb-storage,id=prep_storage,bus=xhci.0,drive=prepdrv,removable=on'
    'hub'      = 'usb-hub,id=prep_hub,bus=xhci.0'
    'bot'      = 'usb-bot,id=prep_bot,bus=xhci.0'
    'uas'      = 'usb-uas,id=prep_uas,bus=xhci.0'
    'net'      = 'usb-net,id=prep_net,bus=xhci.0,netdev=prepnet'
    'serial'   = 'usb-serial,id=prep_serial,bus=xhci.0,chardev=prepchr1'
    'braille'  = 'usb-braille,id=prep_braille,bus=xhci.0,chardev=prepchr2'
    'ccid'     = 'usb-ccid,id=prep_ccid,bus=xhci.0'
    'u2f'      = 'u2f-emulated,id=prep_u2f,bus=xhci.0'
    'audio'    = 'usb-audio,id=prep_audio,bus=xhci.0,audiodev=prepaud'
}
# `mouse` kept as an alias so the earlier invocations in this session still work.
$specs['mouse'] = $specs['mouse-hs']

# THE SCSI ADAPTERS ARE TAUGHT WITH THEIR LUN, OR THEY ARE NOT TAUGHT AT ALL.
# `usb-bot` and `usb-uas` realise with `auto_attach = 0`: the adapter sits in
# its port and is never electrically attached until a `scsi-hd` child exists,
# so a bare `-Attach uas` showed Windows nothing, raised no wizard, and the
# operator read the silence as "already taught".  The first post-release run
# (2026-08-30) then met the Add New Hardware Wizard on `usb-uas/fs`, on a
# guest nobody was allowed to touch.  The child and the `attached` repair
# below are the matrix row's own (`Child` in matrix.psd1, Confirm-DeviceAttached
# in run-matrix.ps1), so the prep pass presents what the run will present.
$children = @{
    'bot' = 'scsi-hd,id=prep_bot_lun,bus=prep_bot.0,drive=prepdrv2'
    'uas' = 'scsi-hd,id=prep_uas_lun,bus=prep_uas.0,drive=prepdrv2,scsi-id=0,lun=0'
}

# The QEMU id one of these rows will create, so `-Detach` can be given the same
# name `-Attach` was (repo audit D4).  Deriving the id from the SPEC rather than
# from the key is the whole point: `mouse` is an alias for `mouse-hs`, so the
# old `"prep_" + ($Detach -replace '-','_')` composed `prep_mouse` and asked
# QEMU to delete a device id that does not exist - which fails at the monitor
# with an error the caller then has to interpret.
function Get-AttachId {
    param([Parameter(Mandatory = $true)][string]$Name)
    if (-not $specs.Contains($Name)) { return "" }
    if ($specs[$Name] -match 'id=([A-Za-z0-9_]+)') { return $Matches[1] }
    return ""
}

# ------------------------------------------------------------ -Clone ---
#
# The fresh image is a COPY of a pre-driver snapshot, and the source is never
# written.  `qemu-img convert -l snapshot.name=<tag>` reads one internal
# snapshot out of a qcow2 and writes it as a new standalone image; the source
# is opened read-only, so a Phase 10 image that is open in a running QEMU makes
# qemu-img refuse (a shared-lock error) rather than read a moving target.
# Reverting the source in place is exactly what this must not do: every later
# snapshot in vm\win98.img is a child of the state a revert would discard.
if ($Clone) {
    $src = Join-Path $vmDir $tgt.CloneFrom.Image
    $tag = [string]$tgt.CloneFrom.Snapshot
    # The refusals are Get-CloneProblems in lib\fresh.ps1, which the self-test
    # drives; this reads the facts and hands them over.
    $srcSnaps = @()
    if (Test-Path -LiteralPath $src) { $srcSnaps = @(Get-ImageSnapshots -QemuImg $qemuImg -Image $src) }
    $cloneProblems = @(Get-CloneProblems -Source $src -Tag $tag -SourceExists ([bool](Test-Path -LiteralPath $src)) -SourceSnapshots $srcSnaps `
                           -Destination $image -DestinationExists ([bool](Test-Path -LiteralPath $image)) -FreshCopy ([bool]$FreshCopy))
    if ($cloneProblems.Count -gt 0) { throw ($cloneProblems -join " ") }
    Write-Host ("cloning {0} @ {1} -> {2} ..." -f $src, $tag, $image)
    $text = Invoke-NativeText -Exe $qemuImg -Arguments @("convert", "-O", "qcow2", "-l", ("snapshot.name={0}" -f $tag), $src, $image)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $image)) {
        throw ("qemu-img convert failed ({0}): {1}" -f $LASTEXITCODE, $text.Trim())
    }
    Write-Host ("  {0:N0} MB written. It carries no snapshots yet; the stamp is the LAST thing the preparation does." -f ((Get-Item -LiteralPath $image).Length / 1MB))
    Write-Host ("  next: -Boot -Xfer, install the driver from the transfer drive, -Attach each class, shut down cleanly, then -Stamp.")
    exit 0
}

# ------------------------------------------------------------ -Stamp ---
#
# The stamp ties the image to a release: an internal snapshot named
# `base-<DriverVer>-qemu`, taken after the driver install has been persisted
# and the guest has shut down.  It is refused while the prep guest is still up
# (the image would be mid-write), and refused when the last prep boot's debug
# console never showed the qemu build running with the offset table's SIZEOF -
# a stamp says what was installed, and this is the only witness this side of
# a boot that anything was.
# -CopyBack: copy the -WorkDir work copy over the vm-dir image once the guest
# has shut down cleanly, and record that the vm-dir file now holds what the
# last boot witnessed, so -Stamp accepts it. A hand copy leaves the paths file
# naming the work copy and -Stamp keeps refusing; this is the route that
# updates both. The refusals are Get-CopyBackProblems in lib\fresh.ps1.
if ($CopyBack) {
    $bootedImage = ""
    $pathLines = @()
    if (Test-Path -LiteralPath $pathsFile) {
        $pathLines = @(Get-Content -LiteralPath $pathsFile)
        if ($pathLines.Count -ge 2) { $bootedImage = [string]$pathLines[1] }
    }
    $bootedExists = (-not [string]::IsNullOrWhiteSpace($bootedImage)) -and (Test-Path -LiteralPath $bootedImage.Trim())
    $copyProblems = @(Get-CopyBackProblems -Port $port -PortFree ([bool](Test-MonitorPortFree -Port $port)) -Image $image `
                          -BootedImage $bootedImage -BootedExists ([bool]$bootedExists))
    if ($copyProblems.Count -gt 0) { throw ($copyProblems -join " ") }
    $bootedImage = $bootedImage.Trim()
    Write-Host ("copying {0} -> {1} ..." -f $bootedImage, $image)
    Invoke-CopyBackRecord -Source $bootedImage -Destination $image -PathsFile $pathsFile
    Write-Host ("{0} now holds the state the last prep boot witnessed, and the record names it. Run -Stamp next; it checks the install witness (MiniPortExtensionSize) before it stamps." -f $image)
    exit 0
}

if ($Stamp) {
    # The image the last -Boot ran is the second line of the paths file: with
    # -WorkDir that is the work copy, and a stamp on the vm-dir file before the
    # copy back would name an install that file does not hold.  The refusals
    # are Get-StampProblems in lib\fresh.ps1, which the self-test drives.
    $bootedImage = ""
    if (Test-Path -LiteralPath $pathsFile) {
        $pathLines = @(Get-Content -LiteralPath $pathsFile)
        if ($pathLines.Count -ge 2) { $bootedImage = [string]$pathLines[1] }
    }
    $ident = Find-ExtensionIdentity -DebugconLog $dbg
    $table = Import-CounterTable
    $stampProblems = @(Get-StampProblems -Port $port -PortFree ([bool](Test-MonitorPortFree -Port $port)) -Image $image `
                           -ImageExists ([bool](Test-Path -LiteralPath $image)) -BootedImage $bootedImage `
                           -DebugconLog $dbg -IdentSize $ident.Size -TableSizeof $table.Sizeof)
    if ($stampProblems.Count -gt 0) { throw ($stampProblems -join " ") }
    $version = Get-DriverVersionUnderTest -RepoRoot $repo
    $name = Get-BaseStampName -Version $version -Flavour "qemu"
    $existing = @(Get-ImageSnapshots -QemuImg $qemuImg -Image $image)
    # A second stamp of the same name (the operator taught one more class and
    # stamped again) replaces the first: two snapshots sharing a tag make
    # `snapshot -a` ambiguous, and the run wants exactly one newest.
    foreach ($s in $existing) {
        if ($s.Tag -eq $name) {
            Write-Host ("replacing the earlier '{0}' (id {1})" -f $name, $s.Id)
            # Through Invoke-NativeText: qemu-img's stderr under Stop would
            # otherwise abort the script before the message below is reached.
            $text = Invoke-NativeText -Exe $qemuImg -Arguments @("snapshot", "-d", $name, $image)
            if ($LASTEXITCODE -ne 0) { throw ("qemu-img snapshot -d failed ({0}): {1}" -f $LASTEXITCODE, $text.Trim()) }
        }
    }
    $text = Invoke-NativeText -Exe $qemuImg -Arguments @("snapshot", "-c", $name, $image)
    if ($LASTEXITCODE -ne 0) { throw ("qemu-img snapshot -c failed ({0}) - is the image open in another QEMU? {1}" -f $LASTEXITCODE, $text.Trim()) }
    $after = @(Get-ImageSnapshots -QemuImg $qemuImg -Image $image)
    $problems = @(Get-FreshImageProblems -ImagePath $image -Snapshots $after -Version $version -Flavour "qemu")
    if ($problems.Count -gt 0) {
        throw ("the stamp was written but the image still does not pass the run's own check: {0}" -f ($problems -join "; "))
    }
    Write-Host ("stamped {0} as '{1}' (newest of {2} snapshot(s)). The post-release run will accept it." -f $image, $name, $after.Count)
    Write-Host ("anything persisted to this image after now fails the run's newest-snapshot check; re-stamp after any further preparation.")
    exit 0
}

if ($Boot) {
    if (-not (Test-MonitorPortFree -Port $port)) {
        throw ("something is already listening on {0} - a prep guest is probably still running. Use -Status, or stop it." -f $port)
    }
    if ($isFresh -and -not (Test-Path -LiteralPath $image)) {
        throw ("fresh image not found: {0}. Run -Clone first." -f $image)
    }
    if (Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue) {
        Write-Host "*** NOTE: a qemu-system-x86_64 process is already running. If it has this image open, the snapshot below will fail."
    }

    # A PREP PASS IS THE ONLY THING HERE THAT WRITES TO THE IMAGE, AND THE IMAGE
    # LIVES IN A ONEDRIVE-SYNCED TREE.
    #
    # Every matrix run boots with `-snapshot`, so its writes go to a temporary
    # overlay under %TEMP% and the .img is only ever READ.  This script is the
    # exception, and the first non-snapshot boot of the session
    # hung QEMU solid: CPU time frozen at 55.05 s across a 5 s sample,
    # `Responding = False`, main loop blocked, so the monitor and the display
    # died together and the guest looked frozen from inside.  The image itself
    # was fine afterwards (`qemu-img check`: 3 leaked clusters, **0
    # corruptions**), which is what a blocked write looks like rather than a
    # crash.
    #
    # A sync client rehydrating or locking a 300 MB qcow2 underneath a running
    # QEMU is the obvious candidate and the correlation is exact - it is the one
    # boot that wrote to the synced file.  It is a HYPOTHESIS, not a proven
    # cause: nothing here observed OneDrive taking the lock.  So the mitigation
    # is stated as a warning and an option rather than done silently, and
    # -WorkDir is the way out that needs no change to anyone's sync settings.
    $syncedTree = ($image -match '(?i)\\OneDrive\\')
    if ($syncedTree -and $WorkDir -eq "") {
        Write-Host ""
        Write-Host "*** WARNING: this image is inside a OneDrive-synced tree, and this is the"
        Write-Host "    one boot that WRITES to it. A sync client touching the file under a"
        Write-Host "    running QEMU blocks its main loop and the guest hangs with the monitor"
        Write-Host "    dead - measured. Consider -WorkDir <local-path> to run the"
        Write-Host "    pass on a local disk and copy the result back, or pause syncing first."
        Write-Host ""
    }

    # No safety snapshot on a fresh image: the clone IS the safety (re-run
    # -Clone -FreshCopy to start over), and any snapshot taken here would sit
    # under the stamp for the run to read.
    if ($isFresh -and -not $NoSafetySnapshot) {
        Write-Host "fresh target: no safety snapshot is taken; re-clone with -Clone -FreshCopy to start over."
        $NoSafetySnapshot = $true
    }
    if (-not $NoSafetySnapshot) {
        $tag = "pre-phase10-prep-{0}" -f (Get-Date -Format "yyyy-MM-dd")
        Write-Host ("taking a safety snapshot '{0}' of {1}" -f $tag, $image)
        $text = Invoke-NativeText -Exe $qemuImg -Arguments @("snapshot", "-c", $tag, $image)
        if ($LASTEXITCODE -ne 0) { throw ("qemu-img snapshot failed ({0}) - is the image open in another QEMU? {1}" -f $LASTEXITCODE, $text.Trim()) }
        Write-Host ("  revert with:  `"{0}`" snapshot -a {1} `"{2}`"" -f $qemuImg, $tag, $image)
    }

    if ($WorkDir -ne "") {
        if (-not (Test-Path $WorkDir)) { New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null }
        $local = Join-Path $WorkDir (Split-Path -Leaf $image)

        # AN EXISTING WORK COPY IS NEVER SILENTLY OVERWRITTEN.
        #
        # A prep pass is not one boot.  Windows 98 needs a RESTART before a
        # driver install takes effect, so the natural sequence is: boot, install,
        # shut down, boot again, verify - and a second `-Boot` that re-copied
        # from vm\ would overwrite the half-finished work with the stale
        # original and quietly undo everything the operator had just done.
        # Reuse is the default; clobbering needs -FreshCopy and says so.
        if ((Test-Path -LiteralPath $local) -and (-not $FreshCopy)) {
            Write-Host ("REUSING the existing work copy: {0}" -f $local)
            Write-Host ("  ({0:N0} MB, last written {1})" -f ((Get-Item $local).Length / 1MB), (Get-Item $local).LastWriteTime)
            Write-Host ("  pass -FreshCopy to replace it from {0} instead." -f $image)
        } else {
            Write-Host ("copying {0} -> {1} ..." -f $image, $local)
            Copy-Item -LiteralPath $image -Destination $local -Force
        }
        Write-Host ("working on the LOCAL copy. When the guest has shut down cleanly, copy it back:")
        Write-Host ("    powershell -File scripts\vm-matrix\prepare-image.ps1 -Target {0} -Config `"{1}`" -CopyBack" -f $Target, (Resolve-RepoPath $Config))
        Write-Host ("  (copy /y `"{0}`" `"{1}`" plus the record that -Stamp checks)" -f $local, $image)
        Write-Host ("Do NOT copy back a guest that was killed rather than shut down.")
        $image = $local
    }

    # EVERY FILE QEMU WRITES GOES LOCAL WHEN -WorkDir IS SET, not just the image.
    #
    # A first version moved only the image and the guest hung again in exactly
    # the same way (CPU frozen, main loop blocked, monitor and display dead
    # together) - which refuted "OneDrive is blocking the image write" as
    # stated, because the image was on a local disk that time.  What was still
    # synced was everything ELSE QEMU touches: the debug console log it appends
    # to on every trace line, the scratch disk, and the VVFAT transfer
    # directory.  Moving one file and calling the hypothesis tested was the
    # mistake; a variable is only controlled if you control all of it.
    if ($WorkDir -ne "") {
        $outDir = $WorkDir
        $dbg = Join-Path $WorkDir ("prep-{0}-debugcon.log" -f $Target)
    }

    $scratch = Join-Path $outDir ("prep-{0}-scratch.img" -f $Target)
    if (-not (Test-Path $scratch)) { $fs = [IO.File]::Create($scratch); $fs.SetLength(64MB); $fs.Close() }
    # A second scratch for the SCSI children ($children): a drive node can back
    # exactly one device at a time, and `storage` may still be on the first.
    $scratch2 = Join-Path $outDir ("prep-{0}-scratch2.img" -f $Target)
    if (-not (Test-Path $scratch2)) { $fs = [IO.File]::Create($scratch2); $fs.SetLength(64MB); $fs.Close() }
    if (Test-Path -LiteralPath $dbg) { Remove-Item -LiteralPath $dbg -Force }
    # Written where the DEFAULT OutDir is, so a later -Status finds it without
    # having to be told -WorkDir again.  The second line is the image this
    # boot runs (the work copy under -WorkDir), which -Stamp checks against
    # the file it is asked to stamp.
    Set-Content -LiteralPath $pathsFile -Value @($dbg, $image) -Encoding ascii

    # A TRANSFER DRIVE CARRYING THE QEMU DRIVER UNDER ITS REAL NAME.
    #
    # **It was the DEBUG build until task 13-L.1** split a third
    # flavour out.  The matrix identifies the running driver from the
    # `cb ... a=<VA>` line on the port-0xE9 debug console, and that trace now
    # compiles only under `qemu` - `src\sources` gates XHCI_DBG_LIVE/XHCI_DBG_E9
    # on BUILD_ALT_DIR == chk_qemu.  A `debug` image boots and binds and then
    # produces no identity line at all, which run-matrix.ps1 reports as a
    # timeout rather than as a wrong flavour, so this staging is where it has to
    # be right.
    #
    # The image must be running the QEMU build, and the SAME one the offset
    # table was generated from, or every counter the matrix reads is off by
    # however much the layout moved - a wrong VALUE, never an error.  Since this
    # is the one pass that may write to the image, it is also the natural place
    # to update the driver, so the build is staged here as `XHCI98.SYS` at the
    # root of a directory of its own: a one-line copy in the guest, with no
    # renaming and nothing else on the drive to pick the wrong file from.
    #
    # A dedicated directory rather than vm\xfer98, which carries 24 MB of test
    # media from earlier batches.  `snapshot=on` so a guest cannot write back
    # into the repository.
    # ...AND THE TRANSFER DRIVE IS OFF BY DEFAULT, because it is the prime
    # suspect for those hangs and nothing in a normal prep pass needs it.
    # VVFAT presents a HOST DIRECTORY as a FAT volume and reads it live, so a
    # sync client rehydrating or locking a file in that directory blocks the
    # read inside QEMU's main loop.  Both hangs happened on a boot that had one
    # attached; no matrix run, which never attaches one, has ever hung.  That is
    # correlation across four runs, not proof - hence a switch rather than a
    # deletion, and -Xfer for when the driver really does need replacing.
    $xferDir = $null
    $staged = $null
    # (assigned inside the -Xfer branch below; left $null when nothing is staged
    # so the banner further down cannot report a file that is not there.)
    if ($Xfer) {
        $xferDir = if ($WorkDir -ne "") { Join-Path $WorkDir "xfer" } else { Join-Path $vmDir "xfer-p10" }
        if (-not (Test-Path $xferDir)) { New-Item -ItemType Directory -Path $xferDir -Force | Out-Null }
        $qemuSys = Join-Path $repo "out\pkg-qemu\xhci98.sys"
        $staged = Join-Path $xferDir "XHCI98.SYS"
        # A FRESH GUEST GETS THE WHOLE PACKAGE, NOT A LOOSE .SYS.  A Phase 10
        # image already has the driver installed and only needs the binary
        # replaced; a fresh one has never seen it, so the install goes through
        # the INF - which is the path a user's machine takes - and the INF
        # delivers the per-target usbd.sys the base image was chosen not to
        # carry.  The package directory is copied as make-package.ps1 laid it
        # out, and the guest is pointed at the directory, never at a file.
        if ($isFresh) {
            $pkgDir = Join-Path $repo "out\pkg-qemu"
            if (-not (Test-Path -LiteralPath (Join-Path $pkgDir "xhci98.inf"))) {
                throw ("no qemu package at {0} (xhci98.inf missing). Build it with: scripts\build-driver.cmd qemu, then scripts\package\make-package.ps1 -Flavor qemu." -f $pkgDir)
            }
            Get-ChildItem -LiteralPath $xferDir -Force | Remove-Item -Recurse -Force
            Copy-Item -Path (Join-Path $pkgDir "*") -Destination $xferDir -Recurse -Force
            $staged = Join-Path $xferDir "xhci98.sys"
            Write-Host ("transfer drive carries the whole qemu package from {0}:" -f $pkgDir)
            foreach ($f in (Get-ChildItem -LiteralPath $xferDir -File)) { Write-Host ("  {0,-16} {1,9:N0} B" -f $f.Name, $f.Length) }
        } elseif (Test-Path $qemuSys) {
            Copy-Item -LiteralPath $qemuSys -Destination $staged -Force
        } else {
            # **The stale file is DELETED, not left behind.** This directory
            # persists between runs, so a missing package used to mean the guest
            # was handed whatever the last run staged - which after the 13-L.1
            # split may be the debug binary that produces no identity line, and
            # the operator would have been told the drive carried nothing
            # while it carried the wrong driver under the right name. Removing
            # it makes the guest's copy step fail loudly instead.
            if (Test-Path $staged) { Remove-Item -LiteralPath $staged -Force }
            $staged = $null
            Write-Warning ("no qemu build at {0} - build it with: scripts\build-driver.cmd qemu, then scripts\package\make-package.ps1 -Flavor qemu. Any XHCI98.SYS left on the transfer drive by an earlier run has been removed, so the drive carries no driver." -f $qemuSys)
        }
        # The extra directory rides in a subdirectory so it can never shadow a
        # package file, and the 8.3 name is what the guest will see it as.
        if ($XferAdd -ne "") {
            $addSrc = Resolve-RepoPath $XferAdd
            if (-not (Test-Path -LiteralPath $addSrc)) { throw ("-XferAdd directory not found: {0}" -f $addSrc) }
            $addName = (Split-Path -Leaf $addSrc)
            if ($addName -notmatch '^[A-Za-z0-9_]{1,8}$') { throw ("-XferAdd directory name '{0}' is not a plain 8.3 name; Windows 98 reads the transfer drive as FAT" -f $addName) }
            $addDst = Join-Path $xferDir $addName
            if (Test-Path -LiteralPath $addDst) { Remove-Item -LiteralPath $addDst -Recurse -Force }
            New-Item -ItemType Directory -Path $addDst -Force | Out-Null
            Copy-Item -Path (Join-Path $addSrc "*") -Destination $addDst -Recurse -Force
            Write-Host ("transfer drive also carries {0}\ from {1}:" -f $addName, $addSrc)
            foreach ($f in (Get-ChildItem -LiteralPath $addDst -File)) { Write-Host ("  {0,-16} {1,9:N0} B" -f $f.Name, $f.Length) }
        }
    }
    if ($XferAdd -ne "" -and -not $Xfer) { throw "-XferAdd needs -Xfer; there is no transfer drive to add to otherwise" }

    $args = @(
        "-name", ("xhci98 image prep - " + $Target),
        "-machine", $tgt.Machine, "-cpu", $tgt.Cpu, "-m", "$($tgt.Memory)",
        "-drive", ("file={0},format={1},if=ide" -f $image, $tgt.Format),
        # p2=8: enough ROOT ports for a prep sweep.  A default qemu-xhci has
        # four, and once they are full QEMU CASCADES further devices onto an
        # auto-inserted hub - so the fifth device gets taught to Windows at a
        # BEHIND-HUB location while the matrix will present it on a root port,
        # and the install does not apply where it is needed.  Measured on the
        # first sweep: `Port 4.1` for the fifth attach.  p3=0 keeps every port
        # USB 2.0, since a Full Speed device cannot attach to a SuperSpeed one.
        "-device", "qemu-xhci,id=xhci,p2=8,p3=0",
        "-drive", ("if=none,id=prepdrv,file={0},format=raw" -f $scratch),
        "-drive", ("if=none,id=prepdrv2,file={0},format=raw" -f $scratch2),
        # The chardevs are declared up front - they create no PCI device, so
        # unlike `-netdev` they cannot disturb the guest's hardware layout.
        # FILE, NOT NULL: a `null` chardev is never open, and `usb-serial` and
        # `usb-braille` attach themselves only when their chardev is - so with
        # `null` they sat in their port with `attached=false`, the guest saw
        # nothing, and the pass taught nothing (measured 2026-08-30 with
        # qom-get: null -> false, file -> true, for both models).
        "-chardev", ("file,id=prepchr1,path={0}" -f (Join-Path $outDir ("prep-{0}-chr1.log" -f $Target))),
        "-chardev", ("file,id=prepchr2,path={0}" -f (Join-Path $outDir ("prep-{0}-chr2.log" -f $Target))),
        # And an audiodev, for the same reason and with the same property:
        # `usb-audio` REFUSES TO ATTACH without one, so the `audio` row of $specs
        # could never have worked (repo audit D4).  `none` is a real audiodev
        # backend that plays nothing and, like the chardevs, creates no PCI
        # device - which is the constraint that rules out `-netdev` here.
        "-audiodev", "none,id=prepaud",
        "-chardev", ("file,id=dbgcon,path={0}" -f $dbg),
        "-device", "isa-debugcon,iobase=0xe9,chardev=dbgcon",
        "-boot", "c", "-action", "reboot=reset", "-no-shutdown",
        "-monitor", ("tcp:127.0.0.1:{0},server=on,wait=off" -f $port)
    )
    # Windows 98's wizard asks for its installation media for anything not
    # already in C:\WINDOWS\OPTIONS\CABS.  Batch 9-V paid a boot for this: with
    # no media the wizard stops the boot with device initialisation blocked
    # behind the dialog, which reads exactly like a driver that failed to load.
    # ...IF THERE IS ANY.  `tools\w98se.img` is NOT the Windows 98 SE CD - it is
    # a 1.44 MB BOOT FLOPPY, and a first version of this script attached it as
    # `-cdrom` on the strength of its name.  The wizard then searched a 1.4 MB
    # floppy for `mouse.drv` and of course did not find it, which looks like
    # missing media rather than like the harness handing it the wrong disk.
    # Size is the discriminator, and an absent CD is reported rather than faked.
    # The CD comes from the config's `Win98Cd`, and the SIZE is checked.
    #
    # `tools\w98se.img` is NOT the Windows 98 SE CD - it is a 1.44 MB BOOT
    # FLOPPY, and its own `w98se.url.example` says so - but a first version of
    # this script attached it as `-cdrom` on the strength of its name.  The
    # wizard then searched a 1.4 MB floppy for `mouse.drv`, which looks like
    # missing media rather than like the harness handing it the wrong disk.
    # Measured, mid-pass, with an operator waiting on the dialog.
    #
    # Windows 98 DOES need the CD here: `C:\WINDOWS\OPTIONS\CABS` on this image
    # does not carry every file the HID install asks for.
    $cd = ""
    if ($isWin98) {
        $candidates = @()
        if ($cfg.ContainsKey('Win98Cd') -and $cfg.Win98Cd -ne '') { $candidates += (Resolve-RepoPath $cfg.Win98Cd) }
        # The in-repo path only.  `D:\isos\w98se.iso` used to sit here as a
        # second fallback, which is one host's layout committed as a default:
        # on that host it silently supplied a CD the config never named, and
        # everywhere else it was a path that could only miss.  Where the media
        # lives is what `Win98Cd` is for; the message below says so.
        $candidates += @((Join-Path $repo "tools\w98se.iso"))
        foreach ($c in $candidates) {
            if ((Test-Path -LiteralPath $c) -and ((Get-Item -LiteralPath $c).Length -gt 100MB)) { $cd = $c; break }
        }
        if ($cd -ne "") {
            $args += @("-cdrom", $cd)
            Write-Host ("Windows 98 SE CD attached: {0}  (its CABs are in \WIN98)" -f $cd)
        } else {
            Write-Host "NOTE: no Windows 98 SE CD image found. Set Win98Cd in the config."
            Write-Host "      Without it a driver-install wizard can stall on a missing file:"
            Write-Host "      C:\WINDOWS\OPTIONS\CABS on this image does not carry all of them."
            Write-Host "      You can also insert one into a RUNNING guest from the monitor:"
            Write-Host "          change ide1-cd0 <path-to-iso>"
        }
    }
    # THE TRANSFER DRIVE ITSELF.  The staging above filled the directory, and
    # until this argument existed nothing attached it, so the banner below
    # described a drive the guest could not see (repo audit S-2).  VVFAT
    # presents the directory as a FAT volume; `index=1` is the primary slave,
    # which collides with neither the image (index 0) nor `-cdrom` (index 2,
    # `ide1-cd0`); `snapshot=on` keeps the guest from writing back into the
    # repository.  Verified guestless on QEMU 11: `info block` lists it as
    # `ide0-hd1` beside `ide0-hd0` and `ide1-cd0`.
    if ($null -ne $xferDir) {
        $args += @("-drive", ("file=fat:{0},format=raw,if=ide,index=1,snapshot=on" -f $xferDir))
    }
    # NOTE: no -snapshot.  That is the entire point of this script.
    Assert-SingleTraceArg -QemuArgs $args

    $proc = Start-Qemu -Qemu $qemuBin -QemuArgs $args -StderrFile (Join-Path $outDir ("prep-{0}-qemu-stderr.log" -f $Target))
    Write-Host ""
    Write-Host ("*** THIS GUEST WRITES TO {0}. Shut it down from inside Windows when done. ***" -f $image)
    Write-Host ("monitor : {0}" -f $port)
    Write-Host ("debugcon: {0}" -f $dbg)
    Write-Host ("pid     : {0}" -f $proc.Id)
    if ($null -ne $staged -and $isFresh) {
        Write-Host ""
        Write-Host "transfer drive carries the qemu PACKAGE. In the guest, install it the way"
        Write-Host "readme.txt section 4 says for this target - point the wizard at the"
        Write-Host "transfer drive's root DIRECTORY, never at a loose file:"
        if ($isWin98) {
            Write-Host "  Device Manager -> the unclaimed xHCI controller -> Properties -> Driver ->"
            Write-Host "  Update Driver -> Specify a location -> <xfer>:\   (the Windows 98 CD is attached)"
            Write-Host "  then RESTART the guest when asked, and confirm with -Status after the restart."
        } else {
            Write-Host "  Device Manager -> the controller -> Properties -> Driver -> Update Driver ->"
            Write-Host "  Have Disk -> <xfer>:\   then confirm with -Status."
        }
    } elseif ($null -ne $staged) {
        $drivers = if ($isWin98) { "C:\WINDOWS\SYSTEM32\DRIVERS" } else { "C:\WINNT\SYSTEM32\DRIVERS" }
        Write-Host ""
        Write-Host ("transfer drive carries the QEMU build as XHCI98.SYS ({0:N0} bytes)" -f (Get-Item $staged).Length)
        Write-Host ("  in the guest, if you want to update the driver:")
        Write-Host ("      copy <xfer>:\XHCI98.SYS {0}" -f $drivers)
        Write-Host ("  a loaded .sys CAN be overwritten in place on both targets; the new one")
        Write-Host ("  takes effect on the next boot.")
    }
    if (-not (Wait-Monitor -Port $port -TimeoutSeconds 90)) {
        throw ("the prep guest's monitor never answered. QEMU said: {0}" -f (Get-QemuStderr -StderrFile (Join-Path $outDir ("prep-{0}-qemu-stderr.log" -f $Target))))
    }

    # THE POINTER GOES ON NOW, BEFORE THE DRIVER STARTS.
    #
    # StartController is reached about 8 s into the boot and Windows 98
    # idle-suspends the controller about half a second after the last transfer,
    # so a device attached once the desktop is up lands on a halted controller
    # and is invisible until a Device Manager Refresh.  Attached here it is
    # picked up by the driver's own start-up port scan, the wizard appears on
    # its own, and the operator never has to know about any of that.
    # EVERYTHING THIS PASS WILL TEACH IS ATTACHED HERE, BEFORE THE DRIVER
    # STARTS, AND NOTHING IS HOT-PLUGGED AFTERWARDS.
    #
    # Four prep boots and every matrix run give one clean discriminator:
    #
    #   prep #1   no -snapshot, NO churn (one device attached at boot)  -> worked
    #   prep #2-4 no -snapshot, hot-plug churn                          -> HUNG
    #   matrix    -snapshot, hot-plug churn                             -> fine
    #
    # Every hang had the same signature - CPU time frozen across a multi-second
    # sample, `Responding = False`, monitor and display dead together, image
    # intact afterwards with 0 corruptions - i.e. a blocked main loop, not a
    # crash.  THREE explanations were offered in turn and all three are refuted:
    #
    #   OneDrive holding the image   - hangs 2 and 3 ran from a local disk
    #   the VVFAT transfer drive     - hang 3 had none
    #   hot-plug churn               - hang 4 preloaded everything and hot-plugged nothing
    #
    # WHAT ACTUALLY CORRELATES is the device: THREE OF THE FOUR HANGS HAPPENED
    # WHILE WINDOWS 98 WAS INSTALLING THE DRIVER FOR `usb-tablet`, and the
    # operator reported it in those words twice.  `usb-tablet` is an ABSOLUTE
    # pointing device, so binding it makes QEMU switch its active pointer and its
    # display backend to absolute mode - work that happens on the main loop.
    # That is a candidate mechanism and NOT a demonstrated one; nothing here has
    # isolated it, and it is written down so the next person starts from the
    # correlation rather than from my three wrong guesses.
    #
    # Practically: preload everything EXCEPT the tablets, which is what
    # -Preload's caller now does, and treat `usb-tablet` / `usb-wacom-tablet` on
    # 2a as an open vehicle question rather than a driver result.
    #
    # The preload itself is kept regardless of cause, because it is also simply
    # how Windows 98 wants to be taught: the devices are present when its PnP
    # stack starts, the wizards queue up on their own, and the operator works
    # through them once.
    $preloadList = @()
    if (-not $NoKeepAlive) { $preloadList += 'mouse-hs' }
    if ($Preload -ne "") {
        $preloadList += @($Preload -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
    }
    $slot = 0
    foreach ($name in $preloadList) {
        if (-not $specs.Contains($name)) {
            Write-Host ("  *** unknown device '{0}' - known: {1}" -f $name, (($specs.Keys) -join ", "))
            continue
        }
        $slot++
        if ($name -eq 'net') { Send-Checked -Port $port -Command "netdev_add user,id=prepnet" | Out-Null }
        # Pinned to a root port each: once QEMU has an auto-inserted hub it
        # prefers the hub's downstream ports over free root ports, and a device
        # taught behind a hub is not taught for the root port the matrix uses.
        Send-Checked -Port $port -Command ("device_add {0},port={1}" -f $specs[$name], $slot) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    if ($slot -gt 0 -and $isFresh) {
        Write-Host ("{0} device(s) attached BEFORE the driver starts. On a fresh guest with no driver installed yet" -f $slot)
        Write-Host "  they sit on an unclaimed controller until the install and restart; their wizards follow then."
    }
    if ($slot -gt 0) {
        Write-Host ("{0} device(s) attached BEFORE the driver starts; their wizards will appear on their own." -f $slot)
        Write-Host "  NOTHING will be hot-plugged into this guest - see the comment above for why."

        # AND THE POINTER GOES BACK TO PS/2, HERE TOO.
        #
        # -Attach already did this; the preload path did not, and the omission
        # cost an operator a "the display seems frozen" - QEMU had routed input
        # to the LAST USB pointer attached, which Windows has not bound yet
        # (that is what the wizards are for), so mouse and keys went nowhere
        # while the guest was provably healthy: IRQ delta 1055 over 1.5 s.
        # A guest that cannot receive input is indistinguishable from a hung one
        # from the outside, which is exactly why this must not be left to chance.
        $mice = Get-MonitorText -Port $port -Command "info mice"
        $ps2 = $null
        foreach ($l in $mice) { if ($l -match 'Mouse #(\d+):.*PS/2') { $ps2 = $Matches[1] } }
        if ($null -ne $ps2) {
            Send-Checked -Port $port -Command ("mouse_set " + $ps2) | Out-Null
            Write-Host ("  pointer pinned to the PS/2 mouse (#{0}); USB pointers cannot take it." -f $ps2)
        }
        if (($preloadList -join ',') -match 'kbd') {
            Write-Host "  NOTE: a USB keyboard is attached and will take key events until Windows"
            Write-Host "        binds it. Drive the wizards with the MOUSE."
        }
    }

    Write-Host "monitor is up; the guest is booting."
    Write-Host ""
    Write-Host "WHEN YOU HAVE FINISHED CLICKING THROUGH THE WIZARDS:"
    Write-Host "  1. check with:  -Status   (`endpoints opened` >= 1 is the proof)"
    Write-Host "  2. SHUT WINDOWS DOWN FROM THE START MENU. Do not kill the window."
    Write-Host "     Windows 98 writes SYSTEM.DAT lazily: a guest that is killed loses the"
    Write-Host "     driver database update and the whole pass with it. Measured."
    if ($WorkDir -ne "") {
        Write-Host ("  3. then copy the prepared image back, which also records it for -Stamp:")
        Write-Host ("       powershell -File scripts\vm-matrix\prepare-image.ps1 -Target {0} -Config `"{1}`" -CopyBack" -f $Target, (Resolve-RepoPath $Config))
    }
    exit 0
}

if ($Attach -ne "") {
    # `.Contains`, not `.ContainsKey`: $specs is an [ordered] hashtable, which is
    # an OrderedDictionary and has no ContainsKey at all.  Making the list
    # ordered - so a caller walking it visits the rows in a defined order -
    # silently changed its type and broke every lookup.
    if (-not $specs.Contains($Attach)) {
        throw ("unknown device '{0}'. Known: {1}" -f $Attach, (($specs.Keys | Sort-Object) -join ", "))
    }
    # `usb-net` needs a netdev, and a netdev may NOT be declared on the command
    # line: doing so suppresses QEMU's default NIC, and losing that PCI device
    # changes the guest's hardware layout enough that a target stops starting
    # the already-installed driver.  Added here through the monitor instead,
    # once, which creates no PCI device at all.
    if ($Attach -eq 'net') {
        $have = (Get-MonitorText -Port $port -Command "info network") -join " "
        if ($have -notmatch 'prepnet') {
            Send-Checked -Port $port -Command "netdev_add user,id=prepnet" | Out-Null
        }
    }
    # THE ROOT PORT IS PINNED WHEN ASKED FOR, because QEMU's own choice is not
    # what this pass needs.  Once an auto-inserted hub exists, QEMU places new
    # devices on the HUB's downstream ports in preference to root ports that
    # have just been freed - measured here as `Port 4.2`, `4.3`, `4.4` with root
    # ports 2 and 3 sitting empty.  Windows 98 keys a devnode by its location,
    # so a device taught behind a hub is not taught for the root port the matrix
    # will present it on, and the whole install is wasted.
    $spec = $specs[$Attach]
    if ($AtPort -gt 0) { $spec = "{0},port={1}" -f $spec, $AtPort }
    Send-Checked -Port $port -Command ("device_add " + $spec) | Out-Null
    Start-Sleep -Seconds 2
    if ($children.ContainsKey($Attach)) {
        # The LUN, then the electrical attach the adapter withholds until it
        # has one - read first, repaired only when false, read back, as the
        # run does.  A silent adapter teaches nothing.
        Send-Checked -Port $port -Command ("device_add " + $children[$Attach]) | Out-Null
        Start-Sleep -Milliseconds 500
        $qom = "/machine/peripheral/" + (Get-AttachId -Name $Attach)
        $state = ((Get-MonitorText -Port $port -Command ("qom-get {0} attached" -f $qom)) -join " ").Trim()
        if ($state -match "(?i)\bfalse\b") {
            Send-Checked -Port $port -Command ("qom-set {0} attached true" -f $qom) | Out-Null
            Start-Sleep -Milliseconds 500
            $state = ((Get-MonitorText -Port $port -Command ("qom-get {0} attached" -f $qom)) -join " ").Trim()
        }
        if ($state -notmatch "(?i)\btrue\b") {
            throw ("{0} is in its port with its LUN but not electrically attached (qom-get said '{1}'); the guest sees nothing and the pass would teach nothing." -f $Attach, $state)
        }
        Write-Host ("  {0} presented with its scsi-hd LUN and electrically attached." -f $Attach)
    }

    # ATTACHING A USB POINTER STEALS THE OPERATOR'S MOUSE, and a USB keyboard
    # steals the keys.  QEMU routes input to the most recently added handler, so
    # a guest that has not yet INSTALLED the device - which is the entire point
    # of this pass - receives nothing at all, and the operator is left unable to
    # drive the very wizard they were asked to drive.  Measured the hard way on
    # in Phase 10's matrix.  A matrix run never notices because nobody is typing at it.
    if ($Attach -match 'mouse|tablet|wacom') {
        $mice = Get-MonitorText -Port $port -Command "info mice"
        $ps2 = $null
        foreach ($l in $mice) { if ($l -match 'Mouse #(\d+):.*PS/2') { $ps2 = $Matches[1] } }
        if ($null -ne $ps2 -and (($mice -join " ") -notmatch ('\*\s*Mouse #' + $ps2))) {
            Send-Checked -Port $port -Command ("mouse_set " + $ps2) | Out-Null
            Write-Host ("  pointer put back on the PS/2 mouse (#{0}) so you can still drive the guest." -f $ps2)
        }
    }
    if ($Attach -match 'kbd') {
        # The advice quotes back the name THIS invocation was given, rather than
        # spelling one out: -Detach resolves spec-table keys (`kbd-hs`), and the
        # hard-coded `kbd_hs` that used to stand here was a QEMU id, which only
        # worked under the pre-D4 id composition. An operator who cannot type is
        # the last person who should be handed a name that throws.
        Write-Host "  NOTE: a USB keyboard takes the guest's key events until Windows binds it."
        Write-Host ("        If you cannot type, detach it (-Detach {0}), finish the dialog," -f $Attach)
        Write-Host "        and attach it again."
    }
    foreach ($l in (Get-MonitorText -Port $port -Command "info usb")) { Write-Host ("  {0}" -f $l) }
    exit 0
}

if ($Detach -ne "") {
    # Through the spec table, so an alias detaches what it attached.
    $delId = Get-AttachId -Name $Detach
    if ($delId -eq "") {
        throw ("unknown device '{0}'. Known: {1}" -f $Detach, (($specs.Keys | Sort-Object) -join ", "))
    }
    # The SCSI child goes before its adapter, or it keeps the drive node and
    # the next adapter cannot take it (the run's Remove-RowChild lesson).
    if ($children.ContainsKey($Detach)) {
        Send-Mon -Port $port -Command ("device_del " + $delId + "_lun") -Quiet | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Send-Checked -Port $port -Command ("device_del " + $delId) | Out-Null
    exit 0
}

if ($Shot) {
    $png = Save-GuestScreenshot -Port $port -Path (Join-Path $outDir ("prep-{0}-{1}.ppm" -f $Target, (Get-Random -Maximum 9999)))
    Write-Host ("screenshot: {0}" -f $png)
    exit 0
}

if ($Status) {
    $ident = Find-ExtensionIdentity -DebugconLog $dbg
    if ($null -eq $ident.Va) {
        Write-Host "the driver has not written to the debug console yet (still booting, or not the qemu build - since task 13-L.1 no other flavour writes to port 0xE9)."
        exit 0
    }
    $table = Import-CounterTable
    Write-Host ("extension 0x{0}, MiniPortExtensionSize={1}" -f $ident.Va, $ident.Size)
    if ($ident.Size -ne $table.Sizeof) {
        Write-Host ("*** the offset table says SIZEOF {0} - counters below would be WRONG. Regenerate or reinstall." -f $table.Sizeof)
        exit 1
    }
    $c = Read-Counters -Port $port -BaseVa $ident.Va -Table $table
    # `endpoints opened` is the one that matters: it counts NON-DEFAULT endpoint
    # opens, so it moves only once a FUNCTION DRIVER has claimed a device.  That
    # is precisely what this whole pass exists to make happen.
    foreach ($n in @('devices addressed', 'slots enabled', 'endpoints opened', 'endpoint opens seen')) {
        $f = Resolve-CounterLabel -Table $table -Label $n
        Write-Host ("  {0,-22} {1}" -f $n, $c.Values[$f])
    }
    $ep = $c.Values[(Resolve-CounterLabel -Table $table -Label 'endpoints opened')]
    Write-Host ""
    if ($ep -ge 1) {
        Write-Host "GOOD: a function driver has opened a non-default endpoint, so this class is now bound."
    } else {
        Write-Host "NOT YET: nothing has opened a non-default endpoint. If a wizard is on screen, it is waiting for you."
    }
    exit 0
}

if ($Shutdown) {
    Write-Host "Shut the guest down from inside Windows instead - a qcow2 image written by a killed"
    Write-Host "QEMU is exactly the state this repository has had to recover from before."
    Write-Host "If you are certain the guest is already at 'It is now safe to turn off', use:"
    # The braces are doubled because this is a -f format string: a literal `{`
    # in one is a malformed format specifier, and with $ErrorActionPreference =
    # "Stop" the -Shutdown path died with a FormatException instead of printing
    # the safe-shutdown guidance it exists for.
    Write-Host ("  powershell -Command `"& {{ . '{0}\lib\monitor.ps1'; Send-Mon -Port {1} -Command quit -Quiet }}`"" -f $PSScriptRoot, $port)
    exit 0
}

Write-Host "Nothing to do. Pass one of -Boot, -Attach <name>, -Detach <name>, -Status, -Shot, -Shutdown, or on a fresh target -Clone / -Stamp."
Write-Host ("Known devices: {0}" -f (($specs.Keys | Sort-Object) -join ", "))



