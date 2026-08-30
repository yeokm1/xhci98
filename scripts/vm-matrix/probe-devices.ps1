<#
.SYNOPSIS
Task 10.1 - enumerate the USB device population of the INSTALLED QEMU, guestless.

.DESCRIPTION
`qemu-system-x86_64 -device help` is the authority for what this build can
present; the roadmap's own list is a starting point and not the contract.  This
script reads that list, then attaches every peripheral model to a `qemu-xhci`
in a PAUSED, GUESTLESS QEMU and reads `info usb` back, which yields:

  * whether the model instantiates at all on this build, and what it needs if
    it does not (a drive, a netdev, a chardev, real host hardware),
  * the speed it declares WHEN PLUGGED INTO AN xHCI PORT, which is the only
    speed this project cares about - QEMU's HID models carry both a Full and a
    High Speed descriptor set and pick by bus, so a speed read off a UHCI bus
    is a different number for the same model.

WHY GUESTLESS.  No guest means no OS, no driver, no enumeration - so every
answer here is a property of the emulator alone, which is exactly what task
10.1's first two columns are.  It also costs seconds rather than a boot, and
this repository has twice spent a boot on a question a paused instance could
have answered (the tier-6 hub refusal, batch 7b-V; the `bus=hub1.0` syntax
error, task 7b-A.1.0).

WHAT THIS SCRIPT CANNOT ANSWER, AND DOES NOT GUESS.  Whether a FUNCTION DRIVER
binds on each target is a guest fact - Windows 98 with NUSB and Windows 2000
SP4 ship different class-driver sets - so that column is left `?` here and is
filled by run-matrix.ps1.  The interface count is likewise left to the run: it
is visible to this driver's own descriptor snoop and to Device Manager, and
guessing it from the model name is how `usb-audio` would have been recorded as
single-interface.

