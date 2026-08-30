param(
    [string]$Qemu = "",
    [string]$BootImage = "",
    [string]$BootImageUrl = "",
    [int]$TimeoutSeconds = 20,
    [string[]]$CaseName = @()
)

$ErrorActionPreference = "Stop"
$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$qualDir = Split-Path -Parent $testDir
$repo = Split-Path -Parent $qualDir
$hddDir = Join-Path $testDir "hdd"
$usbImage = Join-Path $testDir "usb.img"
if ($BootImage -eq "") { $BootImage = Join-Path $repo "tools\w98se.img" }
if ([string]::IsNullOrWhiteSpace($Qemu)) {
    $qemuCommand = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
    if ($null -ne $qemuCommand) {
        $Qemu = $qemuCommand.Source
    } else {
        $Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
    }
}
if (-not (Test-Path -LiteralPath $Qemu)) { throw "QEMU not found: $Qemu" }
# Some process spawners inject duplicate case-variant PATH keys (both "Path" and
# "PATH"); collapse them to a single canonical value so the child QEMU process
# sees a well-formed environment. Done AFTER the Get-Command lookup above so the
# sanitize cannot defeat PATH-based resolution of $Qemu.
$pathKeys = @([Environment]::GetEnvironmentVariables().Keys |
    Where-Object { $_ -ieq "Path" })
if ($pathKeys.Count -gt 1) {
    [Environment]::SetEnvironmentVariable(
        "PATH", $env:PATH, [EnvironmentVariableTarget]::Process)
}
# The MS-DOS 7.1 / Windows 98 target DOS is proprietary and not committed; fetch
# it from a user-configured source if missing (see win98-image.ps1). Skip if none.
. (Join-Path $testDir "win98-image.ps1")
# Resolve before use: this same path is handed to QEMU below, and QEMU cannot
# open PowerShell-only path forms (PSDrive qualifiers, ~).
$BootImage = Resolve-Win98ImagePath $BootImage
if (-not (Ensure-Win98Image -BootImage $BootImage -Repo $repo -Url $BootImageUrl)) { exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $qualDir "xhciqual.exe"))) { throw "Build xhciqual.exe first" }
New-Item -ItemType Directory -Path $hddDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $qualDir "xhciqual.exe") -Destination (Join-Path $hddDir "XHCIQUAL.EXE") -Force
if (-not (Test-Path -LiteralPath $usbImage)) {
    $stream = [IO.File]::Create($usbImage)
    $stream.SetLength(8MB)
    $stream.Close()
}

function Send-Hmp($writer, [string]$command) {
    $writer.WriteLine($command)
    $writer.Flush()
    Start-Sleep -Milliseconds 30
}

function Send-Key($writer, [string]$key) {
    Send-Hmp $writer ("sendkey " + $key)
}

function Send-Text($writer, [string]$value) {
    foreach ($ch in $value.ToCharArray()) {
        if ($ch -eq " ") { $key = "spc" }
        elseif ($ch -eq "-") { $key = "minus" }
        elseif ($ch -eq ":") { $key = "shift-semicolon" }
        elseif ($ch -eq "=") { $key = "equal" }
        else { $key = [string]$ch }
        Send-Key $writer $key
    }
    Send-Key $writer "ret"
}

function Read-Vga($writer, [string]$path) {
    $dump = $path.Replace([char]92, [char]47)
    Send-Hmp $writer ("pmemsave 0xb8000 4000 " + $dump)
    Start-Sleep -Milliseconds 100
    if (-not (Test-Path -LiteralPath $path)) { return "" }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 4000) { return "" }
    $lines = @()
    for ($row = 0; $row -lt 25; $row++) {
        $chars = for ($col = 0; $col -lt 80; $col++) {
            $value = $bytes[($row * 80 + $col) * 2]
            if ($value -ge 32 -and $value -lt 127) { [char]$value } else { " " }
        }
        $lines += (-join $chars).TrimEnd()
    }
    return $lines -join [Environment]::NewLine
}

function Wait-Vga($writer, [string]$path, [string]$pattern, [int]$seconds) {
    $end = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        $text = Read-Vga $writer $path
        if ($text -match $pattern) { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $end)
    throw "VGA timeout waiting for $pattern"
}

