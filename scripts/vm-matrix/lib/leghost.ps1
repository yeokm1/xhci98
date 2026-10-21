# The host's side of a leg: the steps the host takes in a guest that cannot
# take them itself (a Windows 98 SE hardware wizard, which blocks every
# program started at logon until it is answered), the USB stick the guest asks
# for, and the starting point's files. Dot-sourced after lib\legvm.ps1.

function Get-LegStartDir {
    param($Config, [string]$GuestId)
    return (Join-Path $Config.WorkDir "start\$GuestId")
}

# The starting point a leg restores, checked against this host's QEMU and
# this configuration's machine before anything boots.
function Get-LegStartPoint {
    param($Config, $Guest, [string]$QemuVersion)
    $dir = Get-LegStartDir $Config $Guest.Id
    $meta = Join-Path $dir 'start.json'
    if (-not (Test-Path -LiteralPath $meta)) { throw "guest $($Guest.Id) has no starting point: run make-start.ps1 -Guest $($Guest.Id)" }
    $m = Get-Content -Raw -LiteralPath $meta | ConvertFrom-Json
    $h = Get-LegHardwareHash $Guest $QemuVersion
    if ($m.hardwareHash -ne $h) {
        throw "guest $($Guest.Id): the starting point was saved for another machine or QEMU ($($m.qemuVersion)); make it again with make-start.ps1 -Force"
    }
    foreach ($f in 'disk.qcow2', 'state.bin', 'idle.iso') {
        if (-not (Test-Path -LiteralPath (Join-Path $dir $f))) { throw "guest $($Guest.Id): starting point has no $f" }
    }
    return @{ Dir = $dir; Disk = (Join-Path $dir 'disk.qcow2'); State = (Join-Path $dir 'state.bin'); Idle = (Join-Path $dir 'idle.iso'); Meta = $m }
}

function New-LegOverlay {
    param($Config, [string]$Backing, [string]$Path)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    & $Config.QemuImg create -q -f qcow2 -b $Backing -F qcow2 $Path
    if ($LASTEXITCODE -ne 0) { throw "qemu-img could not make $Path" }
}

function New-LegIdleIso {
    param([string]$Path, [string]$Work)
    $d = Join-Path $Work 'idle-cd'
    New-Item -ItemType Directory -Force $d | Out-Null
    Set-Content -LiteralPath (Join-Path $d 'XHIDLE.TXT') -Value 'release matrix: no leg on this CD' -Encoding ASCII
    New-LegIso -SourceDir $d -Path $Path -Label 'XHIDLE'
    Remove-LegTree $d
}

# Waits until the screen has stopped changing (a dialog has finished
# drawing), at least $MinSeconds after $Since and at most $MaxSeconds.
function Wait-LegScreen {
    param([int]$QmpPort, [string]$Dir, [datetime]$Since, [int]$MinSeconds = 30, [int]$MaxSeconds = 300)
    while (((Get-Date) - $Since).TotalSeconds -lt $MinSeconds) { Start-Sleep -Seconds 2 }
    while (((Get-Date) - $Since).TotalSeconds -lt $MaxSeconds) {
        if (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir) { return $true }
    }
    return $false
}

function Save-LegShot {
    param([int]$QmpPort, [string]$Dir, [string]$Name)
    try { Save-QmpScreen -Port $QmpPort -Path (Join-Path $Dir "$Name.png") } catch { }
}

# What a 98 SE guest's 640x480 screen shows, by a few pixels of the dialogs
# the controller's wizard brings up (Windows 98 SE's standard colours, read
# off this lab's screens on 2026-10-22):
#   'insert-disk'    its active title bar across y 213
#   'copy-files'     "Copying Files..." with its "Copy files from" box: an
#                    active title bar across y 199 and the white box at y 363
#   'copy-progress'  the file copy's progress window, title bar at y 143
#                    (active, or inactive under one of the two above), with
#                    the wizard's own title bar inactive
#   'wizard'         the Add New Hardware wizard's title bar active at y 77
#   'question'       none of the above, the wizard's title bar not active,
#                    and an active title bar across the middle of the screen
#                    (System Settings Change, over the wizard's last page)
#   'other'          anything else; 'unknown' when no screen could be read
function Get-Leg98Dialog {
    param([int]$QmpPort, [string]$Dir)
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return 'unknown' }
    return Get-Leg98DialogOf $p
}

