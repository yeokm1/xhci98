<#
.SYNOPSIS
Regression tests for the Win98, Win2000, Windows XP, xHCI-only Win2000, Windows XP x64, and Windows Vista and Windows 7 (both architectures) QEMU launcher generators.

.DESCRIPTION
Generates launchers against stand-in QEMU executable files. QEMU is never
started; the test verifies that each run launcher archives the prior debug
console log and gives each boot a fresh file, so stale DriverEntry lines cannot
be attributed to a replacement driver.

The rotation rule is asserted by *running* the launcher's rotation preamble -
everything ahead of the QEMU command line - against real files, because the
interesting case is the one string matching cannot see: an EMPTY current log
must be left alone. A launch that dies before QEMU writes anything leaves a
zero-byte log behind, and rotating that unconditionally would push the last
real trace out of <target>-debugcon.previous.log and replace it with nothing.

All ten VMs are covered because the phases close on comparisons between them:
the traces must land in separate files, and each must belong to one boot.
(The Windows XP guest is the fourth and the xHCI-only Windows 2000 guest the
fifth, both since roadmap Phase 19; the Windows XP x64 guest is the sixth,
since roadmap Phase 21; the 32-bit Windows Vista and Windows 7 guests are the
seventh and eighth, since roadmap Phase 22; and their 64-bit counterparts are
the ninth and tenth, since roadmap task 21.8. All four of the last group share
one generator body in scripts\qemu-nt6-common.ps1 rather than carrying four
copies of it, with -Arch selecting the parts that differ.)
#>

