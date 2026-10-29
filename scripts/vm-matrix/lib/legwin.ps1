# The installer's window, driven from the host (roadmap-usb4 44.19;
# build-and-test.md, "The installer's window, driven from the host"). The
# keys a person presses, sent through QMP send-key, and a screendump after
# each, on a guest whose leg script waits in its window mode: it says
# "XHLEG OPERATOR <CD letter>" on COM1, waits for the installer's log to end
# with "Exit code N.", then says "XHLEG EXIT install N" and "XHLEG WINDOW
# done". Dot-sourced after lib\legvm.ps1 and lib\leghost.ps1.
#
# What the host knows of the window comes from two places only: the guest's
# lines on COM1 (the run has ended, and its exit code) and whether the
# screen changed after a key. No page is recognised by its pixels, so a key
# sequence belongs to one package and one system; a page the sequence does
# not expect stops the step, and the screendumps show where.

# A sample of the screen above its last 150 rows (one pixel in 4 each way):
# the taskbar's clock changes once a minute and a balloon over the
# notification area comes and goes (7, read 2026-10-26: one going was taken
# for the Install key's answer; on Vista one reached 105 rows up). The
# installer's pages differ above that line.
function Get-LegScreenKey {
    param([int]$QmpPort, [string]$Dir)
    $p = Join-Path $Dir 'key.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $null }
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $p
        $h = [math]::Max(1, $b.Height - 150)
        $rect = New-Object System.Drawing.Rectangle 0, 0, $b.Width, $h
        $data = $b.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try {
            $n = $data.Stride * $h
            $bytes = New-Object byte[] $n
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $n)
        } finally { $b.UnlockBits($data) }
        $out = New-Object System.Collections.Generic.List[int]
        for ($y = 0; $y -lt $h; $y += 4) {
            $row = $y * $data.Stride
            for ($x = 0; $x -lt $b.Width; $x += 4) {
                $o = $row + 3 * $x
                $out.Add(([int]$bytes[$o] + [int]$bytes[$o + 1] + [int]$bytes[$o + 2]))
            }
        }
        return , $out.ToArray()
    } catch { return $null } finally { if ($b) { $b.Dispose() } }
}

# Whether two samples differ: more than 0.5 % of their points changed by more
# than 300 (of 765, the sum of the three colours), as dark text on a light
# page does where a page's text changes (read 2026-10-26: 1.7 to 5 % for a
# page on 98 SE, XP, Vista; 0 for Windows 7's pulsing default button; 0.03
# for a window's frame going from inactive to active, which a measure of any
# change put at 3.6 %, above some page changes on XP's larger screen).
function Test-LegScreenChanged {
    param($Before, $After)
    if ($null -eq $Before -or $null -eq $After -or $Before.Length -ne $After.Length -or $Before.Length -eq 0) { return $false }
    $d = 0
    for ($i = 0; $i -lt $Before.Length; $i++) { if ([math]::Abs($Before[$i] - $After[$i]) -gt 300) { $d++ } }
    return ($d -gt 0.005 * $Before.Length)
}
# Waits until the screen has stopped changing by Test-LegScreenChanged's
# measure, at least $MinSeconds after $Since and at most $MaxSeconds: Windows
# 7's pulsing default button would keep Wait-LegScreen, which takes any
# change, waiting to its end.
function Wait-LegWindowStill {
    param([int]$QmpPort, [string]$Dir, [datetime]$Since, [int]$MinSeconds = 3, [int]$MaxSeconds = 90)
    while (((Get-Date) - $Since).TotalSeconds -lt $MinSeconds) { Start-Sleep -Seconds 1 }
    $prev = Get-LegScreenKey -QmpPort $QmpPort -Dir $Dir
    while (((Get-Date) - $Since).TotalSeconds -lt $MaxSeconds) {
        Start-Sleep -Seconds 4
        $now = Get-LegScreenKey -QmpPort $QmpPort -Dir $Dir
        if ($null -ne $prev -and $null -ne $now -and -not (Test-LegScreenChanged $prev $now)) { return $true }
        $prev = $now
    }
    return $false
}