# The same reading of a saved screen (a PNG).
function Get-Leg98DialogOf {
    param([string]$p)
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $p
        if ($b.Width -ne 640 -or $b.Height -ne 480) { return 'unknown' }
        $navy = { param($c) $c.R -lt 40 -and $c.G -lt 40 -and $c.B -gt 120 }
        $dim = { param($c) $c.R -ge 120 -and $c.R -le 150 -and $c.G -ge 120 -and $c.G -le 150 -and $c.B -ge 125 -and $c.B -le 160 }
        if ((& $navy ($b.GetPixel(400, 213))) -and -not (& $navy ($b.GetPixel(400, 199)))) { return 'insert-disk' }
        $w = $b.GetPixel(300, 363)
        if ((& $navy ($b.GetPixel(400, 199))) -and $w.R -gt 240 -and $w.G -gt 240 -and $w.B -gt 240) { return 'copy-files' }
        $wiz = $b.GetPixel(400, 77)
        if (& $navy $wiz) { return 'wizard' }
        $pr = $b.GetPixel(380, 143)
        if ((& $dim $wiz) -and ((& $navy $pr) -or (& $dim $pr))) { return 'copy-progress' }
        for ($y = 120; $y -le 320; $y += 2) { if (& $navy ($b.GetPixel(320, $y))) { return 'question' } }
        return 'other'
    } catch { return 'unknown' } finally { if ($b) { $b.Dispose() } }
}
# Answers XP's unsigned-driver question ("Software Installation" or
# "Hardware Installation": "has not passed Windows Logo testing") with
# Continue Anyway when it is on the screen: its yellow warning sign sits at
# the same place in both dialogs, centred on a 1024x768 screen (read on
# 2026-10-22). STOP Installation has the focus, so Shift+Tab, then Enter
# (Alt+C did not reach the button). Returns $true when it answered.
function Invoke-LegXpLogoAnswer {
    param([int]$QmpPort, [string]$Dir, [string]$Tag)
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $false }
    Add-Type -AssemblyName System.Drawing
    $b = $null; $hit = $false
    try {
        $b = New-Object System.Drawing.Bitmap $p
        if ($b.Width -eq 1024 -and $b.Height -eq 768) {
            $hit = $true
            foreach ($pt in @(@(343, 275), @(338, 280), @(348, 280))) {
                $c = $b.GetPixel($pt[0], $pt[1])
                if (-not ($c.R -gt 220 -and $c.G -gt 180 -and $c.B -lt 50)) { $hit = $false }
            }
        }
    } catch { $hit = $false } finally { if ($b) { $b.Dispose() } }
    if (-not $hit) { return $false }
    Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-logo-$(Get-Date -Format HHmmss).png") -Force
    Send-QmpChord -Port $QmpPort -Chord 'shift-tab'; Start-Sleep -Seconds 1
    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 3
    return $true
}

# Walks XP's Found New Hardware wizard one page when it is on the screen
# (the installer expects it for the root hub: record 23 section 5.8, read
# again on 2026-10-22): its left panel is pure navy; its title bar, just
# above the panel, is dark blue when it has the focus, so Alt+Tab first when
# it has not. On the first page Alt+T takes "No, not this time" (no other
# page has a T accelerator), then Enter takes each page's default (Next,
# "Install the software automatically", Finish). Returns $true when it acted.
function Invoke-LegXpFnhPage {
    param([int]$QmpPort, [string]$Dir, [string]$Tag, [string]$Layout = 'dvorak')
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $false }
    Add-Type -AssemblyName System.Drawing
    $b = $null; $found = $false; $active = $false
    try {
        $b = New-Object System.Drawing.Bitmap $p
        $n = 0; $top = $b.Height; $tx = 0
        for ($y = 8; $y -lt $b.Height; $y += 8) {
            for ($x = 8; $x -lt $b.Width; $x += 8) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -eq 0 -and $c.G -eq 0 -and $c.B -eq 128) { $n++; if ($y -lt $top) { $top = $y; $tx = $x } }
            }
        }
        if ($n -ge 80) {
            $found = $true
            for ($y = $top; $y -gt [math]::Max(0, $top - 40); $y--) {
                $c = $b.GetPixel([math]::Min($b.Width - 1, $tx + 20), $y)
                if ($c.R -lt 30 -and $c.B -gt 200) { $active = $true; break }
            }
        }
    } catch { $found = $false } finally { if ($b) { $b.Dispose() } }
    if (-not $found) { return $false }
    Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-fnh-$(Get-Date -Format HHmmss).png") -Force
    if (-not $active) { Send-QmpChord -Port $QmpPort -Chord 'alt-tab'; Start-Sleep -Seconds 3 }
    Send-QmpChord -Port $QmpPort -Chord 'alt-t' -Layout $Layout; Start-Sleep -Seconds 1
    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 8
    return $true
}

