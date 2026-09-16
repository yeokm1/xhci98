<#
.SYNOPSIS
The shared body of the four Windows Vista and Windows 7 guest generators, both
architectures (roadmap Phase 22 for the 32-bit pair, task 21.8 for the 64-bit
one).

.DESCRIPTION
Vista and Windows 7 are ONE recipe, and -Arch is what makes it two. The 32-bit
pair are the guests roadmap task 22.4 asks for, created in the same pass and
off the same media set as the static readings of tasks 22.1 and 22.2; the
64-bit pair are task 21.8's, off the media task 21.7 read ITS measurements
from. Everything about all four machines is identical except the ISO, the image
name, the monitor port, the accelerator, and the arch-dependent parts of what
the launchers say about themselves. So the machine is written once here and the
four generators - scripts\setup-qemu-vista.ps1, setup-qemu-win7.ps1,
setup-qemu-vista-x64.ps1 and setup-qemu-win7-x64.ps1 - are the thin callers
that name their guest.

That is the one structural difference from the six generators that came
before, each of which carries its own copy of this text, and it is deliberate.
The 2026-09-07 audit's H28 and J6 are the reason: five copies of the QEMU
resolver had drifted apart unnoticed, and two guests born on the same day out
of one recipe are exactly the pair that would drift next. A per-guest
difference belongs in a parameter here, never in a second copy of the body.

**THE 64-BIT PAIR SHARE THE MACHINE AND NOT THE CONCLUSIONS**, and that is the
whole reason -Arch exists rather than a second body. Four things invert with it
and every one of them is load-bearing:

  the CPU guard   32-bit needs an NX bit (Windows 7 Setup refuses a processor
                  without one); 64-bit needs LONG MODE. qemu64 satisfies both,
                  which is why one default serves all four, but the refusal
                  has to name the feature the guest actually needs or it
                  passes a model that cannot boot.
  the INF half    .NTx86 and the 32-bit package, against .NTamd64 and the
                  SECOND package from the second toolchain. "One binary" has
                  nothing to say across this line (AGENTS.md, Project Purpose).
  code signing    THE INVERSION THAT MATTERS. On the 32-bit pair, kernel-mode
                  code signing enforcement is x64-only, so an unsigned driver
                  raises a PROMPT and loads. On the 64-bit pair it is enforced,
                  so an unsigned driver loads only on an F8 boot with
                  signature enforcement disabled (measured, tasks 21.8 and
                  22.5). Test-signing is no route: the package is unsigned.
  memory          A 32-bit guest with 2048 MB has nothing above 4 GB and the
                  question does not arise. A 64-bit guest above 4 GB is a
                  change to the TEST SURFACE, not tuning - it is the first
                  time the HAL's double-buffering can execute at all. See the
                  -MemoryMb note below.