# One page's key. Sent again while the screen has not changed, up to $Tries
# times: a wizard's buttons stay disabled while the installer reads the
# machine, and a key sent then is lost. Returns $true when the screen
# changed.
function Send-LegPageKey {
    param([int]$QmpPort, [string]$Dir, [string]$Chord, [string]$Layout, [string]$Shot, $Process, [int]$Tries = 6)
    $before = Get-LegScreenKey -QmpPort $QmpPort -Dir $Dir
    for ($i = 1; $i -le $Tries; $i++) {
        if ($Process.HasExited) { return $false }
        Send-QmpChord -Port $QmpPort -Chord $Chord -Layout $Layout
        $null = Wait-LegWindowStill -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 3 -MaxSeconds 90
        $after = Get-LegScreenKey -QmpPort $QmpPort -Dir $Dir
        if (Test-LegScreenChanged $before $after) { Save-LegShot $QmpPort $Dir $Shot; return $true }
        Start-Sleep -Seconds 10
    }
    Save-LegShot $QmpPort $Dir $Shot
    return $false
}

# The mean brightness (0 to 255), over a 16-pixel grid, of the screen's two
# side strips 160 pixels wide above its last 60 rows: where a consent prompt
# dims the desktop and does not cover it (read 2026-10-26 on Vista and 7:
# about 107 and 125 without the prompt, 54 and 53 with it; the whole
# screen's mean hardly moved on Vista, the prompt's own body being light).
# -1 when it cannot be read.
function Get-LegScreenMean {
    param([int]$QmpPort, [string]$Dir)
    $p = Join-Path $Dir 'mean.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return -1 }
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $p
        $sum = 0.0; $k = 0
        for ($y = 8; $y -lt $b.Height - 60; $y += 16) {
            for ($x = 8; $x -lt $b.Width; $x += 16) {
                if ($x -ge 160 -and $x -le $b.Width - 160) { continue }
                $c = $b.GetPixel($x, $y); $sum += ($c.R + $c.G + $c.B) / 3.0; $k++
            }
        }
        if ($k -eq 0) { return -1 }
        return $sum / $k
    } catch { return -1 } finally { if ($b) { $b.Dispose() } }
}

# Start, Run on the guest: Ctrl+Esc then R on 98 SE and ME (their Start
# menu's Run has that accelerator), Windows+R on 2000 and later.
function Open-LegRunBox {
    param([int]$QmpPort, $Guest)
    Send-QmpChord -Port $QmpPort -Chord 'esc'; Start-Sleep -Seconds 2
    if ($Guest.Family -eq '9x') {
        Send-QmpChord -Port $QmpPort -Chord 'ctrl-esc'; Start-Sleep -Seconds 3
        Send-QmpChord -Port $QmpPort -Chord 'r' -Layout $Guest.Layout; Start-Sleep -Seconds 4
    } else {
        Send-QmpChord -Port $QmpPort -Chord 'meta_l-r' -Layout $Guest.Layout; Start-Sleep -Seconds 5
    }
}