# How many points of a 16-pixel grid on the screen are pure navy (0,0,128):
# the Found New Hardware wizard's left panel on XP; nothing else on its
# default desktop is that colour. -1 when no screen could be read.
function Get-LegNavyCount {
    param([int]$QmpPort, [string]$Dir)
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return -1 }
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $p
        $n = 0
        for ($y = 8; $y -lt $b.Height; $y += 16) {
            for ($x = 8; $x -lt $b.Width; $x += 16) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -eq 0 -and $c.G -eq 0 -and $c.B -eq 128) { $n++ }
            }
        }
        return $n
    } catch { return -1 } finally { if ($b) { $b.Dispose() } }
}

# One named host step. Returns a line for the leg's record. The key
# sequences are the ones the 43.5 legs walked by hand on 98 SE with this
# package (Enter takes each wizard page's default button).
function Invoke-LegHostStep {
    param([string]$Step, [int]$QmpPort, [string]$Dir, [datetime]$Since, $Process, [string]$SerialPath, [string]$Tag,
        [string]$InstallDir = 'C:\XHCI98', [string]$Layout = 'qwerty')
    switch ($Step) {
        'esc-wizard98' {
            # A 9x guest with a controller it has no driver for raises the Add
            # New Hardware wizard at every start; until it closes, nothing
            # started at logon runs. Escape it until the poller says BOOT.
            $deadline = $Since.AddSeconds(420)
            while ((Get-Date) -lt $deadline) {
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { return "${Step}: poller up" }
                if ($Process.HasExited) { break }
                if (((Get-Date) - $Since).TotalSeconds -ge 30) { try { Send-QmpChord -Port $QmpPort -Chord 'esc' } catch { } }
                Start-Sleep -Seconds 10
            }
            if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { return "${Step}: poller up" }
            return "${Step}: no BOOT line in 420 s"
        }
        'xp-signing-ignore' {
            # XP's unsigned-driver policy at Ignore, made the system default,
            # through System Properties, Hardware, Driver Signing (read on
            # 2026-10-22: the registry value a script writes is not honoured,
            # and with Warn every unsigned device install raises a question
            # and a Found New Hardware wizard). Keys on the guest's layout.
            Send-QmpChord -Port $QmpPort -Chord 'meta_l-r' -Layout $Layout; Start-Sleep -Seconds 4
            Send-QmpText -Port $QmpPort -Text 'sysdm.cpl' -Layout $Layout; Start-Sleep -Seconds 1
            Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 12
            Send-QmpChord -Port $QmpPort -Chord 'ctrl-tab'; Start-Sleep -Seconds 2
            Send-QmpChord -Port $QmpPort -Chord 'ctrl-tab'; Start-Sleep -Seconds 3
            Save-LegShot $QmpPort $Dir "$Tag-sig-1"
            Send-QmpChord -Port $QmpPort -Chord 'alt-s' -Layout $Layout; Start-Sleep -Seconds 5
            Send-QmpChord -Port $QmpPort -Chord 'alt-i' -Layout $Layout; Start-Sleep -Seconds 2
            Save-LegShot $QmpPort $Dir "$Tag-sig-2"
            Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 3
            Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 3
            Save-LegShot $QmpPort $Dir "$Tag-sig-3"
            return "${Step}: keys sent (shots $Tag-sig-1..3)"
        }
        'esc-fnh' {
            # An NT guest with a controller it has no driver for opens the
            # Found New Hardware wizard after logon (XP, read 2026-10-22);
            # the poller runs beside it, but an install made under an open
            # wizard waits on it. Once the poller is up and the screen still,
            # Escape cancels it.
            $deadline = $Since.AddSeconds(900)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { break }
                Start-Sleep -Seconds 5
            }
            if (-not ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT')) { return "${Step}: no BOOT line in 900 s" }
            $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 20 -MaxSeconds 120
            Save-LegShot $QmpPort $Dir "$Tag-fnh-0"
            # The wizard opens without the focus (the poller's window keeps
            # it), so Alt+Tab brings it forward and Escape cancels it; its
            # left panel is the one place XP's screen shows pure navy.
            for ($i = 1; $i -le 4; $i++) {
                $n = Get-LegNavyCount -QmpPort $QmpPort -Dir $Dir
                if ($n -lt 0) { Start-Sleep -Seconds 3; continue }
                if ($n -lt 20) { return "${Step}: poller up, no wizard open (after $($i - 1) tries)" }
                Send-QmpChord -Port $QmpPort -Chord 'alt-tab'; Start-Sleep -Seconds 3
                Send-QmpChord -Port $QmpPort -Chord 'esc'; Start-Sleep -Seconds 6
                Save-LegShot $QmpPort $Dir "$Tag-fnh-$i"
            }
            $n = Get-LegNavyCount -QmpPort $QmpPort -Dir $Dir
            if ($n -ge 0 -and $n -lt 20) { return "${Step}: poller up, wizard cancelled" }
            if ($n -lt 0) { return "${Step}: FAILED, the screen could not be read to see the wizard gone" }
            return "${Step}: FAILED, the Found New Hardware wizard is still open"
        }
        'wizard98-hcd' {
            # The first start after the console install on 98 SE: the wizard
            # for "xHCI98 USB 3.x eXtensible Host Controller" (record 23 s6.2):
            # Next three times, Next to install; then, as the file copy asks,
            # OK at "Insert Disk" and the install directory typed into "Copy
            # files from"; Finish; Yes to the restart (which ends QEMU). The
            # copy's dialogs come when the copy reaches them, so they are
            # recognised on the screen (Get-Leg98Dialog), not timed.
            $settled = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since $Since -MinSeconds 40
            Save-LegShot $QmpPort $Dir "$Tag-hcd-0"
            for ($i = 1; $i -le 4; $i++) {
                if ($Process.HasExited) { return "${Step}: FAILED, QEMU ended at Next $i" }
                Send-QmpChord -Port $QmpPort -Chord 'ret'
                $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 4 -MaxSeconds 60
                Save-LegShot $QmpPort $Dir "$Tag-hcd-$i"
            }
            $typed = 0; $disk = 0; $quiet = 0; $copySeen = $false
            $deadline = (Get-Date).AddSeconds(300)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                $state = Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir
                if ($state -eq 'insert-disk') {
                    $copySeen = $true; $disk++; Save-LegShot $QmpPort $Dir "$Tag-hcd-disk$disk"
                    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 4; $quiet = 0; continue
                }
                if ($state -eq 'copy-files') {
                    if ($typed -ge 2) { Save-LegShot $QmpPort $Dir "$Tag-hcd-copyagain"; return "${Step}: FAILED, the copy still asks for files after the directory was given twice" }
                    $copySeen = $true; $typed++; Save-LegShot $QmpPort $Dir "$Tag-hcd-copy$typed"
                    Send-QmpText -Port $QmpPort -Text $InstallDir
                    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 5; $quiet = 0; continue
                }
                # The copy runs in a window of its own over the wizard (its
                # title bar then inactive): only once that has been seen is
                # the wizard's own page, active and still, its last one.
                if ($state -eq 'copy-progress') { $copySeen = $true }
                if ($copySeen -and $state -eq 'wizard' -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir -GapSeconds 4)) { $quiet++ } else { $quiet = 0 }
                if ($quiet -ge 2) { break }
                Start-Sleep -Seconds 2
            }
            if ($Process.HasExited) { return "${Step}: FAILED, QEMU ended before the wizard's last page" }
            # The wizard's own page, active and still, after the copy window:
            # its last page (Finish). Anything else by the deadline is a
            # failure, and no further key is sent.
            if ($quiet -lt 2) { Save-LegShot $QmpPort $Dir "$Tag-hcd-timeout"; return "${Step}: FAILED, the wizard's last page not seen in 300 s (Insert Disk $disk, directory typed $typed)" }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-finish"
            Send-QmpChord -Port $QmpPort -Chord 'ret'
            # The restart question (System Settings Change) comes over the
            # wizard's last page, whose title bar goes inactive: answer it
            # only once it is recognised and the screen is still.
            $gone = $false
            $deadline = (Get-Date).AddSeconds(90)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir) -eq 'question' -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir -GapSeconds 4)) { $gone = $true; break }
                Start-Sleep -Seconds 2
            }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-restart"
            if (-not $gone) { return "${Step}: FAILED, no restart question after Finish (Insert Disk $disk, directory typed $typed)" }
            Send-QmpChord -Port $QmpPort -Chord 'ret'
            if ($Process.WaitForExit(300000)) { return "${Step}: walked (Insert Disk $disk, directory typed $typed), QEMU ended at the restart" }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-end"
            return "${Step}: FAILED, QEMU still running 300 s after the restart answer (Insert Disk $disk, directory typed $typed)"
        }
        'wizard98-stor' {
            # A first Bulk-Only stick on 98 SE raises "USB Mass Storage
            # Device" (roadmap-usb4 43.42, open), whose INF is found through
            # DevicePath with no prompt (the 43.5 leg of 2026-10-20): Enter on
            # each page while the wizard is the active window; no wizard in
            # 120 s is said, not failed (a later build may install silently).
            $deadline = (Get-Date).AddSeconds(120)
            $state = 'other'
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                $state = Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir
                if ($state -in 'wizard', 'copy-files', 'insert-disk') { break }
                Start-Sleep -Seconds 3
            }
            Save-LegShot $QmpPort $Dir "$Tag-stor-0"
            if ($state -ne 'wizard' -and $state -ne 'copy-files' -and $state -ne 'insert-disk') { return "${Step}: no wizard came in 120 s" }
            # Keys only on a recognised page, at most eight; waiting for a copy
            # or a readable screen costs time, not pages.
            $keys = 0
            $deadline = (Get-Date).AddSeconds(300)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 3 -MaxSeconds 60
                $state = Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir
                if ($state -in 'other', 'question') { return "${Step}: walked, $keys pages" }
                if ($state -notin 'wizard', 'copy-files', 'insert-disk') { continue }
                if ($keys -ge 8) { break }
                if ($state -eq 'copy-files') { Send-QmpText -Port $QmpPort -Text $InstallDir }
                Send-QmpChord -Port $QmpPort -Chord 'ret'
                $keys++
                Save-LegShot $QmpPort $Dir "$Tag-stor-$keys"
            }
            if ($Process.HasExited) { return "${Step}: QEMU ended after $keys pages (the leg's checks decide)" }
            Save-LegShot $QmpPort $Dir "$Tag-stor-end"
            return "${Step}: FAILED, the wizard was still up after $keys pages"
        }
        default { throw "unknown host step '$Step'" }
    }
}

function Add-LegStick {
    param($Config, [int]$QmpPort, [string]$Overlay)
    $null = Invoke-Hmp -Port $QmpPort -Line "drive_add 0 if=none,id=stk,format=qcow2,file=$Overlay"
    $null = Invoke-Qmp -Port $QmpPort -Command 'device_add' -Arguments @{
        driver = 'usb-storage'; id = 'stick'; bus = 'xh.0'; drive = 'stk'; removable = $true; serial = 'XHLEGSTICK01'
    }
}

function Remove-LegStick {
    param([int]$QmpPort)
    $null = Invoke-Qmp -Port $QmpPort -Command 'device_del' -Arguments @{ id = 'stick' } -AllowError
}