[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$targets = @(
    @{ Name = "Win98";  Setup = "setup-qemu.ps1";          Launcher = "qemu-win98-run.cmd"; LogBase = "win98-debugcon" },
    @{ Name = "Win2000"; Setup = "setup-qemu-win2k.ps1";   Launcher = "qemu-win2k-run.cmd"; LogBase = "win2k-debugcon" },
    @{ Name = "Win2000SMP"; Setup = "setup-qemu-win2k-smp.ps1"; Launcher = "qemu-win2k-smp-run.cmd"; LogBase = "win2k-smp-debugcon" },
    @{ Name = "WinXP";  Setup = "setup-qemu-winxp.ps1";    Launcher = "qemu-winxp-run.cmd"; LogBase = "winxp-debugcon" },
    @{ Name = "Win2000XOnly"; Setup = "setup-qemu-win2k-xonly.ps1"; Launcher = "qemu-win2k-xonly-run.cmd"; LogBase = "win2k-xonly-debugcon" },
    @{ Name = "WinXP64"; Setup = "setup-qemu-winxp64.ps1"; Launcher = "qemu-winxp64-run.cmd"; LogBase = "winxp64-debugcon" },
    @{ Name = "Vista"; Setup = "setup-qemu-vista.ps1"; Launcher = "qemu-vista-run.cmd"; LogBase = "vista-debugcon" },
    @{ Name = "Win7";  Setup = "setup-qemu-win7.ps1";  Launcher = "qemu-win7-run.cmd";  LogBase = "win7-debugcon" },
    # The 64-bit half of the same recipe (roadmap task 21.8). These two carried
    # an -Accel field here until 2026-09-10, because their generators refused to
    # run without one and nothing had been measured for either. Both installs
    # have since COMPLETED under tcg,thread=multi, so the field is gone on
    # purpose: passing no -Accel is what makes these rows exercise the
    # generators' own measured defaults rather than a value this file supplies.
    @{ Name = "VistaX64"; Setup = "setup-qemu-vista-x64.ps1"; Launcher = "qemu-vista-x64-run.cmd"; LogBase = "vista-x64-debugcon" },
    @{ Name = "Win7X64";  Setup = "setup-qemu-win7-x64.ps1";  Launcher = "qemu-win7-x64-run.cmd";  LogBase = "win7-x64-debugcon" }
)
$work = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("xhci98-qemu-launcher-test-" + [System.IO.Path]::GetRandomFileName())
. (Join-Path $PSScriptRoot "test-harness.ps1")

try {
    $bin = Join-Path $work "bin"
    $vm = Join-Path $work "vm"
    $launchers = Join-Path $work "launchers"
    New-Item -ItemType Directory -Path $bin | Out-Null
    New-Item -ItemType Directory -Path $vm | Out-Null
    New-Item -ItemType Directory -Path $launchers | Out-Null
    Set-Content -LiteralPath (Join-Path $bin "qemu-system-x86_64.exe") `
        -Value "stand-in" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $bin "qemu-img.exe") `
        -Value "stand-in" -Encoding ASCII

    $logPaths = @()
    foreach ($target in $targets) {
        $name = $target.Name
        $extra = @{}
        if ($target.ContainsKey("Accel")) { $extra["Accel"] = $target.Accel }
        & (Join-Path $PSScriptRoot $target.Setup) -VmDir $vm -LocalScriptDir $launchers `
            -QemuBinDir $bin @extra | Out-Null

        $run = Join-Path $launchers $target.Launcher
        Assert-True (Test-Path -LiteralPath $run) "the $name run launcher was not generated."
        $text = [System.IO.File]::ReadAllText($run)
        $current = Join-Path $vm ($target.LogBase + ".log")
        $previous = Join-Path $vm ($target.LogBase + ".previous.log")
        $logPaths += $current
        $moveLine = "move /y `"$current`" `"$previous`" >nul"
        $chardevLine = "-chardev file,id=dbgcon,path=`"$current`""

        Assert-True ($text.Contains($moveLine)) `
            "the $name launcher does not archive the prior debug-console log."
        Assert-True ($text.Contains($chardevLine)) `
            "the $name launcher does not capture this boot's debug-console output."
        Assert-True ($text.Contains("-device isa-debugcon,iobase=0xe9,chardev=dbgcon")) `
            "the $name launcher does not attach the port-0xE9 trace device."
        Assert-True (-not $text.Contains("append=on")) `
            "the $name launcher still appends stale output instead of creating a per-boot trace."
        Assert-True ($text.IndexOf($moveLine) -lt $text.IndexOf($chardevLine)) `
            "the $name launcher does not archive the prior log before QEMU opens the new trace."
        if ($name -eq "Win98" -or $name -eq "Win2000") {
            # build-and-test.md, "A floppy can be inserted live without a
            # reboot": `change floppy0 <path>` on the monitor is the
            # guest-to-host courier for 2a and 2b, and a floppy CONTROLLER
            # cannot be added to a running guest - so it has to be on the run
            # launcher at boot. The 2026-09-07 audit's H27 was that the doc had
            # claimed this since batch 13-L with no generator writing it; its
            # first fix then put the drive on 2b's PREPARATION launcher, which
            # is booted once and never during a matrix run, so the claim stayed
            # false for the launcher the doc is about. This asserts the
            # generated text, which is the only thing that survives a
            # regenerated `scripts\local\`.
            Assert-True ($text -match '(?m)^\s*-drive if=floppy') `
                "the $name run launcher has no floppy controller, so a file cannot be couriered out of a running guest."
        }
        if ($name -eq "Win2000") {
            # ...and EMPTY on 2b, because `vm\transfer.img` is one file that
            # every guest shares and the Windows 98 run launcher mounts it
            # WRITABLE. Two guests up at once with the same raw floppy image
            # is a FAT volume with two writers and no arbitration. The drive is
            # what has to exist at boot; the disk is chosen later, per guest,
            # with `change floppy0`.
            Assert-True ($text -notmatch '(?m)^\s*-drive if=floppy,file=') `
                "the Windows 2000 run launcher mounts a floppy image at boot; vm\transfer.img is shared with the Windows 98 guest, which mounts it writable, so booting both would hand one image to two writers."
        }
        if ($name -eq "Win2000SMP") {
            Assert-True ($text.Contains("-accel whpx,kernel-irqchip=off")) `
                "the Win2000 SMP launcher does not default to the proven WHPX rung."
        }
        if ($name -eq "WinXP") {
            # build-and-test.md, "Windows XP target VM": WHPX not TCG, ACPI on,
            # and no companion EHCI unless asked for, because the reading the
            # guest exists to take is an xHCI-only install (roadmap task 19.4).
            Assert-True ($text.Contains("-accel whpx,kernel-irqchip=off") -and $text.Contains("-machine pc ^")) `
                "the Windows XP launcher does not use WHPX with ACPI on."
            Assert-True ($text.Contains('set "EHCI="') -and $text.Contains('if /i "%2"=="ehci"') -and
                -not ($text -match '(?m)^set "EHCI=-device')) `
                "the Windows XP launcher does not leave the companion EHCI out by default."
            Assert-True ($text.Contains('if not exist "%WINXP_ISO%"') -and $text.Contains('set "CDROM="')) `
                "the Windows XP launcher does not boot without the CD when the ISO is absent."
            # Roadmap task 19.3: QEMU pins a SuperSpeed-capable device to a
            # SuperSpeed-capable root port and never falls it back to USB 2.0,
            # so the storage and audio readings need USB 2.0-only root ports
            # and an audio backend for the hot-plugged usb-audio.
            Assert-True ($text.Contains("-device qemu-xhci,p3=0,id=xhci ^")) `
                "the Windows XP launcher does not keep every root port USB 2.0 (p3=0)."
            Assert-True ($text.Contains("-audiodev none,id=xpaud ^")) `
                "the Windows XP launcher declares no audio backend for a hot-plugged usb-audio."
        }
        if ($name -eq "Win2000XOnly") {
            # build-and-test.md, "Windows 2000 xHCI-only VM": the 2b recipe's
            # TCG Standard-PC flags on both launchers (the HAL is fixed at
            # install time), no USB controller at install, the xHCI alone at
            # run unless asked otherwise, and USB 2.0-only root ports (roadmap
            # task 19.5).
            Assert-True ($text.Contains("-machine pc,acpi=off ^") -and $text.Contains("-cpu pentium3,-apic ^") -and
                -not $text.Contains("-accel")) `
                "the xHCI-only Windows 2000 run launcher does not carry the 2b recipe's TCG Standard-PC flags."
            $install = [System.IO.File]::ReadAllText((Join-Path $launchers "qemu-win2k-xonly-install.cmd"))
            Assert-True ($install.Contains("-machine pc,acpi=off ^") -and $install.Contains("-cpu pentium3,-apic ^") -and
                -not $install.Contains(" -device ")) `
                "the xHCI-only Windows 2000 install launcher drifted from the run launcher's HAL flags, or attaches a USB controller."
            Assert-True ($text.Contains('set "CTRL=-device qemu-xhci,p3=0,id=xhci"') -and
                $text.Contains('if /i "%2"=="none" set "CTRL="') -and $text.Contains('if /i "%2"=="ehci"') -and
                -not ($text -match '(?m)^set "CTRL=.*usb-ehci')) `
                "the xHCI-only Windows 2000 run launcher does not attach the xHCI alone by default, with none and ehci as the alternatives."
            Assert-True ($text.Contains('if not exist "%WIN2K_ISO%"') -and $text.Contains('set "CDROM="')) `
                "the xHCI-only Windows 2000 run launcher does not boot without the CD when the ISO is absent."
        }
        if ($name -eq "WinXP64") {
            # build-and-test.md, "Windows XP x64 target VM". Same WHPX/ACPI
            # machine and same xHCI-only default as the 32-bit XP guest, and
            # then the two flags that CANNOT be copied from it.
            # **TCG is this guest's default and not a fallback**, which is the
            # reverse of every other guest here and was measured on
            # 2026-09-08: under whpx,kernel-irqchip=off XP x64 Setup wedges on
            # "Setup is starting Windows" with RIP pinned, and the same command
            # line under tcg reaches "Setup is copying files". Asserted because
            # a well-meaning edit towards "the proven WHPX rung" - which is
            # right for the 32-bit guests and written all over this repository
            # - would silently produce a guest that cannot install.
            Assert-True ($text.Contains("-accel tcg ^") -and $text.Contains("-machine pc ^")) `
                "the Windows XP x64 launcher does not use TCG with ACPI on; WHPX wedges XP x64 Setup on this host family."
            # **NO LONG MODE, NO GUEST.** pentium3 is the 32-bit XP launcher's
            # model and the single most likely thing to survive a copy-paste
            # into this one; an x64 Setup on it does not boot. The generator
            # refuses such a -Cpu outright, and this asserts the generated
            # text, which is the only thing that survives a regenerated
            # scripts\local.
            Assert-True (-not ($text -match '(?m)^\s*-cpu (pentium|486|coreduo|athlon-|n270)')) `
                "the Windows XP x64 launcher names a CPU model with no long mode; XP x64 Setup will not boot on it."
            Assert-True ($text -match '(?m)^\s*-cpu qemu64 \^') `
                "the Windows XP x64 launcher does not default to the qemu64 x86-64 model."
            # 512 MB is the 32-bit figure and the other copy-paste casualty.
            # This is comfort, not correctness: measurement M5 read the NT 5.2
            # amd64 usbport creating its DMA adapter 32-bit, so nothing lands
            # above 4 GB whatever the guest has.
            Assert-True ($text -match '(?m)^\s*-m 2048 \^') `
                "the Windows XP x64 launcher does not give the guest the 2048 MB the x64 recipe calls for."
            Assert-True ($text.Contains('set "EHCI="') -and $text.Contains('if /i "%2"=="ehci"') -and
                -not ($text -match '(?m)^set "EHCI=-device')) `
                "the Windows XP x64 launcher does not leave the companion EHCI out by default, so the xHCI-only usbport.sys reading cannot be taken."
            Assert-True ($text.Contains('if not exist "%WINXP64_ISO%"') -and $text.Contains('set "CDROM="')) `
                "the Windows XP x64 launcher does not boot without the CD when the ISO is absent."
            Assert-True ($text.Contains("-device qemu-xhci,p3=0,id=xhci ^")) `
                "the Windows XP x64 launcher does not keep every root port USB 2.0 (p3=0)."
            Assert-True ($text.Contains("-audiodev none,id=xp64aud ^")) `
                "the Windows XP x64 launcher declares no audio backend for a hot-plugged usb-audio."
            $install64 = [System.IO.File]::ReadAllText((Join-Path $launchers "qemu-winxp64-install.cmd"))
            Assert-True ($install64 -match '(?m)^\s*-cpu qemu64 \^' -and $install64 -match '(?m)^\s*-m 2048 \^' -and
                $install64.Contains("-boot d ^") -and -not $install64.Contains(" -device ")) `
                "the Windows XP x64 install launcher drifted from the run launcher's CPU or memory, lost -boot d, or attaches a USB controller."
            # The HAL is fixed at INSTALL time, so a guest installed under one
            # accelerator must be booted under it too - the 2b lesson, asserted
            # here across the pair the way it already is for the SMP rungs.
            Assert-True ($install64.Contains("-accel tcg ^")) `
                "the Windows XP x64 install and run launchers disagree on the accelerator; the HAL is fixed at install time, so the installed system would not boot the way it was installed."
        }

        if ($name -eq "Vista" -or $name -eq "Win7" -or $name -eq "VistaX64" -or $name -eq "Win7X64") {
            # build-and-test.md, "Windows Vista and Windows 7 target VMs" and
            # "Windows Vista x64 and Windows 7 x64 target VMs". All FOUR are
            # one recipe with one body (scripts\qemu-nt6-common.ps1), so what
            # is asserted here is asserted for all of them - and four guests
            # out of one recipe are exactly what would drift if it were ever
            # copied into four.
            $isAmd64 = ($name -eq "VistaX64" -or $name -eq "Win7X64")
            $stem = switch ($name) {
                "Vista"    { "vista" }
                "Win7"     { "win7" }
                "VistaX64" { "vista-x64" }
                "Win7X64"  { "win7-x64" }
            }
            $isoVar = switch ($name) {
                "Vista"    { "VISTA_ISO" }
                "Win7"     { "WIN7_ISO" }
                "VistaX64" { "VISTA_X64_ISO" }
                "Win7X64"  { "WIN7_X64_ISO" }
            }
            # **NO NX BIT, NO GUEST.** pentium3 is the 32-bit XP launcher's
            # model, it is the single most likely thing to survive a
            # copy-paste into a 32-bit guest's launcher, and Windows 7 Setup
            # refuses a processor without an NX bit. Asserted on the generated
            # text, which is the only thing that survives a regenerated
            # scripts\local.
            Assert-True (-not ($text -match '(?m)^\s*-cpu (486|pentium|athlon |n270|kvm32|qemu32|coreduo)')) `
                "the $name launcher names a CPU model with no NX bit; Windows 7 Setup refuses one."
            Assert-True ($text -match '(?m)^\s*-cpu qemu64 \^') `
                "the $name launcher does not default to the qemu64 CPU model the 6.x recipe calls for."
            # 512 MB is the 32-bit XP figure and the other copy-paste
            # casualty; Windows 7's own floor is 1 GB.
            Assert-True ($text -match '(?m)^\s*-m 2048 \^') `
                "the $name launcher does not give the guest the 2048 MB the 6.x recipe calls for."
            # **THE ACCELERATOR IS PER GUEST, AND ON THIS PAIR THE TWO GUESTS
            # DISAGREE.** Roadmap task 22.4 asks for it to be probed per host
            # AND per guest, and this pair is now the third case that pays for
            # the rule: Vista wants tcg, for the same reason the XP x64 guest
            # does. Under whpx,kernel-irqchip=off Vista Setup ran its whole
            # first phase and then wedged on the boot after it (2026-09-10:
            # EIP pinned, disk idle twenty-two minutes, marquee still
            # animating); under tcg the same image resumed and reached the
            # desktop. Windows 7 was then installed under
            # whpx,kernel-irqchip=off and ran the whole way through to a
            # finished desktop, so BOTH values are confirmed through a
            # completed install rather than a language page - and the pair
            # disagrees, which is why neither may be copied to the other.
            # thread=multi rides along on a tcg accelerator when the guest has
            # more than one vCPU, and MUST NOT reach WHPX - it rejects the
            # whole -accel argument rather than ignoring an option it does not
            # know, so a launcher that carried it would not start at all.
            #
            # **AND THE 64-BIT PAIR AGREE, MEASURED 2026-09-10: BOTH WANT TCG.**
            # Their rows pass no -Accel, so these values assert the generators'
            # own defaults. Vista x64 bugchecks inside WinPE under WHPX (STOP
            # 0x0A) before Setup writes a byte; Windows 7 x64 gets through its
            # entire first phase and 7.27 GB on that rung and wedges at the
            # first restart, which for most of an afternoon read like a
            # disagreement with its sibling and was only a slower failure.
            # Both defaults rest on an install that reached a desktop.
            #
            # Agreement across this pair and disagreement across the 32-bit pair
            # is not an inconsistency to tidy up. It is the rule stated twice:
            # the accelerator belongs to the guest, never to the family.
            #
            $expectAccel = switch ($name) {
                "Vista"    { "tcg,thread=multi" }
                "Win7"     { "whpx,kernel-irqchip=off" }
                "VistaX64" { "tcg,thread=multi" }
                "Win7X64"  { "tcg,thread=multi" }
            }
            Assert-True ($text.Contains("-accel $expectAccel ^") -and $text.Contains("-machine pc ^")) `
                "the $name launcher does not carry its accelerator ($expectAccel) with ACPI on."
            Assert-True (-not ($text -match '-accel whpx[^ ]*thread=')) `
                "the $name launcher hands thread= to WHPX, which refuses the whole -accel argument and will not start."
            # Four vCPUs, which is what makes a TCG guest usable here. Asserted
            # on BOTH launchers for the same reason the accelerator is: the HAL
            # is fixed at install time.
            Assert-True ($text -match '(?m)^\s*-smp 4 \^') `
                "the $name launcher does not give the guest the 4 vCPUs the 6.x recipe calls for."
            Assert-True ($text.Contains('set "EHCI="') -and $text.Contains('if /i "%2"=="ehci"') -and
                -not ($text -match '(?m)^set "EHCI=-device')) `
                "the $name launcher does not leave the companion EHCI out by default, so task 22.3's xHCI-only reading cannot be taken."
            Assert-True ($text.Contains("if not exist `"%$isoVar%`"") -and $text.Contains('set "CDROM="')) `
                "the $name launcher does not boot without the DVD when the ISO is absent."
            Assert-True ($text.Contains("-device qemu-xhci,p3=0,id=xhci ^")) `
                "the $name launcher does not keep every root port USB 2.0 (p3=0)."
            Assert-True ($text.Contains("-audiodev none,id=${stem}aud ^")) `
                "the $name launcher declares no audio backend for a hot-plugged usb-audio."
            $install6 = [System.IO.File]::ReadAllText((Join-Path $launchers "qemu-$stem-install.cmd"))
            Assert-True ($install6 -match '(?m)^\s*-cpu qemu64 \^' -and $install6 -match '(?m)^\s*-m 2048 \^' -and
                $install6.Contains("-boot d ^") -and -not $install6.Contains(" -device ")) `
                "the $name install launcher drifted from the run launcher's CPU or memory, lost -boot d, or attaches a USB controller."
            # The HAL is fixed at INSTALL time, so a guest installed under one
            # accelerator must be booted under it too - the 2b lesson,
            # asserted across the pair as it already is for the SMP rungs and
            # for XP x64.
            Assert-True ($install6.Contains("-accel $expectAccel ^")) `
                "the $name install and run launchers disagree on the accelerator; the HAL is fixed at install time, so the installed system would not boot the way it was installed."
            Assert-True ($install6 -match '(?m)^\s*-smp 4 \^') `
                "the $name install and run launchers disagree on the vCPU count; the HAL is fixed at install time, so the installed system would not boot the way it was installed."

            #
            # **THE ONE THING THAT INVERTS BETWEEN THE TWO ARCHITECTURES, AND
            # THE ONLY ONE THAT WOULD MISLEAD AN OPERATOR RATHER THAN BREAK A
            # BOOT.** On the 32-bit pair, kernel-mode code signing enforcement
            # is x64-only, so an unsigned driver raises a prompt and loads,
            # and the launcher says to record what the prompt did. On the
            # 64-bit pair it is enforced, an unsigned driver does not load at
            # all, and the launcher has to name the two routes that exist and
            # say to settle them first.
            #
            # Every other check in this file guards against a launcher that
            # will not work. This one guards against a launcher that works and
            # tells the operator the wrong thing - which is worse, because a
            # guest that boots and then refuses the driver with no explanation
            # is exactly the shape of a day lost. The shared body builds both
            # variants from -Arch, so the failure mode is one caller passing
            # the wrong arch, and nothing else in the generated text would
            # show it.
            #
            if ($isAmd64) {
                Assert-True ($text.Contains("this system ENFORCES kernel-mode code signing") -and
                    $text.Contains("bcdedit -set TESTSIGNING ON") -and
                    $text.Contains("Disable Driver Signature Enforcement")) `
                    "the $name run launcher does not tell the operator that this system enforces kernel-mode code signing, or does not name both routes around it; an unsigned driver does not load here and roadmap task 21.8 is which route works."
                Assert-True (-not $text.Contains("Kernel-mode code signing enforcement is")) `
                    "the $name run launcher carries the 32-bit pair's 'enforcement is x64-only' sentence, which is false on this guest and would send the operator looking for a prompt that never comes."
                Assert-True ($text.Contains(".NTamd64 half") -and -not $text.Contains(".NTx86")) `
                    "the $name run launcher names the wrong INF half; the 64-bit guests install the second package through src\xhci98-amd64.inf."
                # make-package.ps1 -Arch moves the obj subdirectory, the INF
                # and both gates' architecture together. Without it the
                # transfer drive gets the 32-bit package, which installs on
                # this guest and then does not load, with nothing on the
                # guest saying why.
                Assert-True ($text.Contains("make-package.ps1 -Arch amd64 -Flavor qemu -OutDir vm\xfer$stem")) `
                    "the $name run launcher's staging command omits -Arch amd64, so it would put the 32-bit package on a 64-bit guest's transfer drive."
            } else {
                Assert-True ($text.Contains("Kernel-mode code signing enforcement is") -and
                    $text.Contains("x64-only, so a prompt and not a refusal is the expectation")) `
                    "the $name run launcher lost the sentence saying enforcement is x64-only here, which is the assumption roadmap Phase 22 sends the operator to confirm."
                Assert-True ($text.Contains(".NTx86") -and -not $text.Contains(".NTamd64")) `
                    "the $name run launcher names the wrong INF half; the 32-bit guests install through src\xhci98.inf's .NTx86 half."
            }
        }

        # --- the rotation preamble, actually executed -----------------------
        #
        # The QEMU command line is the first line that starts with a quote (the
        # quoted path to qemu-system-x86_64.exe); everything before it is the
        # rotation logic and its comments.
        $lines = $text -split "`r?`n"
        $qemuAt = -1
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i].StartsWith('"')) { $qemuAt = $i; break }
        }
        Assert-True ($qemuAt -gt 0) `
            "could not find the QEMU command line in the $name run launcher."

        $preamble = Join-Path $vm ("rotate-only-" + $name + ".cmd")
        [System.IO.File]::WriteAllText($preamble,
            ((@($lines[0..($qemuAt - 1)]) + @("exit /b 0")) -join "`r`n"),
            (New-Object System.Text.ASCIIEncoding))

        # No log yet: nothing to archive, and nothing may be invented.
        $preambleOut = (& cmd.exe /c $preamble) -join "`n"
        Assert-True ($LASTEXITCODE -eq 0) "$name rotation failed when there was no prior log."

        # --- the launcher says which QEMU it resolved, and from which rung ---
        #
        # The resolver falls through rather than failing, which is what makes a
        # launcher portable and also what let this host swap the emulator under
        # two installed guests in silence (common.ps1, Get-QemuLauncherResolver).
        # THIS GATE CANNOT CATCH THAT - it reads generated text and must not
        # depend on the host - so what it can hold is that the launcher SAYS it,
        # and these assertions read the preamble's real output rather than its
        # source, because an echo that does not survive cmd's parsing is worth
        # nothing.
        Assert-True ($preambleOut -match "(?m)^QEMU: resolved from .+\.$") `
            "the $name launcher does not say which rung of the resolver found QEMU, so a fall-through to a different emulator would be silent."
        Assert-True ($preambleOut.Contains("QEMU: " + (Join-Path $bin "qemu-system-x86_64.exe"))) `
            "the $name launcher does not echo the QEMU path it resolved."
        Assert-True ($preambleOut.Contains("resolved from where this launcher was generated")) `
            "the $name launcher did not resolve QEMU from the path its generator found, so the rung it names is wrong."

        # The stand-in is not a runnable image, which is the degrade path: the
        # version read must leave the launcher working and say it could not read
        # one, never abort the boot.
        Assert-True ($preambleOut.Contains("version unreadable")) `
            "the $name launcher did not degrade gracefully when the resolved QEMU could not report a version."
        Assert-True (-not (Test-Path -LiteralPath $previous)) `
            "an archive was created for $name when there was no prior log to archive."

        # A real trace is archived, and this boot starts from a clean slate.
        [System.IO.File]::WriteAllText($current, "TRACE-1")
        $null = & cmd.exe /c $preamble
        Assert-True ($LASTEXITCODE -eq 0) "$name rotation failed on a non-empty prior log."
        Assert-True (-not (Test-Path -LiteralPath $current)) `
            "the prior $name log was left in place; this boot would append to another boot's trace."
        Assert-True ((Test-Path -LiteralPath $previous) -and
            ([System.IO.File]::ReadAllText($previous) -eq "TRACE-1")) `
            "the prior $name trace was not archived intact."

        # The case that matters: a launch that died before QEMU wrote anything
        # leaves a zero-byte log. Rotating it would destroy TRACE-1.
        [System.IO.File]::WriteAllText($current, "")
        $null = & cmd.exe /c $preamble
        Assert-True ($LASTEXITCODE -eq 0) "$name rotation failed on an empty prior log."
        Assert-True ((Test-Path -LiteralPath $previous) -and
            ([System.IO.File]::ReadAllText($previous) -eq "TRACE-1")) `
            "an empty $name log was rotated over the archive, destroying the last real trace."
    }

    # The two targets are compared against each other in the Phase 3 gate, so
    # neither may write into the other's trace.
    Assert-True (($logPaths | Select-Object -Unique).Count -eq $targets.Count) `
        "the targets do not each get their own debug-console log."

    # **The x64 generator must REFUSE a CPU model with no long mode**, rather
    # than write a launcher that cannot boot. The 32-bit XP recipe is what the
    # x64 one is modelled on and -cpu pentium3 is the one line of it that is
    # actively wrong, so the refusal is the guard against the copy that looks
    # right. Asserted by asking for it: a generator that quietly accepted it
    # would write a launcher here and the throw would never happen.
    $badCpuDir = Join-Path $work "launchers-xp64-badcpu"
    New-Item -ItemType Directory -Path $badCpuDir | Out-Null
    $refused = $false
    try {
        & (Join-Path $PSScriptRoot "setup-qemu-winxp64.ps1") -VmDir $vm `
            -LocalScriptDir $badCpuDir -QemuBinDir $bin -Cpu "pentium3" | Out-Null
    } catch {
        $refused = $true
    }
    Assert-True $refused `
        "setup-qemu-winxp64.ps1 accepted -Cpu pentium3, which has no long mode; the launcher it wrote would never boot XP x64 Setup."
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $badCpuDir "qemu-winxp64-run.cmd"))) `
        "setup-qemu-winxp64.ps1 wrote a launcher for a CPU model with no long mode before refusing it."

    # **And the same refusal on the 6.x pair, for a different missing feature.**
    # These two are 32-bit guests, so nothing about long mode protects them and
    # -cpu pentium3 is not merely plausible here - it is the 32-bit XP recipe's
    # own value, one line away in the same directory. Windows 7 Setup refuses a
    # processor with no NX bit, so the generator must refuse the model rather
    # than write a launcher that installs nothing. Asserted by asking for it on
    # both, because the shared body is what refuses and a caller could bypass
    # it.
    # **AND THE SAME REFUSAL ON ALL FOUR OF THE 6.x GUESTS, FOR TWO DIFFERENT
    # MISSING FEATURES.** The 32-bit pair need an NX bit, which Windows 7 Setup
    # requires; the 64-bit pair need long mode. pentium3 lacks both, it is the
    # 32-bit XP recipe's own value one file away in the same directory, and
    # -cpu is the line most likely to be copied. The generator must refuse the
    # model rather than write a launcher that installs nothing - and it must
    # name the feature THAT guest is missing, so the next reader does not
    # "fix" a 64-bit refusal by reaching for a model with NX and no long mode.
    # Asserted by asking for it on each, because the shared body is what
    # refuses and a caller could bypass it.
    foreach ($nt6 in @(
        @{ Setup = "setup-qemu-vista.ps1";     Run = "qemu-vista-run.cmd";     Missing = "no NX bit" },
        @{ Setup = "setup-qemu-win7.ps1";      Run = "qemu-win7-run.cmd";      Missing = "no NX bit" },
        @{ Setup = "setup-qemu-vista-x64.ps1"; Run = "qemu-vista-x64-run.cmd"; Missing = "no long mode" },
        @{ Setup = "setup-qemu-win7-x64.ps1";  Run = "qemu-win7-x64-run.cmd";  Missing = "no long mode" }
    )) {
        $badNxDir = Join-Path $work ("launchers-badcpu-" + [System.IO.Path]::GetFileNameWithoutExtension($nt6.Setup))
        New-Item -ItemType Directory -Path $badNxDir | Out-Null
        $nx6Extra = @{}
        if ($nt6.ContainsKey("Accel")) { $nx6Extra["Accel"] = $nt6.Accel }
        $refusedNx = $false
        $refusalText = ""
        try {
            & (Join-Path $PSScriptRoot $nt6.Setup) -VmDir $vm `
                -LocalScriptDir $badNxDir -QemuBinDir $bin -Cpu "pentium3" @nx6Extra | Out-Null
        } catch {
            $refusedNx = $true
            $refusalText = $_.Exception.Message
        }
        Assert-True $refusedNx `
            "$($nt6.Setup) accepted -Cpu pentium3, which has $($nt6.Missing); that guest's Setup will not run on it."
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $badNxDir $nt6.Run))) `
            "$($nt6.Setup) wrote a launcher for a CPU model with $($nt6.Missing) before refusing it."
        Assert-True ($refusalText.Contains($nt6.Missing)) `
            "$($nt6.Setup) refused -Cpu pentium3 but named the wrong missing feature; it must say '$($nt6.Missing)', or the next reader picks a model that fails the same way."
    }

    # THE 64-BIT PAIR'S refuses-without-`-Accel` CHECK LIVED HERE AND WAS
    # DELETED ON 2026-09-10, WHICH IS WHAT IT WAS FOR. It held the line that an
    # accelerator may not be written down until an install has COMPLETED under
    # it, for as long as nothing had been measured for either guest. Both
    # installs have now completed under tcg,thread=multi, both generators carry
    # that as a measured default, and a check asserting they have none would
    # assert the opposite of the truth.
    #
    # What replaces it is not nothing: the two rows at the top of this file now
    # pass NO -Accel, so every accelerator assertion above reads the
    # generators' own defaults rather than a value supplied here. A default that
    # drifted from the measurement would fail there.
    #
    # If a fifth guest is ever added with nothing measured for it, bring the
    # refusal back for THAT generator rather than widening one of these checks:
    # the point was never the pair, it was the unmeasured value.

    # The fallback rung changes the HAL Setup should select. Keep the generated
    # command line and the post-generation verification guidance in agreement.
    $fallbackLaunchers = Join-Path $work "launchers-fallback"
    New-Item -ItemType Directory -Path $fallbackLaunchers | Out-Null
    $fallbackOutput = (& (Join-Path $PSScriptRoot "setup-qemu-win2k-smp.ps1") `
        -VmDir $vm -LocalScriptDir $fallbackLaunchers -QemuBinDir $bin `
        -Accel "tcg,thread=multi" -AcpiOff 6>&1 | Out-String)
    $fallbackRun = Join-Path $fallbackLaunchers "qemu-win2k-smp-run.cmd"
    $fallbackText = [System.IO.File]::ReadAllText($fallbackRun)
    Assert-True ($fallbackText.Contains("-accel tcg,thread=multi")) `
        "the Win2000 SMP fallback launcher lost its selected accelerator."
    Assert-True ($fallbackText.Contains("-machine pc,acpi=off")) `
        "the Win2000 SMP fallback launcher did not disable ACPI."
    Assert-True ($fallbackOutput.Contains("Computer node = 'MPS Multiprocessor PC'")) `
        "the Win2000 SMP fallback guidance names the wrong expected HAL."
    Assert-True ($fallbackText.Contains("Setup should pick the MPS Multiprocessor PC HAL")) `
        "the Win2000 SMP fallback launcher's own comment names the wrong expected HAL."

    # The HAL is fixed at install time, so a system installed under one rung must
    # be booted under it too - the 2b lesson (LESSONS) that install and
    # run launchers must carry identical machine/CPU/accel flags. Assert it across
    # all three launchers of a rung, not just the run one: a rung that drifted in
    # the install launcher alone would leave an installed system that will not
    # boot, and every check above would still pass.
    foreach ($rung in @(
        @{ Dir = $launchers;         Accel = "whpx,kernel-irqchip=off"; Machine = "-machine pc" },
        @{ Dir = $fallbackLaunchers; Accel = "tcg,thread=multi";        Machine = "-machine pc,acpi=off" }
    )) {
        foreach ($leaf in @("install", "prepare-usbd", "run")) {
            $path = Join-Path $rung.Dir "qemu-win2k-smp-$leaf.cmd"
            Assert-True (Test-Path -LiteralPath $path) `
                "the Win2000 SMP $leaf launcher was not generated for the $($rung.Accel) rung."
            $body = [System.IO.File]::ReadAllText($path)
            Assert-True ($body.Contains("-accel $($rung.Accel) ^")) `
                "the Win2000 SMP $leaf launcher does not carry the $($rung.Accel) rung's accelerator."
            Assert-True ($body.Contains("$($rung.Machine) ^")) `
                "the Win2000 SMP $leaf launcher does not carry the $($rung.Accel) rung's machine flags."
            Assert-True ($body.Contains("-smp 2 ^") -and $body.Contains("-cpu pentium3 ^")) `
                "the Win2000 SMP $leaf launcher does not carry 2 vCPUs with the local APIC present."
        }
    }

    # The Windows ME install launcher (build-and-test.md, "Windows ME target
    # VM"): without -Win98Iso it boots the Windows ME CD alone; with it, the
    # Windows 98 SE CD's floppy boots for the format (the Windows ME CD's own
    # FORMAT never writes a sector under QEMU) and the Windows ME CD rides
    # second, where SETUP is E:\WIN9X\SETUP.EXE.
    $meLaunchers = Join-Path $work "launchers-winme"
    New-Item -ItemType Directory -Path $meLaunchers | Out-Null
    & (Join-Path $PSScriptRoot "setup-qemu.ps1") -VmDir $vm -LocalScriptDir $meLaunchers `
        -QemuBinDir $bin -WinMeIso "C:\isos\winme.iso" | Out-Null
    $mePath = Join-Path $meLaunchers "qemu-winme-install.cmd"
    Assert-True (Test-Path -LiteralPath $mePath) "the Windows ME install launcher was not generated."
    $meText = [System.IO.File]::ReadAllText($mePath)
    Assert-True ($meText.Contains('-cdrom "%WINME_ISO%" ^') -and -not $meText.Contains("index=3")) `
        "the Windows ME launcher without -Win98Iso does not boot the Windows ME CD alone."
    Assert-True ($meText.Contains('set "WINME_ISO=C:\isos\winme.iso"')) `
        "the Windows ME launcher does not carry the ISO it was given."
    & (Join-Path $PSScriptRoot "setup-qemu.ps1") -VmDir $vm -LocalScriptDir $meLaunchers `
        -QemuBinDir $bin -WinMeIso "C:\isos\winme.iso" -Win98Iso "C:\isos\win98.iso" | Out-Null
    $meText = [System.IO.File]::ReadAllText($mePath)
    Assert-True ($meText.Contains('-drive file="%WIN98_ISO%",media=cdrom,index=2 ^') -and
        $meText.Contains('-drive file="%WINME_ISO%",media=cdrom,index=3 ^') -and
        -not $meText.Contains('-cdrom "%WINME_ISO%"')) `
        "the Windows ME launcher with -Win98Iso does not boot the Windows 98 SE CD with the Windows ME CD second."
    Assert-True ($meText.Contains("-boot once=d ^") -and $meText.Contains('E:\WIN9X\SETUP.EXE /p j')) `
        "the Windows ME two-CD launcher does not boot the first CD once, or names the wrong SETUP path."
    Assert-True ($meText.Contains('set "WIN98_ISO=C:\isos\win98.iso"') -and $meText.Contains('if not exist "%WIN98_ISO%"')) `
        "the Windows ME two-CD launcher does not check the Windows 98 SE ISO it depends on."
    Assert-True ($meText.Contains("-machine pc ^") -and $meText.Contains("-cpu pentium3 ^") -and
        $meText.Contains("-action reboot=reset -no-shutdown ^")) `
        "the Windows ME launcher lost the machine, CPU or reboot flags the Windows 98 recipe fixes."

    #
    # **NO TWO GUESTS MAY SHARE A QEMU MONITOR PORT**, and nothing checked it
    # until the 2026-09-07 audit's H31. Two launchers on one port cannot both
    # run: the second QEMU fails to bind and dies, or - worse, and this is the
    # one that costs a run - the harness connects to the FIRST guest's monitor
    # believing it is talking to the second, and every reading it takes is of
    # the wrong machine.
    #
    # It has happened twice. The Windows ME launcher took 55558, which is the
    # ACPI-HAL Windows 2000 machine's, until the 2026-09-05 audit; it then took
    # 55560, which is the xHCI-only Windows 2000 machine's, until the
    # 2026-09-07 one. Both were found by reading, not by a check - which is the
    # argument for this being the cheap check it is: the ports are written into
    # the generators as arithmetic on a base, so a collision is a sum nobody
    # evaluated.
    #
    # Every launcher this file generated is scanned, install and run alike:
    # an install launcher colliding with a run launcher matters just as much,
    # since the prepare and install passes are exactly when a second guest is
    # most likely to be up.
    #
    $portsSeen = @{}
    foreach ($dir in @($launchers, $fallbackLaunchers, $meLaunchers)) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        foreach ($cmd in (Get-ChildItem -LiteralPath $dir -File -Filter "*.cmd")) {
            foreach ($line in [System.IO.File]::ReadAllLines($cmd.FullName)) {
                if ($line -match 'tcp:127\.0\.0\.1:(\d+),server') {
                    $port = $Matches[1]
                    # One generator writing the same port into its own install,
                    # prepare and run launchers is correct and expected: they
                    # are the same guest at three moments and never run at
                    # once. What must not happen is two DIFFERENT guests
                    # sharing one, so the key is the port and the value is the
                    # set of launcher STEMS, with the guest's own suffix
                    # removed.
                    $guest = $cmd.BaseName -replace '-(install|prepare-usbd|run|usb-test|net-storage-test)$', ''
                    if (-not $portsSeen.ContainsKey($port)) { $portsSeen[$port] = @() }
                    if ($portsSeen[$port] -notcontains $guest) { $portsSeen[$port] += $guest }
                }
            }
        }
    }
    Assert-True ($portsSeen.Count -gt 0) `
        "no monitor port was found in any generated launcher, so this check measured nothing."
    foreach ($port in ($portsSeen.Keys | Sort-Object)) {
        Assert-True ($portsSeen[$port].Count -le 1) `
            ("monitor port {0} is used by more than one guest: {1}. Two guests on one port cannot both run, and a harness that connects to it reads whichever one answered." -f `
                $port, (($portsSeen[$port] | Sort-Object) -join ", "))
    }
    Write-Ok ("{0} monitor port(s) across the generated launchers, none shared" -f $portsSeen.Count)

    #
    # **THE RESERVATION ON 55563 AND 55564 HAS BEEN CLAIMED AND THE CHECK THAT
    # HELD IT IS GONE.** From 2026-09-10 this file asserted that nothing took
    # those two, because roadmap task 21.8 had reserved them in prose for
    # guests that did not exist yet and Phase 22's 32-bit Vista and Windows 7
    # guests had been drafted onto exactly that pair. The scan above compares
    # generated launchers against each other and is blind to a number reserved
    # in writing, so the reservation had to be honoured by hand until the
    # generators claiming it were written. They now are -
    # setup-qemu-vista-x64.ps1 takes 55563 and setup-qemu-win7-x64.ps1 takes
    # 55564, both generated and scanned above - so the ordinary
    # no-two-guests-share-a-port check covers them like any other guest, and
    # keeping the reservation would now assert that they must NOT exist.
    #
    # The episode is why the check existed and is worth the paragraph: a port
    # taken by a guest nobody has built yet is invisible to every mechanical
    # check here, and the collision surfaces only when that guest is finally
    # generated - which is months later, after both records have been believed.
    # If a future task reserves ports the same way, re-add the same assertion
    # with the same instruction to delete it on claiming.
    #
    Assert-True (($portsSeen.ContainsKey("55563")) -and ($portsSeen.ContainsKey("55564"))) `
        "roadmap task 21.8's guests no longer take monitor ports 55563 and 55564; those two were reserved in writing for them and released only because their generators claimed them, so a guest that has given one up has left it reserved for nothing."
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Err ("QEMU launcher self-tests FAILED: {0} of {1} check(s)." -f `
        $script:failures.Count, $script:checks)
    exit 1
}
Write-Ok ("QEMU launcher self-tests passed ({0} checks)" -f $script:checks)
exit 0