# The window leg's first start, from the guest's OPERATOR line to the
# restart the installer's Finish page asks for:
#   1. SETUP.EXE from the leg's CD, typed into Start, Run;
#   2. ConsentKeys (Vista, 7: the consent prompt the window's manifest
#      raises), each once the screen is still;
#   3. WindowKeys, the pages up to and including Install, each sent until the
#      screen changes (Send-LegPageKey); 'until-install' walks the pages
#      that are left, whatever their number, to Install;
#   4. the run: the host waits for "XHLEG WINDOW done", answering XP's
#      questions meanwhile when the leg's PromptStep is xp, and on 98 SE and
#      ME the installer's OK/Cancel box with OK;
#   5. Enter, and Enter again if QEMU is still running 120 s later: the
#      "Steps left for you" page, when there is one, takes the first as Next
#      (its Cancel is disabled, so no Enter can cancel there); Finish takes
#      one with "Restart Windows now", its default, which ends QEMU (-action
#      reboot=shutdown). An Enter that reaches a restart under way does
#      nothing. A run that left steps (exit 4) defaults to "Restart Windows
#      later": QEMU still running 120 s after the second Enter, the host
#      shuts Windows down from Start, Run.
# Returns a line for the leg's record; a failure's line says FAILED.
function Invoke-LegWindow {
    param($Def, $Guest, $Config, [hashtable]$Ports, [string]$Dir, $Process, [string]$SerialPath, [string]$Tag, [int]$RunMinutes = 60)
    $QmpPort = $Ports.Qmp
    $layout = $Guest.Layout
    $deadline = (Get-Date).AddSeconds(600)
    $cd = ''
    while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
        foreach ($ln in (Get-LegSerialLines $SerialPath)) { if ($ln -match '^XHLEG OPERATOR ([A-Z]) ') { $cd = $Matches[1] } }
        if ($cd) { break }
        Start-Sleep -Seconds 3
    }
    if (-not $cd) { Save-LegShot $QmpPort $Dir "$Tag-w-nooperator"; return 'window: FAILED, no "XHLEG OPERATOR <letter>" on COM1 in 600 s' }
    $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 10 -MaxSeconds 120
    Save-LegShot $QmpPort $Dir "$Tag-w-00-desktop"
    $consent = @($Def.ConsentKeys | Where-Object { $_ })
    $focusTab = 0
    $n = 2
    $runs = 0
    for ($try = 1; $try -le 3; $try++) {
        $runs = $try
        $mean0 = Get-LegScreenMean -QmpPort $QmpPort -Dir $Dir
        Open-LegRunBox -QmpPort $QmpPort -Guest $Guest
        Send-QmpText -Port $QmpPort -Text "$($cd):\PKG\SETUP.EXE" -Layout $layout; Start-Sleep -Seconds 2
        Save-LegShot $QmpPort $Dir "$Tag-w-01-run$try"
        Send-QmpChord -Port $QmpPort -Chord 'ret'
        if (-not $consent.Count) {
            $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 20 -MaxSeconds 180
            Save-LegShot $QmpPort $Dir "$Tag-w-02-open"
            break
        }
        # The consent prompt dims the whole screen behind it. On 7, the first
        # elevation after a starting point's state was restored was refused
        # at once, with no prompt ("needs an administrator's approval, which
        # was not given"), and the next one prompted (read 2026-10-26): no
        # dimmed screen means that refusal, whose box takes Enter (OK), and
        # SETUP.EXE is run again.
        $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 6 -MaxSeconds 120
        $mean1 = Get-LegScreenMean -QmpPort $QmpPort -Dir $Dir
        Save-LegShot $QmpPort $Dir "$Tag-w-02-prompt$try"
        if ($mean0 -gt 0 -and $mean1 -ge 0 -and $mean1 -lt 0.7 * $mean0) {
            foreach ($k in $consent) {
                $n++
                Send-QmpChord -Port $QmpPort -Chord $k -Layout $layout
                $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 20 -MaxSeconds 180
                Save-LegShot $QmpPort $Dir ("$Tag-w-{0:D2}-consent" -f $n)
            }
            break
        }
        if ($try -eq 3) { return "window: FAILED, no consent prompt after SETUP.EXE three times (shots $Tag-w-02-prompt1..3)" }
        Send-QmpChord -Port $QmpPort -Chord 'ret'
        $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 5 -MaxSeconds 60
    }
    $page = 0
    foreach ($k in @($Def.WindowKeys)) {
        if ($k -eq 'until-install') {
            # Pages the package may or may not show (the Other USB drivers
            # pages follow the download's sets): on each page Alt+I first,
            # which only the Summary page's Install answers (no other control
            # of the window has that accelerator), and Alt+N when it changed
            # nothing. At most ten pages.
            $installed = $false
            for ($j = 1; $j -le 10 -and -not $installed; $j++) {
                $null = Wait-LegWindowStill -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 3 -MaxSeconds 90
                $n++; $page++
                $shot = "$Tag-w-{0:D2}-page{1}-alti" -f $n, $page
                if (Send-LegPageKey -QmpPort $QmpPort -Dir $Dir -Chord 'alt-i' -Layout $layout -Shot $shot -Process $Process -Tries 1) { $installed = $true; break }
                $shot = "$Tag-w-{0:D2}-page{1}-altn" -f $n, $page
                if (-not (Send-LegPageKey -QmpPort $QmpPort -Dir $Dir -Chord 'alt-n' -Layout $layout -Shot $shot -Process $Process)) {
                    return "window: FAILED, page $page took neither Alt+I nor Alt+N (shot $shot)"
                }
            }
            if (-not $installed) { return "window: FAILED, no Install in ten pages" }
            continue
        }
        $n++; $page++
        $shot = "$Tag-w-{0:D2}-page{1}-{2}" -f $n, $page, ($k -replace '[^a-z0-9]', '')
        if (-not (Send-LegPageKey -QmpPort $QmpPort -Dir $Dir -Chord $k -Layout $layout -Shot $shot -Process $Process)) {
            # The window an elevation started can open without the keyboard
            # focus (7 on q35, read 2026-10-26: its Welcome page took no key
            # while its taskbar button was lit): Alt+Tab once, and the key
            # again.
            if ($page -eq 1 -and $consent.Count) {
                Send-QmpChord -Port $QmpPort -Chord 'alt-tab'; Start-Sleep -Seconds 3
                $shot = "$Tag-w-{0:D2}-page{1}-{2}-alttab" -f $n, $page, ($k -replace '[^a-z0-9]', '')
                if (Send-LegPageKey -QmpPort $QmpPort -Dir $Dir -Chord $k -Layout $layout -Shot $shot -Process $Process -Tries 3) { $focusTab = 1; continue }
            }
            return "window: FAILED, page key $page ($k) changed nothing on the screen (shot $shot)"
        }
    }
    # The run. Its end is the guest's word, not the screen's.
    $deadline = (Get-Date).AddMinutes($RunMinutes)
    $lastShot = Get-Date; $lastPrompt = Get-Date; $shots = 0; $prompts = 0; $fnh = 0; $boxes = 0; $nudges = 0; $lastAct = Get-Date
    $done = $false
    while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
        if ((Get-LegSerialLines $SerialPath) -match '^XHLEG WINDOW done') { $done = $true; break }
        if ($Def.ContainsKey('PromptStep') -and $Def.PromptStep -eq 'xp' -and ((Get-Date) - $lastPrompt).TotalSeconds -ge 6) {
            $lastPrompt = Get-Date
            if (Invoke-LegXpLogoAnswer -QmpPort $QmpPort -Dir $Dir -Tag $Tag) { $prompts++; $lastAct = Get-Date }
            elseif (Invoke-LegXpFnhPage -QmpPort $QmpPort -Dir $Dir -Tag $Tag -Layout $layout) { $fnh++; $lastAct = Get-Date }
            elseif (((Get-Date) - $lastAct).TotalSeconds -ge 90 -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir)) {
                # A Found New Hardware wizard the installer waits on can open
                # behind its window, its panel hidden (XP on q35, read
                # 2026-10-26: the installer waited out its 10 minutes):
                # Alt+Tab, 90 s after the last answer or Alt+Tab and on a
                # screen still for 5 s, brings the window
                # behind forward, where the wizard is recognised; when it is
                # not one, Alt+Tab again gives the focus back.
                Send-QmpChord -Port $QmpPort -Chord 'alt-tab'; $nudges++; $lastAct = Get-Date
                Start-Sleep -Seconds 4
                if (Invoke-LegXpFnhPage -QmpPort $QmpPort -Dir $Dir -Tag $Tag -Layout $layout) { $fnh++ }
                else { Send-QmpChord -Port $QmpPort -Chord 'alt-tab' }
            }
        }
        if ($Def.ContainsKey('PromptStep') -and $Def.PromptStep -eq 'nt6' -and ((Get-Date) - $lastPrompt).TotalSeconds -ge 6) {
            $lastPrompt = Get-Date
            if (Invoke-LegNt6UnsignedAnswer -QmpPort $QmpPort -Dir $Dir -Tag $Tag -Layout $layout) { $prompts++; $lastAct = Get-Date }
        }
        # On 98 SE and ME the installer asks before it goes on while
        # Windows may have a hardware wizard open (its gate, an OK/Cancel
        # box over the Progress page, read on ME under q35 on 2026-10-26):
        # OK, its default, as a person taking the defaults would.
        if ($Guest.Family -eq '9x' -and ((Get-Date) - $lastPrompt).TotalSeconds -ge 6) {
            $lastPrompt = Get-Date
            if ((Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir) -eq 'question' -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir -GapSeconds 4)) {
                $boxes++; Save-LegShot $QmpPort $Dir ("$Tag-w-box{0}" -f $boxes)
                Send-QmpChord -Port $QmpPort -Chord 'ret'
            }
        }
        if (((Get-Date) - $lastShot).TotalSeconds -ge 60) { $shots++; Save-LegShot $QmpPort $Dir ("$Tag-w-run{0:D2}" -f $shots); $lastShot = Get-Date }
        Start-Sleep -Seconds 3
    }
    $said = "unsigned-driver question $prompts, Found New Hardware pages $fnh, 9x boxes answered OK $boxes, Alt+Tab $nudges, SETUP.EXE runs $runs, focus Alt+Tab $focusTab"
    if ($Process.HasExited) { return "window: FAILED, QEMU ended during the run ($said)" }
    if (-not $done) { Save-LegShot $QmpPort $Dir "$Tag-w-runtimeout"; return "window: FAILED, no ""XHLEG WINDOW done"" in $RunMinutes min ($said)" }
    $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 10 -MaxSeconds 120
    $n++; Save-LegShot $QmpPort $Dir ("$Tag-w-{0:D2}-ended" -f $n)
    $n++; Save-LegShot $QmpPort $Dir ("$Tag-w-{0:D2}-enter1" -f $n)
    Send-QmpChord -Port $QmpPort -Chord 'ret'
    $enters = 1
    if (-not $Process.WaitForExit(120000)) {
        $n++; Save-LegShot $QmpPort $Dir ("$Tag-w-{0:D2}-enter2" -f $n)
        Send-QmpChord -Port $QmpPort -Chord 'ret'
        $enters = 2
    }
    if ($Process.WaitForExit(120000)) { return "window: walked ($page page keys, Enter $enters, $said), QEMU ended at the restart" }
    # A run that left steps (exit 4) offers "Restart Windows later" by
    # default, and its Finish closes the window with no restart: the host
    # shuts Windows down from Start, Run, as at the end of a leg's phase.
    $n++; Save-LegShot $QmpPort $Dir ("$Tag-w-{0:D2}-norestart" -f $n)
    Open-LegRunBox -QmpPort $QmpPort -Guest $Guest
    $off = 'shutdown -s -f -t 0'
    if ($Guest.Family -eq '9x') { $off = 'rundll32.exe shell32.dll,SHExitWindowsEx 5' }
    if ($Guest.ContainsKey('ShutdownCommand') -and $Guest.ShutdownCommand) { $off = $Guest.ShutdownCommand }
    Send-QmpText -Port $QmpPort -Text $off -Layout $layout; Start-Sleep -Seconds 1
    Send-QmpChord -Port $QmpPort -Chord 'ret'
    if (Wait-LegPowerOff -Config $Config -Guest $Guest -Ports $Ports -Process $Process -Dir $Dir -Tag $Tag) {
        return "window: walked ($page page keys, Enter $enters, $said), Finish with no restart, shut down by the host"
    }
    Save-LegShot $QmpPort $Dir "$Tag-w-noshutdown"
    return "window: FAILED, QEMU still running after the last Enter and the host's shut down ($page page keys, Enter $enters, $said)"
}