WHAT IS THE SAME AS THE 32-BIT XP RECIPE: -machine pc (ACPI on), -vga std,
-boot d on every install boot (the DVD's "Press any key" falls through to the
hard disk, which is what Setup's own reboots need), qemu-xhci,p3=0 on the run
launcher with no companion EHCI by default, no USB device boot-attached, the
VVFAT transfer drive, and the port-0xE9 debug console rotated per boot.

WHAT DIFFERS, AND WHY:

  -cpu     qemu64, not the 32-bit XP guest's pentium3, and one default serves
           all four guests for two DIFFERENT reasons. On the 32-bit pair:
           pentium3 predates the NX bit, and Windows 7 requires NX - Setup
           refuses a processor without one. A 32-bit guest that wants a
           64-bit-era CPU MODEL is not the same thing as a 64-bit guest, and
           that is the line most likely to be "corrected" back to the 32-bit
           recipe. On the 64-bit pair: pentium3 has no LONG MODE and the guest
           does not leave the BIOS. The generator refuses the wrong model
           outright, naming the feature THAT guest needs, and the launcher
           gate asserts the generated text.
  RAM      2048 MB, not 512. Windows 7's own floor is 1 GB and Vista's is
           512 MB; 2048 is comfort, and it is the install-time value for all
           four guests.

           ON THE 32-BIT PAIR THE QUESTION ABOVE 4 GB DOES NOT ARISE, because
           a 32-bit guest with 2048 MB has nothing there. Do not raise it
           without reading measurement M5 of design record 11 first: Windows
           7's usbport has a second IoGetDmaAdapter call site that asks for
           Dma64BitAddresses = 1, and what keeps this driver away from it is
           that it declares interface Version 200 - not anything about the
           guest's RAM.

           ON THE 64-BIT PAIR IT WOULD BE A DELIBERATE EXPERIMENT AND NOT A
           DEFAULT. It was a clause of roadmap task 21.8 until the owner
           removed it on 2026-09-16, untaken, so THE HAL'S DOUBLE-BUFFERING HAS
           NEVER ONCE EXECUTED. -MemoryMb still works; install at 2048 and
           regenerate afterwards if the run is ever taken. What that opens
           is not 64-bit addressing - the second IoGetDmaAdapter is
           version-gated, not RAM-gated, and no amount of memory reaches it -
           but the SHAPE of the scatter-gather list this driver walks, read
           off ProbeSgDisordered / ProbeSgGapped / ProbeSgHighDwords /
           ProbeSgMapped. Note the asymmetry before running it: a high address
           in an SG element is refused and counted (src\xhci_xfer.c:542), but
           USBPORT_RESOURCES.StartPA is a ULONG, so the common buffer carries
           no such check and a violation there would be silent.
  Disk     32 GB. Windows 7 x86 wants 16 GB free and Vista 15 GB, and the
           64-bit editions want 20 GB; qcow2 is sparse, so the file costs what
           the install actually writes.
  Monitor  55565 (Vista) and 55566 (Windows 7) for the 32-bit pair; 55563 and
           55564 for the 64-bit one, which is the pair roadmap task 21.8
           reserved in writing and has now claimed. 55555-55562 belong to
           guests that exist - 2a, 2b, the SMP 2d, the ACPI-HAL Windows 2000
           machine, 32-bit XP, the xHCI-only Windows 2000 machine, Windows ME
           and XP x64, in that order.

           The 32-bit pair were drafted onto 55563/55564 on 2026-09-10 and
           moved off them, because those two were spoken for by guests that
           did not exist yet. That is how the collision the launcher gate
           exists to catch actually arrives: the gate can only see launchers
           that have been generated, so a reservation on paper has to be
           honoured by hand until the generator claiming it is written. It now
           is, so the gate's reservation check is gone and the ordinary
           no-two-guests-share-a-port scan covers all four.
  Smp      4 vCPUs, where every guest before this one takes the default 1.
           This is about the accelerator, not the guest: Vista must run under
           TCG here (below), and single-threaded TCG on a 2.0 GHz i7-9700T is
           painful. QEMU emulates x86-on-x86 with MULTI-THREADED TCG, so vCPUs
           become host threads and the emulation parallelises; four rather
           than the host's eight leaves room for QEMU's own I/O and display
           threads. `thread=multi` is derived here for a tcg accelerator and
           is NEVER handed to WHPX, which rejects the whole -accel argument
           rather than ignoring an option it does not know. The HAL is fixed
           at install time, so -Smp must not differ between a guest's install
           and run launchers, and the launcher gate asserts that it does not.
  Accel    A PARAMETER WITH NO INHERITED DEFAULT. Roadmap task 22.4 says to
           probe the accelerator per host AND per guest, because Phase 21 paid
           for that rule twice in opposite directions: every 32-bit guest here
           wants WHPX (lessons.md, "The vector-0xD1 storm is the
           accelerator"), and the XP x64 guest wants TCG because WHPX wedges
           its Setup. A 32-bit Vista or Windows 7 guest is covered by neither
           reading - it shares the bitness of one and the era of the other -
           so each generator carries the value measured for ITS guest, and the
           measurement is in build-and-test.md.

           **THE 32-BIT PAIR THEN DISAGREED WITH EACH OTHER**, which is the
           strongest form the rule has taken yet: two guests one WDM revision
           apart, out of one recipe, on one host, in one afternoon. Vista
           wants tcg and Windows 7 installed clean under WHPX. Do not "tidy"
           the two values into agreement.

           **AND THE 64-BIT PAIR AGREE WITH EACH OTHER, MEASURED 2026-09-10:
           BOTH WANT TCG.** setup-qemu-vista-x64.ps1 and setup-qemu-win7-x64.ps1
           carried no default at all until then and refused to run without an
           explicit -Accel; both installs have since COMPLETED under
           tcg,thread=multi and both generators carry that. Agreement here and
           disagreement one bitness away is not a contradiction, it is the rule:
           the value belongs to the guest, not to the family.

           **THE TWO FAILED DIFFERENTLY UNDER WHPX AND THAT IS THE PART WORTH
           KEEPING.** Vista x64 bugchecks inside WinPE before Setup writes a
           byte (STOP 0x0A, address 0x10 at IRQL 0xC). Windows 7 x64 clears
           WinPE, runs its ENTIRE first phase, writes 7.27 GB, and only then
           wedges at the first restart. For most of an afternoon that second
           reading looked like a disagreement with the first. It was a slower
           failure, which is exactly what the rule this project paid for on
           2026-09-10 exists to catch: AN ACCELERATOR MAY NOT BE WRITTEN DOWN
           UNTIL AN INSTALL HAS COMPLETED UNDER IT. Reaching a prompt, or a
           progress bar, or a whole finished phase, proves only that the guest
           has not failed YET.

           **AND A WEDGE DOES NOT ALWAYS LEAVE A RESUMABLE IMAGE.** "Switching
           to TCG costs no reinstall" holds where the guest already has a
           bootable disk - vm\vista.img wedged on a boot after a completed
           phase and resumed fine. Windows 7 x64 wedged AT the transition,
           before its boot files were written, and the TCG relaunch met
           "BOOTMGR is missing": that install had to be run again from the DVD.

           **AND ON THIS HOST THE WHPX
           OPTION SPACE IS ONE RUNG WIDE**: plain `-accel whpx` refuses to
           initialise at all ("Failed to enable nested virtualization,
           hr=80370302"), because the in-kernel irqchip wants nested
           virtualisation the host does not offer. So `kernel-irqchip=off` is
           not a tuning choice anywhere in this project - it is the only WHPX
           there is here, and when it wedges a guest the alternative is TCG
           rather than another WHPX rung.

WHAT THIS RECIPE OWES THAT NO EARLIER ONE DID. All four of these systems stage
a driver package into a driver store, and all four warn at install that an
unsigned package's publisher cannot be verified. Neither is a machine setting
and neither is configured here; they are what the guests are gone to for.

**AND THE ANSWER IS NOT THE SAME ON BOTH ARCHITECTURES, WHICH IS THE ONE THING
NOT TO CARRY ACROSS.** On the 32-bit pair, kernel-mode code signing enforcement
is x64-only, so a prompt and not a refusal is the expectation - written into
Phase 22 as the assumption to confirm on the guest, since it is the one that
would make that half of the phase pointless if wrong. On the 64-bit pair it IS
enforced, the cross-certificate route that once made third-party Windows 7 x64
signing possible is not available in practice, and an unsigned xhci98.sys
therefore loads only on a boot with signature enforcement disabled (F8), chosen
at the console on every boot - measured on both guests (roadmap tasks 21.8 and
22.5). Test-signing mode loads only a test-signed driver and this package is
not signed, so the owner removed that route on 2026-09-16 untried.
build-and-test.md, "Vista x64 and Windows 7 x64 target VMs", carries the F8
cost and, as a record, the host-side signing recipe proved out before either
guest existed.
#>

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

#
# The CPU models this recipe refuses, and WHICH FEATURE IS MISSING DEPENDS ON
# THE ARCHITECTURE even though the list and the default are the same.
#
# On a 32-bit guest it is the NX bit: Windows 7 Setup refuses a processor
# without one. On a 64-bit guest it is long mode: the guest never leaves the
# BIOS. pentium3 - the 32-bit XP guest's model, and the single most likely
# thing to survive a copy-paste into any of these launchers - fails both, so
# the same names are refused either way and only the sentence differs.
#
# Saying which feature is missing is not decoration. It is what stops the next
# reader "fixing" a 64-bit refusal by reaching for a model that has NX but no
# long mode, and refusing here is cheaper than reading either failure off a
# guest.
#
function Test-Nt6CpuModel {
    param([string]$Cpu, [string]$GuestName, [string]$Arch = "x86")
    if ($Cpu -match '^(486|pentium|athlon$|n270|kvm32|qemu32|coreduo)') {
        if ($Arch -eq "amd64") {
            throw "-Cpu $Cpu has no long mode; $GuestName needs an x86-64 model (qemu64, core2duo, ...)."
        }
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
        # x86 or amd64. It selects the CPU refusal's wording, which INF half
        # the run launcher tells the operator to install through, and - the
        # one that inverts rather than varies - what the launcher says about
        # code signing. See the header.
        [ValidateSet("x86", "amd64")][string]$Arch = "x86",
        [string]$Iso = "",
        [string]$VmDir = "",
        [string]$LocalScriptDir = "",
        [string]$DiskSize = "32G",
        [string]$QemuBinDir = "",
        [string]$XhciDevice = "qemu-xhci,p3=0",
        [string]$Cpu = "qemu64",
        [int]$MemoryMb = 2048,
        # vCPUs. Four, and the reason is TCG rather than any property of the
        # guest: Vista cannot use WHPX here (see the Accel note) and a
        # single-threaded TCG guest on a 2.0 GHz i7-9700T is painful. QEMU
        # emulates x86-on-x86 with multi-threaded TCG, so vCPUs become host
        # threads and the emulation actually parallelises. Four rather than
        # the host's eight leaves room for QEMU's own I/O and display threads,
        # which are what a starved MTTCG guest waits on.
        [int]$Smp = 4,
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

    Test-Nt6CpuModel -Cpu $Cpu -GuestName $GuestName -Arch $Arch

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

    # **thread=multi IS A TCG PROPERTY AND MUST NOT BE HANDED TO WHPX**, which
    # rejects the whole -accel argument rather than ignoring the part it does
    # not know. So it is derived here rather than written into either caller's
    # -Accel default, and asking for it explicitly still wins.
    $accelArg = $Accel
    if ($Smp -gt 1 -and $Accel -match '^tcg' -and $Accel -notmatch 'thread=') {
        $accelArg = $Accel + ",thread=multi"
    }

    Write-Step "Writing QEMU launchers"
    Write-Ok "Using xHCI device model: $XhciDevice"
    Write-Ok "Using CPU model: $Cpu; $MemoryMb MB; $Smp vCPU(s); accelerator $accelArg; monitor port $MonitorPort"

    # QEMU is resolved at RUN time by the launcher, not baked in here: the host
    # that generated a launcher is not always the host that runs it
    # (scripts\local is git-ignored and OneDrive-synced). One resolver for all
    # the generators, in common.ps1 (the 2026-09-07 audit's H28).
    $qemuResolve = Get-QemuLauncherResolver -FoundPath $qemuSystemCommand

    $accelComment = @()
    foreach ($line in $AccelNote) { $accelComment += ("rem " + $line) }

    #
    # The arch-dependent prose. Everything above this point is the machine and
    # is the same for all four guests; what follows is what each pair has to be
    # TOLD, and the code-signing block is the one that does not merely vary
    # between them - it inverts. Building both variants here rather than making
    # each caller supply its own keeps the two 32-bit callers and the two
    # 64-bit ones free of any duplicated text at all, which is the same H28
    # argument that put the machine here in the first place.
    #
    $isAmd64 = ($Arch -eq "amd64")

    # make-package.ps1's -Arch moves three things together and they are not
    # separable: the obj subdirectory, which of the two INFs is staged, and the
    # architecture both gates run under. Leaving it off a 64-bit guest's
    # launcher would stage the 32-bit package into the transfer drive, which
    # installs and then does not load, with nothing on the guest saying why.
    $packageCmd = if ($isAmd64) {
        "make-package.ps1 -Arch amd64 -Flavor qemu -OutDir vm\xfer$Stem"
    } else {
        "make-package.ps1 -Flavor qemu -OutDir vm\xfer$Stem"
    }

    if ($isAmd64) {
        $purposeComment = @(
            "rem One of the two guests of roadmap task 21.8: whether the amd64",
            "rem xhci98.sys - the SECOND binary, from the second toolchain, in the",
            "rem second package - can be made to load at all on a system that",
            "rem enforces kernel-mode code signing. Task 21.7 read this system's",
            "rem usbport.sys statically and found nothing in the interface against",
            "rem it; this guest is where the install-and-load questions are settled,",
            "rem and one of them may have no acceptable answer. A NEGATIVE CLOSES",
            "rem THE TASK: ""the driver cannot be loaded without disabling signature",
            "rem enforcement, and here is exactly what that costs"" is the answer to",
            "rem the question that was asked."
        )
        $cpuComment = @(
            "rem -cpu $Cpu, NOT the 32-bit XP guest's pentium3: that model has no",
            "rem long mode and this guest would never leave the BIOS. (On the 32-bit",
            "rem Vista and Windows 7 guests the same model is refused for a different",
            "rem missing feature, the NX bit; qemu64 satisfies both.)"
        )
        # The 8192 case is a REGENERATED launcher, so a launcher that has one
        # says so and a launcher that does not says what it would mean.
        if ($MemoryMb -gt 4096) {
            $memComment = @(
                "rem $MemoryMb MB - ABOVE 4 GB, AND THAT IS A DELIBERATE EXPERIMENT",
                "rem RATHER THAN A DEFAULT, and not on the roadmap (removed by the",
                "rem owner 2026-09-16). Every guest this project has booted before this one",
                "rem had less than 4 GB, so the HAL's double-buffering has never",
                "rem executed. IT DOES NOT TEST 64-BIT ADDRESSING: usbport's second",
                "rem IoGetDmaAdapter is gated on the miniport declaring Version >= 310",
                "rem and filling a packet slot this driver does not fill, so it is",
                "rem version-gated and no amount of memory opens it. What it DOES open",
                "rem is the SHAPE of the scatter-gather list this driver walks - read",
                "rem it off ProbeSgDisordered, ProbeSgGapped, ProbeSgHighDwords and",
                "rem ProbeSgMapped. Install at 2048 and raise afterwards; regenerate",
                "rem with -MemoryMb 2048 to go back."
            )
        } else {
            $memComment = @(
                "rem $MemoryMb MB, which is the INSTALL value for all four 6.x guests.",
                "rem Raising a 64-bit guest above 4 GB is a change to the test surface",
                "rem and not tuning - it is the first time the HAL's double-buffering",
                "rem could execute at all. That run is not planned (removed from the",
                "rem roadmap 2026-09-16); if it is ever taken, regenerate with a larger",
                "rem -MemoryMb AFTER the install, never before."
            )
        }
    } else {
        $purposeComment = @(
            "rem One of the two guests of roadmap Phase 22: whether the 32-bit",
            "rem xhci98.sys this project already ships installs, loads and works on",
            "rem Windows Vista and Windows 7 as it stands. Nothing is built for this",
            "rem guest - it is the binary on the .NTx86 half of the INF that exists."
        )
        $cpuComment = @(
            "rem -cpu $Cpu, NOT the 32-bit XP guest's pentium3: that model predates",
            "rem the NX bit and Windows 7 Setup refuses a processor without one. A",
            "rem 32-bit guest wanting a 64-bit-era CPU model is not the same thing as a",
            "rem 64-bit guest."
        )
        $memComment = @(
            "rem $MemoryMb MB, not 512, and a 32-bit guest, so nothing is above 4 GB."
        )
    }

    if ($isAmd64) {
        $infHalfComment = @(
            "rem    USB controller and the AMD64 package installs through the SECOND",
            "rem    INF's .NTamd64 half (src\xhci98-amd64.inf) from the transfer",
            "rem    drive - a second binary from a second toolchain, not the one the",
            "rem    32-bit guests take. p3=0 (USB 2.0 root ports only, as"
        )
        $ehciComment = @(
            "rem  - NO companion EHCI by default - and on this pair that is not the",
            "rem    reading it is on the NT 5.x guests. An xHCI-only XP or XP x64",
            "rem    install has no usbport.sys on disk at all (Code 39 on the first XP",
            "rem    boot, 2026-09-03), which is what the INF's LayoutFile route answers.",
            "rem    Vista and later apply a whole install image instead, and the 32-bit",
            "rem    guests of Phase 22 CONFIRMED on 2026-09-10 that all four Microsoft",
            "rem    USB files and usbui.dll are on disk after an install with no USB",
            "rem    host controller at all. The amd64 halves of both images carry the",
            "rem    same four files (read 2026-09-09, task 21.7), so the expectation",
            "rem    here is the same - take the reading anyway.",
            "rem    %2 = ehci adds the companion EHCI."
        )
        $signingComment = @(
            "rem  - AND WHAT NO FLAG HERE CONTROLS, WHICH ON THIS GUEST IS THE WHOLE",
            "rem    TASK: this system ENFORCES kernel-mode code signing. That is the",
            "rem    one thing that inverts between this launcher and its 32-bit",
            "rem    sibling, where enforcement is absent and an unsigned driver raises",
            "rem    a prompt and loads. Here an unsigned xhci98.sys does not load, and",
            "rem    the cross-certificate route that once made third-party Windows 7",
            "rem    x64 signing possible is not available in practice. ONE route is",
            "rem    left, measured on this guest (roadmap tasks 21.8 and 22.5):",
            "rem      F8 at boot -> Disable Driver Signature Enforcement. Costs a",
            "rem      choice at the console on EVERY BOOT and survives nothing.",
            "rem      QEMU's monitor sendkey f8 is ignored on this boot path, so be",
            "rem      at the keyboard before a system_reset.",
            "rem    Test-signing mode is NOT a route: it loads only a test-signed",
            "rem    driver, and this package is not signed. See build-and-test.md,",
            "rem    ""Vista x64 and Windows 7 x64 target VMs""."
        )
    } else {
        $infHalfComment = @(
            "rem    USB controller and the package installs through the INF's .NTx86",
            "rem    half from the transfer drive. p3=0 (USB 2.0 root ports only, as"
        )
        $ehciComment = @(
            "rem  - NO companion EHCI by default - and on this pair that is not the",
            "rem    reading it is on the NT 5.x guests. An xHCI-only XP or XP x64",
            "rem    install has no usbport.sys on disk at all (Code 39 on the first XP",
            "rem    boot, 2026-09-03), which is what the INF's LayoutFile route answers.",
            "rem    Vista and later apply a whole install image instead, so all four",
            "rem    Microsoft USB files should already be in System32\drivers whatever",
            "rem    controllers the machine has - read off the MEDIA on 2026-09-09, and",
            "rem    roadmap task 22.3 is that same reading taken off an installed guest.",
            "rem    %2 = ehci adds the companion EHCI."
        )
        $signingComment = @(
            "rem  - AND WHAT NO FLAG HERE CONTROLS: this system stages the package into",
            "rem    a driver store, and warns that an unsigned package's publisher",
            "rem    cannot be verified. Kernel-mode code signing enforcement is",
            "rem    x64-only, so a prompt and not a refusal is the expectation - and it",
            "rem    is written into roadmap Phase 22 as the assumption to CONFIRM on",
            "rem    this guest, since it is the one that would make the phase pointless",
            "rem    if wrong. Record what the prompt actually did."
        )
    }

    $installCmd = Join-Path $LocalScriptDir ("qemu-" + $Stem + "-install.cmd")
    Write-AsciiFile $installCmd (@(
        "@echo off",
        "rem $GuestName guest - install launcher.",
        "rem Generated by scripts\$GeneratorName; regenerate rather than edit.",
        "rem"
    ) + $purposeComment + @(
        "rem"
    ) + $cpuComment + $memComment + @(
        "rem $Smp vCPUs. Under TCG that is what makes the guest usable: QEMU",
        "rem emulates x86-on-x86 with multi-threaded TCG, so vCPUs become host",
        "rem threads. Four rather than the host's core count leaves room for",
        "rem QEMU's own I/O and display threads. THE HAL IS FIXED AT INSTALL",
        "rem TIME, so this must not be changed between installing a guest and",
        "rem running it; regenerate both launchers with -Smp instead.",
        "rem",
        "rem Accelerator: -accel $accelArg."
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
        "  -accel $accelArg ^",
        "  -cpu $Cpu ^",
        "  -smp $Smp ^",
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
        "rem Same machine as qemu-$Stem-install.cmd (ACPI on, -accel $accelArg,",
        "rem -cpu $Cpu, $Smp vCPUs, $MemoryMb MB, -vga std) plus:",
        "rem  - the xHCI ($($XhciDevice)): there is no in-box driver for",
        "rem    PCI\CC_0C0330 on 6.0 or 6.1 either, so it shows as an unrecognised"
    ) + $infHalfComment + @(
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
        "rem    0xE9 trace: $packageCmd.",
        "rem  - isa-debugcon at 0xE9 -> vm\$Stem-debugcon.log, rotated like the",
        "rem    other guests' logs so a stale DriverEntry cannot be read as this",
        "rem    boot's.",
        "rem  - the QEMU xhci trace events of scripts\local\xhci-trace-events.txt,",
        "rem    when that file exists."
    ) + $ehciComment + $signingComment + @(
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
        "  -accel $accelArg ^",
        "  -cpu $Cpu ^",
        "  -smp $Smp ^",
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
    if ($isAmd64) {
        Write-Host "     The accelerator above is MEASURED (2026-09-10): both 64-bit guests installed under tcg."
        Write-Host "     Under WHPX Vista x64 bugchecks in WinPE and Windows 7 x64 wedges at its first restart -"
        Write-Host "     after a whole finished phase, which is why only a COMPLETED install may be recorded."
        Write-Host "  3. This system enforces kernel-mode code signing and the package is unsigned, so the driver loads"
        Write-Host "     only on an F8 boot: Disable Driver Signature Enforcement, chosen at the console on EVERY boot"
        Write-Host "     (measured, roadmap tasks 21.8 and 22.5). Test-signing mode is not a route for an unsigned package."
        Write-Host "  4. Then the amd64 package: make-package.ps1 -Arch amd64 -Flavor qemu -OutDir vm\xfer$Stem, boot"
        Write-Host "     qemu-$Stem-run.cmd <tag>, and install through the second INF's .NTamd64 half."
        Write-Host "     Then task 21.5's clauses, on the release flavour as well as qemu."
    } else {
        Write-Host "  3. Roadmap task 22.3's two readings, neither needing a driver and neither needing a boot:"
        Write-Host "     7z l vm\$Stem.img, then look for usbport.sys / usbhub.sys / usbd.sys / usbehci.sys in"
        Write-Host "     Windows\System32\drivers and in Windows\System32\DriverStore\FileRepository."
        Write-Host "  4. Stage the package: make-package.ps1 -Flavor qemu -OutDir vm\xfer$Stem, then boot"
        Write-Host "     qemu-$Stem-run.cmd <tag> and install from the transfer drive."
        Write-Host "     RECORD WHAT THE UNSIGNED-DRIVER PROMPT ACTUALLY DID (task 22.4)."
    }
}
