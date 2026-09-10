<#
.SYNOPSIS
The shared body of the Windows Vista and Windows 7 (32-bit) guest generators
(roadmap Phase 22).

.DESCRIPTION
Vista x86 and Windows 7 x86 are ONE recipe. They are the two guests roadmap
task 22.4 asks for, they were created in the same pass and off the same media
set as the static readings of tasks 22.1 and 22.2, and everything about their
machines is identical except the ISO, the image name and the monitor port. So
the machine is written once here and the two generators -
scripts\setup-qemu-vista.ps1 and scripts\setup-qemu-win7.ps1 - are the thin
callers that name their guest.

That is the one structural difference from the six generators that came
before, each of which carries its own copy of this text, and it is deliberate.
The 2026-09-07 audit's H28 and J6 are the reason: five copies of the QEMU
resolver had drifted apart unnoticed, and two guests born on the same day out
of one recipe are exactly the pair that would drift next. A per-guest
difference belongs in a parameter here, never in a second copy of the body.

WHAT IS THE SAME AS THE 32-BIT XP RECIPE: -machine pc (ACPI on), -vga std,
-boot d on every install boot (the DVD's "Press any key" falls through to the
hard disk, which is what Setup's own reboots need), qemu-xhci,p3=0 on the run
launcher with no companion EHCI by default, no USB device boot-attached, the
VVFAT transfer drive, and the port-0xE9 debug console rotated per boot.

WHAT DIFFERS, AND WHY:

  -cpu     qemu64, not the 32-bit XP guest's pentium3. pentium3 predates the
           NX bit, and Windows 7 requires NX: Setup refuses a processor
           without one. This is a 32-bit guest that wants a 64-bit-era CPU
           MODEL, which is not the same thing as a 64-bit guest, and it is the
           line most likely to be "corrected" back to the 32-bit recipe. The
           generator refuses such a -Cpu outright and the launcher gate
           asserts the generated text.
  RAM      2048 MB, not 512. Windows 7's own floor is 1 GB and Vista's is
           512 MB; 2048 is comfort. Unlike the XP x64 guest, where measurement
           M5 made the size a non-question, NOTHING HAS BEEN MEASURED HERE
           ABOUT WHAT THE 6.x usbport DOES WITH MEMORY ABOVE 4 GB - and it
           does not arise, because a 32-bit guest with 2048 MB has none. Do
           not raise this without reading measurement M5 of design record 11
           first: Windows 7's usbport has a second IoGetDmaAdapter call site
           that asks for Dma64BitAddresses = 1, and what keeps this driver
           away from it is that it declares interface Version 200 - not
           anything about the guest's RAM.
  Disk     32 GB. Windows 7 x86 wants 16 GB free and Vista 15 GB; qcow2 is
           sparse, so the file costs what the install actually writes.
  Monitor  55565 (Vista) and 55566 (Windows 7), and NOT the next two free
           numbers. 55555-55562 belong to guests that exist - 2a, 2b, the SMP
           2d, the ACPI-HAL Windows 2000 machine, 32-bit XP, the xHCI-only
           Windows 2000 machine, Windows ME and XP x64, in that order - and
           **55563 and 55564 are reserved in writing for roadmap task 21.8's
           Vista x64 and Windows 7 x64 guests**, which are not built yet
           (build-and-test.md, "Vista x64 and Windows 7 x64 target VMs -
           planned"). Two unbuilt guests and two guests being built claimed
           the same pair, which is how the collision the launcher gate exists
           to catch actually arrives: the gate can only see launchers that
           have been generated, so a reservation on paper has to be honoured
           by hand. These take the pair above and leave 21.8's alone.
  Accel    A PARAMETER WITH NO INHERITED DEFAULT. Roadmap task 22.4 says to
           probe the accelerator per host AND per guest, because Phase 21 paid
           for that rule twice in opposite directions: every 32-bit guest here
           wants WHPX (lessons.md, "The vector-0xD1 storm is the
           accelerator"), and the XP x64 guest wants TCG because WHPX wedges
           its Setup. A 32-bit Vista or Windows 7 guest is covered by neither
           reading - it shares the bitness of one and the era of the other -
           so each generator carries the value measured for ITS guest, and the
           measurement is in build-and-test.md.

WHAT THIS RECIPE OWES THAT NO EARLIER ONE DID. Both these systems stage a
driver package into a driver store, and both warn at install that an unsigned
package's publisher cannot be verified. Neither is a machine setting and
neither is configured here; they are what roadmap task 22.4 goes to the guest
to observe. Kernel-mode code signing enforcement is x64-only, so a prompt and
not a refusal is the expectation - written into Phase 22 as THE ASSUMPTION TO
CONFIRM ON THE GUEST, since it is the one that would make the phase pointless
if wrong.
#>

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

#
# The CPU models with no NX bit. Windows 7 Setup refuses a processor without
# one, and pentium3 - the 32-bit XP guest's model, and the single most likely
# thing to survive a copy-paste into these launchers - is the first name that
# matches. Refusing here is cheaper than reading the refusal off a guest.
#
function Test-Nt6CpuModel {
    param([string]$Cpu, [string]$GuestName)
    if ($Cpu -match '^(486|pentium|athlon$|n270|kvm32|qemu32|coreduo)') {
        throw "-Cpu $Cpu has no NX bit; $GuestName Setup needs a processor model that has one (qemu64, core2duo, ...)."
    }
}

function New-Nt6QemuGuest {
    param(
        # What this guest is called, in prose: "Windows Vista Business SP2".
        [Parameter(Mandatory = $true)][string]$GuestName,
        # The stem every generated name is built from: "vista" gives
        # vm\vista.img, vm\xfervista, vm\vista-debugcon.log and
        # scripts\local\qemu-vista-install.cmd / -run.cmd.
        [Parameter(Mandatory = $true)][string]$Stem,
        # The launcher's environment variable for the ISO: "VISTA_ISO".
        [Parameter(Mandatory = $true)][string]$IsoVar,
        # The generator that called this, named in the launchers' own comments
        # so a reader knows what to regenerate rather than edit.
        [Parameter(Mandatory = $true)][string]$GeneratorName,
        # That generator's own ISO parameter name, for the same reason.
        [Parameter(Mandatory = $true)][string]$IsoParamName,
        # The measured accelerator for THIS guest, and the sentences saying
        # what was measured. Both are the caller's, because both are per-guest.
        [Parameter(Mandatory = $true)][string]$Accel,
        [Parameter(Mandatory = $true)][string[]]$AccelNote,
        [Parameter(Mandatory = $true)][int]$MonitorPort,
        [string]$Iso = "",
        [string]$VmDir = "",
        [string]$LocalScriptDir = "",
        [string]$DiskSize = "32G",
        [string]$QemuBinDir = "",
        [string]$XhciDevice = "qemu-xhci,p3=0",
        [string]$Cpu = "qemu64",
        [int]$MemoryMb = 2048,
        [switch]$CreateDisk
    )

    if ([string]::IsNullOrWhiteSpace($VmDir)) {
        $VmDir = Get-DefaultVmDir
    }
    if ([string]::IsNullOrWhiteSpace($LocalScriptDir)) {
        $LocalScriptDir = Get-DefaultLocalScriptDir
    }

    Write-Step "Checking host"
    Test-SetupHost

    Write-Step "Checking QEMU"
    $qemuSystem = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-system-x86_64.exe"
    $qemuImg = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-img.exe"

    if ($null -eq $qemuSystem) {
        Write-Warn "qemu-system-x86_64.exe is not on PATH. Install QEMU (see setup-qemu.ps1) or pass -QemuBinDir."
        $qemuSystemCommand = ""
    } else {
        Write-Ok "Found $qemuSystem"
        $qemuSystemCommand = $qemuSystem
    }

    if ($null -eq $qemuImg) {
        Write-Warn "qemu-img.exe is not on PATH. Disk image creation will be skipped unless QEMU is installed."
    } else {
        Write-Ok "Found $qemuImg"
    }

    Test-Nt6CpuModel -Cpu $Cpu -GuestName $GuestName

    Write-Step "Creating local directories"
    Ensure-Directory $VmDir
    Ensure-Directory $LocalScriptDir
    $xferDir = Join-Path $VmDir ("xfer" + $Stem)
    Ensure-Directory $xferDir
    $diskImage = Join-Path $VmDir ($Stem + ".img")
    $debugConLog = Join-Path $VmDir ($Stem + "-debugcon.log")
    $debugConPreviousLog = Join-Path $VmDir ($Stem + "-debugcon.previous.log")
    $traceLogBase = Join-Path $VmDir ($Stem + "-qemu-trace")
    $audioDev = $Stem + "aud"
    Write-Ok "VM directory: $VmDir"
    Write-Ok "Local script directory: $LocalScriptDir"
    Write-Ok "Transfer (VVFAT) directory: $xferDir"

    # The empty case first: -LiteralPath refuses an empty string outright, and
    # under "Stop" that ends the run with a parameter-binding error instead of
    # the sentence the operator needs.
    if ([string]::IsNullOrWhiteSpace($Iso)) {
        Write-Warn "No -$IsoParamName given. The launchers are still written; edit the $IsoVar line in them, or re-run with -$IsoParamName <path>."
    } elseif (-not (Test-Path -LiteralPath $Iso)) {
        Write-Warn "$GuestName ISO not found at: $Iso (pass -$IsoParamName to override)."
    } else {
        Write-Ok "$GuestName ISO: $Iso"
    }

    if ($CreateDisk) {
        Write-Step "Creating QEMU disk image"
        if (-not (Test-Path -LiteralPath $diskImage)) {
            if ($null -eq $qemuImg) {
                Write-Warn "Skipping $diskImage because qemu-img.exe is not available."
            } else {
                & $qemuImg create -f qcow2 $diskImage $DiskSize | Out-Host
                Write-Ok "Created $diskImage ($DiskSize)"
            }
        } else {
            Write-Ok "Disk image already exists: $diskImage"
        }
    }

    Write-Step "Writing QEMU launchers"
    Write-Ok "Using xHCI device model: $XhciDevice"
    Write-Ok "Using CPU model: $Cpu; $MemoryMb MB; accelerator $Accel; monitor port $MonitorPort"

    # QEMU is resolved at RUN time by the launcher, not baked in here: the host
    # that generated a launcher is not always the host that runs it
    # (scripts\local is git-ignored and OneDrive-synced). One resolver for all
    # the generators, in common.ps1 (the 2026-09-07 audit's H28).
    $qemuResolve = Get-QemuLauncherResolver -FoundPath $qemuSystemCommand

    $accelComment = @()
    foreach ($line in $AccelNote) { $accelComment += ("rem " + $line) }

    $installCmd = Join-Path $LocalScriptDir ("qemu-" + $Stem + "-install.cmd")
    Write-AsciiFile $installCmd (@(
        "@echo off",
        "rem $GuestName guest - install launcher.",
        "rem Generated by scripts\$GeneratorName; regenerate rather than edit.",
        "rem",
        "rem One of the two guests of roadmap Phase 22: whether the 32-bit",
        "rem xhci98.sys this project already ships installs, loads and works on",
        "rem Windows Vista and Windows 7 as it stands. Nothing is built for this",
        "rem guest - it is the binary on the .NTx86 half of the INF that exists.",
        "rem",
        "rem -cpu $Cpu, NOT the 32-bit XP guest's pentium3: that model predates",
        "rem the NX bit and Windows 7 Setup refuses a processor without one. A",
        "rem 32-bit guest wanting a 64-bit-era CPU model is not the same thing as a",
        "rem 64-bit guest.",
        "rem $MemoryMb MB, not 512, and a 32-bit guest, so nothing is above 4 GB.",
        "rem",
        "rem Accelerator: -accel $Accel."
    ) + $accelComment + @(
        "rem The HAL is fixed at INSTALL time, so a guest installed under one rung",
        "rem must be booted under it too; regenerate with -Accel rather than editing",
        "rem this file, and the launcher gate asserts that install and run agree.",
        "rem -vga std because cirrus does not restore across an ACPI sleep.",
        "rem",
        "rem The xHCI is absent during install, as for every other guest, so it",
        "rem appears as a fresh unrecognised device on the first boot from the run",
        "rem launcher. -boot d every time: the DVD's ""Press any key to boot from CD",
        "rem or DVD"" falls through to the hard disk when no key is pressed, which is",
        "rem what Setup's own reboots need.",
        "rem",
        "rem The owner drives Setup at the console - the standing decision of",
        "rem 2026-09-03, taken for the 32-bit XP guest and unchanged here.",
        "set ""$IsoVar=$Iso""",
        "if ""%$IsoVar%""=="""" (",
        "  echo Edit this file or set -$IsoParamName when running $GeneratorName.",
        "  exit /b 1",
        ")",
        "if not exist ""%$IsoVar%"" (",
        "  echo Missing ISO: %$IsoVar%",
        "  exit /b 1",
        ")",
        "if not exist ""$diskImage"" (",
        "  echo Missing image: $diskImage - rerun $GeneratorName -CreateDisk",
        "  exit /b 1",
        ")"
    ) + $qemuResolve + @(
        """%QEMU%"" ^",
        "  -name ""xhci98 $GuestName"" ^",
        "  -machine pc ^",
        "  -accel $Accel ^",
        "  -cpu $Cpu ^",
        "  -m $MemoryMb ^",
        "  -vga std ^",
        "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
        "  -cdrom ""%$IsoVar%"" ^",
        "  -boot d ^",
        "  -rtc base=localtime ^",
        "  -net none ^",
        "  -action reboot=reset -no-shutdown ^",
        "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
    ))

    $runCmd = Join-Path $LocalScriptDir ("qemu-" + $Stem + "-run.cmd")
    Write-AsciiFile $runCmd (@(
        "@echo off",
        "rem $GuestName guest - RUN launcher.",
        "rem Generated by scripts\$GeneratorName; regenerate rather than edit.",
        "rem",
        "rem Same machine as qemu-$Stem-install.cmd (ACPI on, -accel $Accel,",
        "rem -cpu $Cpu, $MemoryMb MB, -vga std) plus:",
        "rem  - the xHCI ($($XhciDevice)): there is no in-box driver for",
        "rem    PCI\CC_0C0330 on 6.0 or 6.1 either, so it shows as an unrecognised",
        "rem    USB controller and the package installs through the INF's .NTx86",
        "rem    half from the transfer drive. p3=0 (USB 2.0 root ports only, as",
        "rem    every other guest's launcher) because QEMU pins a SuperSpeed-capable",
        "rem    device to a SuperSpeed-capable port and does not model the USB 2.0",
        "rem    fallback real hardware gives: on the default 4+4 layout a",
        "rem    hot-plugged usb-storage attached at 5000 Mb/s lands on a USB3 port",
        "rem    this driver leaves unmanaged and the guest never saw it (2026-09-03,",
        "rem    run p194, on the 32-bit XP guest). Regenerate with",
        "rem    -XhciDevice qemu-xhci for the 4+4 layout's own reading.",
        "rem  - an audio backend (-audiodev none,id=$audioDev) so a composite",
        "rem    usb-audio can be hot-plugged:",
        "rem    device_add usb-audio,id=a1,bus=xhci.0,audiodev=$audioDev",
        "rem  - a VVFAT transfer drive backed by vm\xfer$Stem (read-only on the host",
        "rem    side; snapshot=on gives the guest a throw-away writable overlay). It",
        "rem    carries the qemu-flavour package, the only flavour that writes the",
        "rem    0xE9 trace: make-package.ps1 -Flavor qemu -OutDir vm\xfer$Stem.",
        "rem  - isa-debugcon at 0xE9 -> vm\$Stem-debugcon.log, rotated like the",
        "rem    other guests' logs so a stale DriverEntry cannot be read as this",
        "rem    boot's.",
        "rem  - the QEMU xhci trace events of scripts\local\xhci-trace-events.txt,",
        "rem    when that file exists.",
        "rem  - NO companion EHCI by default - and on this pair that is not the",
        "rem    reading it is on the NT 5.x guests. An xHCI-only XP or XP x64",
        "rem    install has no usbport.sys on disk at all (Code 39 on the first XP",
        "rem    boot, 2026-09-03), which is what the INF's LayoutFile route answers.",
        "rem    Vista and later apply a whole install image instead, so all four",
        "rem    Microsoft USB files should already be in System32\drivers whatever",
        "rem    controllers the machine has - read off the MEDIA on 2026-09-09, and",
        "rem    roadmap task 22.3 is that same reading taken off an installed guest.",
        "rem    %2 = ehci adds the companion EHCI.",
        "rem  - AND WHAT NO FLAG HERE CONTROLS: this system stages the package into",
        "rem    a driver store, and warns that an unsigned package's publisher",
        "rem    cannot be verified. Kernel-mode code signing enforcement is",
        "rem    x64-only, so a prompt and not a refusal is the expectation - and it",
        "rem    is written into roadmap Phase 22 as the assumption to CONFIRM on",
        "rem    this guest, since it is the one that would make the phase pointless",
        "rem    if wrong. Record what the prompt actually did.",
        "rem No USB device is boot-attached: hot-plug from the monitor (port $MonitorPort)",
        "rem after the desktop is up, e.g.  device_add usb-mouse,id=m1,bus=xhci.0",
        "rem (no port= is needed: QEMU takes the first free root port, and a number",
        "rem above the port count is refused). QEMU 11.1.0-rc2 parks a boot in",
        "rem SeaBIOS's SMM handler when a USB device is attached at boot",
        "rem (docs\contributing\lessons.md).",
        "rem The DVD stays attached when the ISO is on this host, in case Setup asks.",
        "rem %1 = run tag for the QEMU trace file name.",
        "setlocal",
        "set TAG=%1",
        "if ""%TAG%""=="""" set TAG=run",
        "set ""EHCI=""",
        "if /i ""%2""==""ehci"" set ""EHCI=-device usb-ehci,id=ehci""",
        "set ""$IsoVar=$Iso""",
        "set ""CDROM=-cdrom ""%$IsoVar%""""",
        "if not exist ""%$IsoVar%"" (",
        "  echo NOTE: %$IsoVar% is not on this host - booting with no CD attached.",
        "  set ""CDROM=""",
        ")",
        "set ""TRACE=""",
        "if exist ""%~dp0xhci-trace-events.txt"" set ""TRACE=-trace events=%~dp0xhci-trace-events.txt,file=$traceLogBase.%TAG%.log""",
        "if not exist ""$xferDir"" (",
        "  echo Missing transfer directory: $xferDir",
        "  exit /b 1",
        ")",
        "if exist ""$debugConLog"" (",
        "  for %%S in (""$debugConLog"") do if not ""%%~zS""==""0"" (",
        "    move /y ""$debugConLog"" ""$debugConPreviousLog"" >nul",
        "    if errorlevel 1 (",
        "      echo Could not archive the prior debug-console log.",
        "      exit /b 1",
        "    )",
        "  )",
        ")"
    ) + $qemuResolve + @(
        """%QEMU%"" ^",
        "  -name ""xhci98 $GuestName"" ^",
        "  -machine pc ^",
        "  -accel $Accel ^",
        "  -cpu $Cpu ^",
        "  -m $MemoryMb ^",
        "  -vga std ^",
        "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
        "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
        "  %CDROM% ^",
        "  %EHCI% ^",
        "  -device $XhciDevice,id=xhci ^",
        "  -audiodev none,id=$audioDev ^",
        "  -chardev file,id=dbgcon,path=""$debugConLog"" ^",
        "  -device isa-debugcon,iobase=0xe9,chardev=dbgcon ^",
        "  %TRACE% ^",
        "  -boot c ^",
        "  -rtc base=localtime ^",
        "  -net none ^",
        "  -action reboot=reset -no-shutdown ^",
        "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
    ))

    Write-Ok "Wrote $installCmd"
    Write-Ok "Wrote $runCmd"

    Write-Step "Next steps"
    Write-Host "  1. Run scripts\local\qemu-$Stem-install.cmd and install $GuestName by hand (the owner drives Setup)."
    Write-Host "  2. Shut the guest down from the Start menu; snapshot: qemu-img snapshot -c $Stem-clean-install vm\$Stem.img"
    Write-Host "  3. Roadmap task 22.3's two readings, neither needing a driver and neither needing a boot:"
    Write-Host "     7z l vm\$Stem.img, then look for usbport.sys / usbhub.sys / usbd.sys / usbehci.sys in"
    Write-Host "     Windows\System32\drivers and in Windows\System32\DriverStore\FileRepository."
    Write-Host "  4. Stage the package: make-package.ps1 -Flavor qemu -OutDir vm\xfer$Stem, then boot"
    Write-Host "     qemu-$Stem-run.cmd <tag> and install from the transfer drive."
    Write-Host "     RECORD WHAT THE UNSIGNED-DRIVER PROMPT ACTUALLY DID (task 22.4)."
}