function Connect-Monitor([int]$port) {
    $end = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $client = New-Object Net.Sockets.TcpClient
        try {
            $client.Connect("127.0.0.1", $port)
            return $client
        } catch {
            $client.Close()
            Start-Sleep -Milliseconds 100
        }
    } while ([DateTime]::UtcNow -lt $end)
    throw "QEMU monitor did not open"
}

function Case([string]$name, [array]$qemuArgs, [string]$command,
              [array]$expect) {
    [pscustomobject]@{
        Name=$name
        Args=$qemuArgs
        Command=$command
        Expect=$expect
    }
}

$cmd = "xhciqual --serial --no-wait --no-page"
# Task 11-V.8's read-only quick scan. No --no-wait, because it waits for
# nothing, and no family selector, because it always scans all three.
$quick = "xhciqual --quick --serial --no-page"
$cases = @(
    (Case "help" @() "xhciqual --serial --no-page --help" @("PURPOSE","SAFETY - READ BEFORE AN ACTIVE RUN","RECOMMENDED TEST ORDER","RECOMMENDED DEVICES","OHCI WARN proves SOF; its note says if PCI INTx was seen","OUTPUT AND EXIT CODES","photo plus last printed line")),
    (Case "none_default" @() $cmd @("Families: xHCI EHCI OHCI","No selected USB host controller found")),
    (Case "xhci_irq_selftest" @("-device","qemu-xhci,id=hc,msi=off,msix=off") ($cmd+" --irq-selftest") @("IRQ-SELFTEST","Families: xHCI","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:    SKIP","IRQ SELF-TEST PASS","Done.")),
    # HCCPARAMS2/FSC: qemu-xhci reports CAPLENGTH 40h, so the register is
    # reachable, and answers 0 - which is what makes every CSS on this vehicle a
    # declined save. Pinned here because the register is read conditionally, so a
    # gate that stopped reading it would otherwise print nothing and pass.
    (Case "xhci_probe_only" @("-device","qemu-xhci,id=hc,msi=off,msix=off") "xhciqual --serial --no-page xhci --probe-only" @("PROBE-ONLY (read-only)","Found 1 selected","HCCPARAMS2 00000000  FSC=0","(FSC=0: suspend/resume re-enumerates)","fsc=0","Probe-only run:","Active tests (C1-C7) NOT run","Probe safety: PASS - no PCI configuration writes.","Done.")),
    # The zero-write assertion is global (one line per run, covering every
    # controller scanned), so one all-family probe covers the xHCI, EHCI, and
    # OHCI probe paths at once - the EHCI/OHCI paths own the config writes
    # (legacy handoff, the PCI 2.3 INTx-status probe, cleanup) that must stay
    # behind the active gate.
    (Case "all_probe_only" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") "xhciqual --serial --no-page --probe-only" @("PROBE-ONLY (read-only)","Found 3 selected","==== EHCI Controller","==== OHCI Controller","Probe safety: PASS - no PCI configuration writes.","Done.")),
    (Case "xhci_none_default" @("-device","qemu-xhci,id=hc,msi=off,msix=off") $cmd @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:    SKIP","Done.")),
    (Case "xhci_hs_storage" @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" --scan xhci") @("Families: xHCI","Found 1 selected","C6 ports:    PASS","C8 devices:  PASS","DEV port=","Done.")),
    (Case "xhci_fs_mouse" @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off","-device","usb-mouse,bus=hc.0,port=1") ($cmd+" --xhci") @("Found 1 selected","C6 ports:    PASS","C8 devices:  PASS","iface=03.01.02","Done.")),
    (Case "xhci_ss_only" @("-device","qemu-xhci,id=hc,msi=off,msix=off","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" xhci") @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:    SKIP","Done.")),
    (Case "xhci_hub_mouse" @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off","-device","usb-hub,id=hub,bus=hc.0,port=1","-device","usb-mouse,bus=hc.0,port=1.1") ($cmd+" --scan=xhci") @("Found 1 selected","C6 ports:    PASS","C8 devices:  PASS","iface=09.","Done.")),
    (Case "xhci_poll_empty" @("-device","qemu-xhci,id=hc,msi=off,msix=off") ($cmd+" xhci --poll-only") @("POLL-ONLY","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      SKIP","C6 ports:    SKIP","PROVISIONAL","Done.")),
    (Case "xhci_poll_storage" @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" xhci --poll-only") @("POLL-ONLY","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      SKIP","C6 ports:    PASS","PROVISIONAL","Done.")),
    (Case "xhci_poll_hub" @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off","-device","usb-hub,id=hub,bus=hc.0,port=1","-device","usb-mouse,bus=hc.0,port=1.1") ($cmd+" xhci --poll-only") @("POLL-ONLY","Found 1 selected","C6 ports:    PASS","PROVISIONAL","Done.")),
    (Case "ehci_none" @("-device","usb-ehci,id=hc") ($cmd+" --scan ehci") @("Families: EHCI","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:    SKIP","Done.")),
    (Case "ehci_hs_storage" @("-device","usb-ehci,id=hc","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" ehci") @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:    PASS","Done.")),
    (Case "ehci_poll_storage" @("-device","usb-ehci,id=hc","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" ehci --poll-only") @("POLL-ONLY","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      SKIP","C6 ports:    PASS","PROVISIONAL","Done.")),
    (Case "ehci_fs_mouse" @("-device","usb-ehci,id=hc","-device","usb-mouse,bus=hc.0,port=1") ($cmd+" --ehci") @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","C6 ports:","Done.")),
    (Case "ohci_none" @("-device","pci-ohci,id=hc") ($cmd+" --scan ohci") @("Families: OHCI","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      WARN","C6 ports:    SKIP","Done.")),
    (Case "log_family_selector" @("-device","pci-ohci,id=hc") ($cmd+" --log ohci") @("Families: OHCI","Found 1 selected","C4 IRQ:      WARN","Done. Report copied to XHCIQUAL.LOG.")),
    (Case "ohci_mouse" @("-device","pci-ohci,id=hc","-device","usb-mouse,bus=hc.0,port=1") ($cmd+" ohci") @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      WARN","C6 ports:    PASS","Done.")),
    (Case "ohci_storage" @("-device","pci-ohci,id=hc","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=hc.0,port=1") ($cmd+" --ohci") @("Found 1 selected","C3 DMA:      PASS","C4 IRQ:      WARN","C6 ports:    PASS","Done.")),
    (Case "xhci_ehci_none" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci") $cmd @("Found 2 selected","==== Controller","==== EHCI Controller","Done.")),
    (Case "xhci_ohci_none" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","pci-ohci,id=ohci") $cmd @("Found 2 selected","==== Controller","==== OHCI Controller","Done.")),
    (Case "ehci_ohci_none" @("-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") $cmd @("Found 2 selected","==== EHCI Controller","==== OHCI Controller","Done.")),
    (Case "xhci_ehci_mixed" @("-device","qemu-xhci,id=xhci,p3=0,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","usb-mouse,bus=xhci.0,port=1","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=ehci.0,port=1") $cmd @("Found 2 selected","C6 ports:    PASS","==== EHCI Controller","Done.")),
    (Case "xhci_ohci_mixed" @("-device","qemu-xhci,id=xhci,p3=0,msi=off,msix=off","-device","pci-ohci,id=ohci","-device","usb-mouse,bus=xhci.0,port=1","-device","usb-kbd,bus=ohci.0,port=1") $cmd @("Found 2 selected","C6 ports:    PASS","==== OHCI Controller","Done.")),
    (Case "ehci_ohci_mixed" @("-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=ehci.0,port=1","-device","usb-mouse,bus=ohci.0,port=1") $cmd @("Found 2 selected","C6 ports:    PASS","==== OHCI Controller","Done.")),
    (Case "all_none" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") $cmd @("Found 3 selected","==== Controller","==== EHCI Controller","==== OHCI Controller","Done.")),
    (Case "all_mixed" @("-device","qemu-xhci,id=xhci,p3=0,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci","-device","usb-mouse,bus=xhci.0,port=1","-drive","if=none,id=ud,file=$usbImage,format=raw","-device","usb-storage,drive=ud,bus=ehci.0,port=1","-device","usb-kbd,bus=ohci.0,port=1") $cmd @("Found 3 selected","C6 ports:    PASS","Done.")),
    (Case "combined_selector" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") ($cmd+" --scan ehci --scan ohci") @("Families: EHCI OHCI","Found 2 selected","==== EHCI Controller","==== OHCI Controller","Done.")),
    (Case "missing_selected_family" @("-device","qemu-xhci,id=hc,msi=off,msix=off") ($cmd+" --scan ohci") @("Families: OHCI","No selected USB host controller found")),
    #
    # Task 11-V.8's read-only quick scan, one case per controller-presence
    # mask. It had NO case at all before this batch, which is why nothing would
    # have caught a regression in what is now the common path - the first thing
    # a user runs.
    #
    # `--quick` rather than a bare invocation: the matrix reads its verdict off
    # the serial line, so it has to pass --serial, and any argument at all takes
    # the run off the no-argument default. What the bare invocation does is the
    # same code with none of these knobs set - a real console read-through, on a
    # multi-controller machine, which is where the roadmap and `xhciqual/main.c`
    # both put it. Not the Low-Speed / Windows 2000 resume leg: this file took
    # that wrong branch once, before Phase 13 was re-cut into batches and the
    # legs were told apart. No machine this project has left carries two USB
    # controllers, so the read-through is published as a limitation rather than
    # scheduled.
    #
    # Every case asserts the safety line as well as the verdict. A quick scan
    # that silently started writing PCI configuration would still print a
    # plausible verdict, and that line is the only thing that would notice.
    (Case "quick_none" @() $quick @("quick scan (read-only)","No USB host controller found","DISQUALIFIED")),
    (Case "quick_xhci" @("-device","qemu-xhci,id=hc,msi=off,msix=off") $quick @("Found 1 USB host controller","xHCI ","LOOKS QUALIFIED","Probe safety: PASS - no PCI configuration writes.","Next: XHCIQUAL xhci --poll-only")),
    (Case "quick_ehci" @("-device","usb-ehci,id=hc") $quick @("Found 1 USB host controller","EHCI ","LOOKS QUALIFIED","Probe safety: PASS - no PCI configuration writes.")),
    (Case "quick_ohci" @("-device","pci-ohci,id=hc") $quick @("Found 1 USB host controller","OHCI ","LOOKS QUALIFIED","Probe safety: PASS - no PCI configuration writes.")),
    (Case "quick_xhci_ehci" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci") $quick @("Found 2 USB host controller","xHCI ","EHCI ","LOOKS QUALIFIED","Probe safety: PASS - no PCI configuration writes.")),
    (Case "quick_xhci_ohci" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","pci-ohci,id=ohci") $quick @("Found 2 USB host controller","xHCI ","OHCI ","Probe safety: PASS - no PCI configuration writes.")),
    (Case "quick_ehci_ohci" @("-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") $quick @("Found 2 USB host controller","EHCI ","OHCI ","Probe safety: PASS - no PCI configuration writes.")),
    # The overflow shape - a machine carrying xHCI plus EHCI functions, which no
    # fleet machine does any more - so the one
    # that says whether the screen budget survives a multi-controller machine.
    (Case "quick_all" @("-device","qemu-xhci,id=xhci,msi=off,msix=off","-device","usb-ehci,id=ehci","-device","pci-ohci,id=ohci") $quick @("Found 3 USB host controller","xHCI ","EHCI ","OHCI ","Probe safety: PASS - no PCI configuration writes.")),
    # `--full` must still reach the old no-argument behaviour byte for byte,
    # which is the clause "break no existing invocation" turns on.
    (Case "quick_full_is_the_old_default" @("-device","qemu-xhci,id=hc,msi=off,msix=off") ($cmd+" --full") @("Mode: FULL (active bring-up tests)","Families: xHCI EHCI OHCI","Found 1 selected","C3 DMA:      PASS","C4 IRQ:      PASS","Done."))
)
if ($CaseName.Count -ne 0) {
    $cases = @($cases | Where-Object { $_.Name -in $CaseName })
    if ($cases.Count -eq 0) { throw "No requested matrix cases matched" }
}

$failures = @()
for ($index=0; $index -lt $cases.Count; $index++) {
    $case = $cases[$index]
    $stderrPath = Join-Path $testDir ('stderr-' + $case.Name + '.log')
    $port = 20000 + ([Diagnostics.Process]::GetCurrentProcess().Id % 30000) + $index
    $serialPath = Join-Path $testDir ("serial-" + $case.Name + ".log")
    $vgaPath = Join-Path $testDir ("vga-" + $case.Name + ".bin")
    Remove-Item -LiteralPath $serialPath,$vgaPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    # Read-only VVFAT backing + per-drive snapshot: the guest gets a writable C:
    # for XHCIQUAL and any --log copy, but those writes hit a throwaway overlay
    # and never modify the host hdd\ staging dir. Results are read from the serial
    # log, not this disk. Do not use fat:rw: - that writes back to the host.
    # not $args: that is a PowerShell automatic variable
    $qargs = @("-machine","pc","-m","64","-boot","a","-drive","if=floppy,file=$BootImage,format=raw","-drive","file=fat:$hddDir,format=raw,if=ide,media=disk,snapshot=on","-display","none","-monitor","tcp:127.0.0.1:$port,server=on,wait=off","-serial","file:$serialPath") + $case.Args
    Write-Host ("[{0}/{1}] {2}" -f ($index+1),$cases.Count,$case.Name)
    $process = Start-Process -FilePath $Qemu -ArgumentList $qargs -PassThru -WindowStyle Hidden -RedirectStandardError $stderrPath
    $client = $null
    $writer = $null
    try {
        $client = Connect-Monitor $port
        $writer = New-Object IO.StreamWriter($client.GetStream())
        # The bare Win98SE disk boots straight to the A:\ prompt (no menu).
        Wait-Vga $writer $vgaPath "A:\\>" 45
        Send-Text $writer "c:"
        Wait-Vga $writer $vgaPath "C:\\>" 10
        Send-Text $writer $case.Command
        $end = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $serial = ""
        do {
            Start-Sleep -Milliseconds 200
            if (Test-Path -LiteralPath $serialPath) { $serial = Get-Content -Raw -LiteralPath $serialPath }
            if ($serial -match "Done\." -or
                $serial -match "No selected USB host controller found" -or
                $serial -match "photo plus last printed line") { break }
        } while ([DateTime]::UtcNow -lt $end)
        $vga = Read-Vga $writer $vgaPath
        if (Test-Path -LiteralPath $serialPath) {
            Start-Sleep -Milliseconds 200
            $serial = Get-Content -Raw -LiteralPath $serialPath
        }
        if ($serial -notmatch "Done\." -and
            $serial -notmatch "No selected USB host controller found" -and
            $serial -notmatch "photo plus last printed line") {
            throw "qualifier timed out; VGA: $vga"
        }
        if (($serial+$vga) -match "DOS/32A fatal|exception|page fault|general protection|divide error") {
            throw "fault text detected"
        }
        foreach ($pattern in $case.Expect) {
            if ($serial -notmatch [regex]::Escape($pattern)) {
                throw "missing expected output: $pattern"
            }
        }
        Write-Host "  PASS"
    } catch {
        $message = $_.Exception.Message
        if (Test-Path -LiteralPath $stderrPath) {
            # -Raw answers $null for an EMPTY file, not "", so the .Trim()
            # below used to throw inside the handler and replace the real
            # failure message with "You cannot call a method on a null-valued
            # expression" - i.e. a QEMU run that produced no stderr, which is
            # the healthy case, hid every diagnosis this block exists to give.
            $qemuError = Get-Content -Raw -LiteralPath $stderrPath
            if ($null -ne $qemuError -and $qemuError.Trim() -ne '') {
                $message += '; QEMU: ' + $qemuError.Trim()
            }
        }
        $failures += ($case.Name + ": " + $message)
        Write-Host ("  FAIL: " + $message)
    } finally {
        if ($client -ne $null -and $client.Connected) {
            try { Send-Hmp $writer "quit" } catch {}
            $client.Close()
        }
        Start-Sleep -Milliseconds 200
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.WaitForExit()
    }
}

Write-Host ""
Write-Host ("QEMU matrix: {0} passed, {1} failed" -f ($cases.Count-$failures.Count),$failures.Count)
foreach ($failure in $failures) { Write-Host ("  " + $failure) }
if ($failures.Count -ne 0) { exit 1 }
