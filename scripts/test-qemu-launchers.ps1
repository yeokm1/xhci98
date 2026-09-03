<#
.SYNOPSIS
Regression tests for the Win98, Win2000, Windows XP and xHCI-only Win2000 QEMU launcher generators.

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

All five VMs are covered because the phases close on comparisons between them:
the traces must land in separate files, and each must belong to one boot.
(The Windows XP guest is the fourth and the xHCI-only Windows 2000 guest the
fifth, both since roadmap Phase 19.)
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
    @{ Name = "Win2000XOnly"; Setup = "setup-qemu-win2k-xonly.ps1"; Launcher = "qemu-win2k-xonly-run.cmd"; LogBase = "win2k-xonly-debugcon" }
)
$work = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("xhci98-qemu-launcher-test-" + [System.IO.Path]::GetRandomFileName())
$script:failures = @()
$script:checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks++
    if (-not $Condition) {
        $script:failures += $Message
        Write-Host "FAIL: $Message" -ForegroundColor Red
    }
}

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
        & (Join-Path $PSScriptRoot $target.Setup) -VmDir $vm -LocalScriptDir $launchers `
            -QemuBinDir $bin | Out-Null

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
        $null = & cmd.exe /c $preamble
        Assert-True ($LASTEXITCODE -eq 0) "$name rotation failed when there was no prior log."
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