.EXAMPLE
powershell -File scripts\vm-matrix\probe-devices.ps1 -OutFile out\device-population.txt
#>
[CmdletBinding()]
param(
    [string]$Qemu = "",
    [int]$MonitorPort = 55581,
    [string]$OutFile = "",
    [string]$XhciDevice = "qemu-xhci"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\monitor.ps1")
. (Join-Path $PSScriptRoot "lib\qemu.ps1")

$Qemu = Resolve-QemuBinary -Hint $Qemu
Write-Output ("qemu:  {0}" -f $Qemu)
Write-Output ("build: {0}" -f (Get-QemuVersion -Qemu $Qemu))
Write-Output ""

# ---------------------------------------------------------------- the list ---
# Every device whose bus is `usb-bus` is a peripheral; everything under the
# monitor's own "USB devices:" heading is a host CONTROLLER and is not what this
# matrix walks.  Selecting on the bus rather than on the heading is what makes
# `usb-storage` (filed under "Storage devices") appear at all.
$helpText = Invoke-NativeText -Exe $Qemu -Arguments @("-device", "help")
$models = @()
foreach ($line in ($helpText -split "`r?`n")) {
    if ($line -match '^\s*name\s+"([^"]+)",\s*bus usb-bus') { $models += $Matches[1] }
}
$models = $models | Sort-Object -Unique
if ($models.Count -eq 0) {
    throw "No `bus usb-bus` devices in -device help output. Is $Qemu really a system emulator?"
}
Write-Output ("{0} peripheral models on this build:" -f $models.Count)
Write-Output ("  " + ($models -join ", "))
Write-Output ""

# ------------------------------------------------------- backends per model ---
# What a model needs before it will instantiate.  A model that needs real host
# hardware or a real host peripheral is NOT probed and says so; a model that
# needs a backend gets a scratch one built here so that "did not instantiate"
# means the model, not the harness.
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("xhci98-probe-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$scratchImg = Join-Path $scratch "scratch.img"
$fs = [IO.File]::Create($scratchImg)
$fs.SetLength(8MB)
$fs.Close()

# A chardev may be claimed by exactly one device, so the two chardev-backed
# models get one each.  Sharing them made usb-serial read as "refused" with
# `chardev 'probechr' is already in use` - a harness fault wearing a device
# model's name, which is the failure mode this whole table exists to avoid.
$backendArgs = @(
    "-drive", ("if=none,id=probedrv,file={0},format=raw" -f $scratchImg),
    "-netdev", "user,id=probenet",
    "-chardev", "null,id=probechr1",
    "-chardev", "null,id=probechr2"
)

# addArgs: what to append to device_add.  skip: why the model is not probed.
#
# usb-bot and usb-uas are SCSI HOST ADAPTERS, not disks: they have no `drive`
# property at all (`Property 'usb-bot.drive' not found`) and take a scsi-hd on
# their own bus instead.  For the population table the empty adapter is the
# right thing to attach - it is still the USB device the driver would see - and
# giving it a disk is the matrix runner's job, not this probe's.
$modelPlan = @{
    "usb-host"        = @{ Skip = "needs a real host USB device (hostbus/hostaddr); passthrough is Phase 13's" }
    "usb-redir"       = @{ Skip = "needs a usbredir server on the host" }
    "usb-storage"     = @{ AddArgs = "drive=probedrv,removable=on" }
    "usb-net"         = @{ AddArgs = "netdev=probenet" }
    "usb-serial"      = @{ AddArgs = "chardev=probechr1" }
    "usb-braille"     = @{ AddArgs = "chardev=probechr2" }
}

# ------------------------------------------------------------- the instance ---
# p2/p3 are the USB2 and USB3 port counts.  The default qemu-xhci is 4 and 4,
# and THE PORT NUMBERS ARE ONE SPACE: 1-4 are the USB 2.0 ports and 5-8 are the
# SuperSpeed ones.  A first version of this script assigned `port=N` per model
# and everything from the fifth model on was refused, because a Full Speed
# device cannot attach to a SuperSpeed port - which is the same electrical fact
# AGENTS.md's port strategy is built on, met here as a harness bug.  The fix is
# both halves: enough USB2 ports for the whole population, and no explicit port
# at all, so QEMU places each device on a port that can carry it and the join
# key becomes the device id rather than a number this script predicted.
$qemuArgs = @(
    "-display", "none",
    "-S",
    "-M", "pc",
    "-m", "64",
    "-monitor", ("telnet:127.0.0.1:{0},server,nowait" -f $MonitorPort),
    "-device", ("{0},id=xhci,p2=15,p3=2" -f $XhciDevice)
) + $backendArgs

Write-Output ("starting a paused guestless instance on monitor {0}" -f $MonitorPort)
# Through Start-Qemu, which quotes each argument and captures stderr: a bare
# Start-Process quotes nothing, so a temp path with a space split the
# `-drive file=` argument and QEMU died with its reason in a hidden window.
$stderrFile = Join-Path $scratch "qemu-stderr.log"
$proc = Start-Qemu -Qemu $Qemu -QemuArgs $qemuArgs -StderrFile $stderrFile
try {
    if (-not (Wait-Monitor -Port $MonitorPort -TimeoutSeconds 30)) {
        $err = Get-QemuStderr -StderrFile $stderrFile
        if ($err -eq "") { $err = "(QEMU wrote nothing to stderr)" }
        throw ("the guestless instance never answered its monitor. QEMU said: {0}" -f $err)
    }

    # Per-model properties, from the model itself.  Two of them change what the
    # matrix can do and neither is guessable from the name:
    #
    #   usb_version   selects the descriptor set the model presents, which is
    #                 what decides its SPEED.  usb_version=1 attaches at 12 Mb/s
    #                 and usb_version=2 at 480 Mb/s - measured on this host with
    #                 two usb-kbd instances side by side.  That is the only way
    #                 this vehicle can present a Full Speed HID on a root port,
    #                 which is the exact shape that bugchecked in Phase 5 task 7,
    #                 so a model carrying it is worth two matrix rows.
    #   pcap          writes the device's wire traffic to a file.  It is a
    #                 property of QEMU's USB device base class, so EVERY model
    #                 has it - which gives the matrix a host-side oracle for the
    #                 descriptors, and therefore for the interface count, with
    #                 no guest agent at all.
    $props = @{}
    foreach ($model in $models) {
        $helpOne = Invoke-NativeText -Exe $Qemu `
            -Arguments @("-device", ("{0},help" -f $model))
        $props[$model] = @{
            UsbVersion = ($helpOne -match 'usb_version=')
            Pcap       = ($helpOne -match 'pcap=')
        }
    }

    $results = @()
    $n = 0
    foreach ($model in $models) {
        $plan = $modelPlan[$model]
        if ($null -ne $plan -and $plan.ContainsKey("Skip")) {
            $results += [pscustomobject]@{
                Model = $model; Variant = ""; Status = "not probed"; Speed = ""
                Product = ""; Note = $plan.Skip; Id = ""
                Pcap = $props[$model].Pcap
            }
            continue
        }

        # Default first, then the explicit Full Speed variant where the model
        # has the knob.  The default is probed rather than assumed to equal
        # usb_version=2, because a model whose default is 1 would otherwise be
        # recorded at a speed the matrix never presents.
        $variants = @("")
        if ($props[$model].UsbVersion) { $variants += "usb_version=1" }

        foreach ($variant in $variants) {
            $n++
            $id = "p{0}" -f $n
            $add = "device_add {0},id={1},bus=xhci.0" -f $model, $id
            if ($null -ne $plan -and $plan.ContainsKey("AddArgs") -and $plan.AddArgs -ne "") {
                $add = "{0},{1}" -f $add, $plan.AddArgs
            }
            if ($variant -ne "") { $add = "{0},{1}" -f $add, $variant }
            $before = Get-MonitorErrors
            $reply = Send-Checked -Port $MonitorPort -Command $add
            $failed = (Get-MonitorErrors) -gt $before

            $row = [pscustomobject]@{
                Model = $model
                Variant = if ($variant -eq "") { "(default)" } else { $variant }
                Status = if ($failed) { "refused" } else { "attached" }
                Speed = ""
                Product = ""
                Port = ""
                Note = if ($failed) { ($reply -join "; ") } else { "" }
                Id = if ($failed) { "" } else { $id }
                Pcap = $props[$model].Pcap
            }

            # ONE DEVICE ON THE BUS AT A TIME, read and then removed.  The first
            # version attached the whole population at once and read `info usb`
            # at the end, which broke in a way worth recording: an xHCI
            # controller tops out at 15 USB 2.0 root ports, and once they were
            # full QEMU CASCADED the remaining devices onto the usb-hub this
            # same probe had just attached.  They showed up at `Port 6.1` and
            # `Port 6.2` - so the speed column would have reported a
            # behind-a-Full-Speed-hub speed as a root-port speed, silently, for
            # whichever models happened to sort last.  Attaching one at a time
            # removes the port-exhaustion coupling instead of raising a limit
            # until it happens not to bite.
            if (-not $failed) {
                foreach ($l in (Get-MonitorText -Port $MonitorPort -Command "info usb")) {
                    if ($l -match 'Device\s+[0-9.]+,\s*Port\s+([0-9.]+),\s*Speed\s+([0-9.]+)\s*Mb/s,\s*Product\s+(.*?),\s*ID:\s*(\S+)\s*$') {
                        if ($Matches[4] -ne $id) { continue }
                        $row.Port = $Matches[1]
                        $row.Speed = $Matches[2]
                        $row.Product = $Matches[3].Trim()
                    }
                }
                if ($row.Speed -eq "") {
                    # device_add returned no error and the device is still not
                    # on the bus.  A distinct outcome from a refusal, and it
                    # must never be reported as an attach.
                    $row.Status = "attached, invisible"
                    $row.Note = ("device_add returned no error but info usb does not list id={0}" -f $id)
                } elseif ($row.Port -match '\.') {
                    # Nothing should be behind anything: the bus was empty when
                    # this device was added.  If it is, the reading is not a
                    # root-port reading and says so rather than being averaged in.
                    $row.Status = "attached, not on a root port"
                    $row.Note = ("landed at port {0}, so this speed is not a root-port speed" -f $row.Port)
                }
                Send-Checked -Port $MonitorPort -Command ("device_del {0}" -f $id) | Out-Null
                # device_del is asynchronous - the guest (here, nothing) has to
                # release it - so wait for the bus to actually empty rather than
                # letting the next model share it.
                # Only a COMPLETE reply that omits the id says it left; an
                # unanswered `info usb` prints the same nothing an empty bus
                # does, and Test-UsbDeviceListed returns $null for it.
                for ($w = 0; $w -lt 20; $w++) {
                    if ((Test-UsbDeviceListed -Port $MonitorPort -Id $id) -eq $false) { break }
                    Start-Sleep -Milliseconds 100
                }
            }
            $results += $row
        }
    }

    # The bus must be empty now: every attach above was matched by a device_del.
    # A leftover means one of those deletes did not take, and every speed read
    # after it is suspect.
    $leftover = Get-MonitorText -Port $MonitorPort -Command "info usb"
    $leftover = $leftover | Where-Object { $_ -match '^Device\s' }
    if ($leftover.Count -gt 0) {
        Write-Host ""
        Write-Host "*** the bus is not empty after the probe - these were never released:"
        foreach ($l in $leftover) { Write-Host ("    {0}" -f $l) }
        Add-MonitorError "a device_del did not take; the readings above may not all be root-port readings"
    }

    $speeds = ($results | Where-Object { $_.Speed -ne "" } | ForEach-Object { $_.Speed } | Sort-Object -Unique)
    $lowSpeed = ($results | Where-Object { $_.Speed -eq "1.5" })

    $report = @()
    $report += ("# xhci98 Phase 10 task 10.1 - USB device population")
    $report += ("# qemu:  {0}" -f $Qemu)
    $report += ("# build: {0}" -f (Get-QemuVersion -Qemu $Qemu))
    $report += ("# bus:   {0} (an xHCI port - a model's speed is bus-dependent)" -f $XhciDevice)
    $report += ("#")
    $report += ("# Speed is in Mb/s as QEMU reports it: 1.5 = Low, 12 = Full, 480 = High.")
    $report += ("# 'binds' and 'interfaces' are guest facts and are filled by run-matrix.ps1.")
    $report += ("# pcap = the model can write its own wire traffic to a file, which is")
    $report += ("#        this harness's guest-agent-free descriptor oracle.")
    $report += ("")
    $report += ("{0,-18} {1,-14} {2,-20} {3,6} {4,5}  {5}" -f "MODEL", "VARIANT", "STATUS", "SPEED", "PCAP", "PRODUCT / NOTE")
    foreach ($r in ($results | Sort-Object Model, Variant)) {
        $tail = if ($r.Product -ne "") { $r.Product } else { $r.Note }
        $report += ("{0,-18} {1,-14} {2,-20} {3,6} {4,5}  {5}" -f `
            $r.Model, $r.Variant, $r.Status, $r.Speed, $(if ($r.Pcap) { "yes" } else { "no" }), $tail)
    }
    $report += ("")
    $report += ("# Speeds this vehicle can present on an xHCI root port: {0} Mb/s." -f ($speeds -join ", "))
    if ($lowSpeed.Count -eq 0) {
        $report += ("# NO LOW SPEED (1.5 Mb/s) DEVICE EXISTS IN THIS BUILD.  That is a measured")
        $report += ("# property of the vehicle, not a gap in this matrix: no row here can cover")
        $report += ("# the Low Speed path, and any Low Speed clause belongs to bare metal.")
    } else {
        $report += ("# A Low Speed model IS present in this build: {0}. Earlier phases recorded" -f (($lowSpeed | ForEach-Object { $_.Model }) -join ", "))
        $report += ("# that none existed - re-read that finding before relying on it.")
    }

    Write-Output ""
    foreach ($l in $report) { Write-Output $l }

    if ($OutFile -ne "") {
        $dir = Split-Path -Parent $OutFile
        if ($dir -ne "" -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Set-Content -Path $OutFile -Value $report -Encoding utf8
        Write-Output ""
        Write-Output ("written: {0}" -f $OutFile)
    }
} finally {
    Send-Mon -Port $MonitorPort -Command "quit" -Quiet | Out-Null
    Start-Sleep -Milliseconds 500
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    # A QEMU that still holds scratch.img makes the removal fail quietly and
    # the directory leaks; a second attempt after the process has gone, and a
    # line naming the leftover when that fails too.
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $scratch) {
        Start-Sleep -Milliseconds 500
        Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $scratch) {
        Write-Host ("*** the scratch directory could not be removed (QEMU may still hold scratch.img): {0}" -f $scratch)
    }
}

# Monitor errors and a bus that did not empty were reported and counted, and
# then nothing read the count: the script exited 0 whatever it had found, so a
# caller - or a person reading a green run - had no signal short of re-reading
# the whole report.  The count is the verdict; report it as one.
$monitorErrors = Get-MonitorErrors
if ($monitorErrors -gt 0) {
    Write-Output ""
    Write-Output ("*** {0} monitor error(s) or leftover device(s) during the probe - the table above is not trustworthy." -f $monitorErrors)
    exit 1
}
exit 0
