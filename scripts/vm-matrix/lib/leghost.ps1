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
    $need = @('disk.qcow2', 'state.bin', 'idle.iso')
    if (Test-LegDiskMedium $Guest) { $need = @('disk.qcow2', 'state.bin', 'idle.iso.dir') }
    foreach ($f in $need) {
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
    param([string]$Path, [string]$Work, $Guest = $null)
    $d = Join-Path $Work 'idle-cd'
    New-Item -ItemType Directory -Force $d | Out-Null
    Set-Content -LiteralPath (Join-Path $d 'XHIDLE.TXT') -Value 'release matrix: no leg on this CD' -Encoding ASCII
    New-LegMedium -Guest $Guest -SourceDir $d -Path $Path -Label 'XHIDLE'
    Remove-LegTree $d
}

# The leg's medium from a staged folder: a CD image at $Path, or for a
# disk-medium guest (lib\legvm.ps1 Test-LegDiskMedium) a copy of the folder
# at $Path.dir, which QEMU serves as a read-only FAT disk.
function New-LegMedium {
    param($Guest, [string]$SourceDir, [string]$Path, [string]$Label)
    if ($Guest -and (Test-LegDiskMedium $Guest)) {
        $d = Get-LegMediumDir $Path
        Remove-LegTree $d
        Copy-Item -LiteralPath $SourceDir -Destination $d -Recurse
        return
    }
    New-LegIso -SourceDir $SourceDir -Path $Path -Label $Label
}

function Remove-LegMedium {
    param([string]$Path)
    Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    Remove-LegTree (Get-LegMediumDir $Path)
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
    $b = $null; $hit = $false; $teal = $false
    try {
        $b = New-Object System.Drawing.Bitmap $p
        if ($b.Width -eq 1024 -and $b.Height -eq 768) {
            $hit = $true
            foreach ($pt in @(@(343, 275), @(338, 280), @(348, 280))) {
                $c = $b.GetPixel($pt[0], $pt[1])
                if (-not ($c.R -gt 220 -and $c.G -gt 180 -and $c.B -lt 50)) { $hit = $false }
            }
        }
        # Windows 2000's "Digital Signature Not Found" (a package staged
        # with SetupCopyOEMInf, read on q35 on 2026-10-26): its picture's
        # teal (0,130,132) is the one place a 2000 screen shows that colour;
        # Yes has the focus.
        if (-not $hit) {
            $n = 0
            for ($y = 8; $y -lt $b.Height; $y += 8) {
                for ($x = 8; $x -lt $b.Width; $x += 8) {
                    $c = $b.GetPixel($x, $y)
                    if ($c.R -le 10 -and $c.G -ge 120 -and $c.G -le 135 -and $c.B -ge 120 -and $c.B -le 135) { $n++ }
                }
            }
            if ($n -ge 100) { $teal = $true }
        }
    } catch { $hit = $false } finally { if ($b) { $b.Dispose() } }
    if ($teal) {
        Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-sig2k-$(Get-Date -Format HHmmss).png") -Force
        Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 3
        return $true
    }
    if (-not $hit) { return $false }
    Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-logo-$(Get-Date -Format HHmmss).png") -Force
    Send-QmpChord -Port $QmpPort -Chord 'shift-tab'; Start-Sleep -Seconds 1
    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 3
    return $true
}

# Whether a saved screen (a PNG) shows 7's "Would you like to install this
# device software?" box, centred: two columns 100 and 200 pixels right of
# the middle each pass from its white upper part straight into its grey
# (240,240,240) lower part at the same height, and the yellow shield of "You
# should only install driver software from publishers you trust" sits at
# the lower part's left (read on 7 under q35 on 2026-10-27; a message box's
# warning sign is in its white part). Returns the row where the grey part
# begins, or -1 when the box is not on the screen.
function Test-LegNt6PublisherBox {
    param([string]$Path)
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $Path
        $cx = [int]($b.Width / 2)
        $bands = @()
        foreach ($x in @(($cx + 100), ($cx + 200))) {
            # A separator line up to 3 pixels high lies between the two.
            $band = -1; $white = 0; $gap = 0
            for ($y = [int]($b.Height * 0.15); $y -lt [int]($b.Height * 0.8); $y++) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -eq 255 -and $c.G -eq 255 -and $c.B -eq 255) { $white++; $gap = 0; continue }
                if ($white -ge 40 -and $c.R -eq 240 -and $c.G -eq 240 -and $c.B -eq 240) {
                    $grey = 0
                    for ($k = 0; $k -lt 8 -and ($y + $k) -lt $b.Height; $k++) {
                        $d = $b.GetPixel($x, $y + $k)
                        if ($d.R -eq 240 -and $d.G -eq 240 -and $d.B -eq 240) { $grey++ }
                    }
                    if ($grey -eq 8) { $band = $y; break }
                }
                if ($white -ge 40 -and $gap -lt 3) { $gap++; continue }
                $white = 0; $gap = 0
            }
            $bands += $band
        }
        if ($bands[0] -lt 0 -or [math]::Abs($bands[0] - $bands[1]) -gt 2) { return -1 }
        $n = 0
        for ($y = $bands[0]; $y -lt [math]::Min($b.Height, $bands[0] + 90); $y++) {
            for ($x = $cx - 240; $x -le $cx - 215; $x++) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -ge 200 -and $c.G -ge 140 -and $c.B -le 80) { $n++ }
            }
        }
        if ($n -ge 10) { return $bands[0] }
        return -1
    } catch { return -1 } finally { if ($b) { $b.Dispose() } }
}

# Whether "Always trust software from ..." is ticked in that box on a saved
# screen: the check mark is dark inside its square, which sits 15 to 27
# rows below the grey part's first row and 235 to 223 pixels left of the
# middle (read on 7 under q35 on 2026-10-27).
function Test-LegNt6TrustTicked {
    param([string]$Path, [int]$Band)
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $Path
        $cx = [int]($b.Width / 2)
        $n = 0
        for ($y = $Band + 18; $y -le [math]::Min($b.Height - 1, $Band + 24); $y++) {
            for ($x = $cx - 232; $x -le $cx - 226; $x++) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -lt 100 -and $c.G -lt 100 -and $c.B -lt 100) { $n++ }
            }
        }
        return ($n -ge 3)
    } catch { return $false } finally { if ($b) { $b.Dispose() } }
}

# Answers the unsigned-driver question of Vista and 7 ("Windows can't verify
# the publisher of this driver software", a red band across its top) with
# "Install this driver software anyway" (a click), when it is on the screen:
# the band is the one place their screens show that red (read on 7 under q35
# on 2026-10-26); and 7's question for a signed package from a publisher it
# does not trust yet (Test-LegNt6PublisherBox) with Install. Returns $true
# when it answered.
function Invoke-LegNt6UnsignedAnswer {
    param([int]$QmpPort, [string]$Dir, [string]$Tag, [string]$Layout = 'dvorak')
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $false }
    Add-Type -AssemblyName System.Drawing
    $b = $null; $n = 0
    try {
        $b = New-Object System.Drawing.Bitmap $p
        for ($y = 8; $y -lt $b.Height; $y += 8) {
            for ($x = 8; $x -lt $b.Width; $x += 8) {
                $c = $b.GetPixel($x, $y)
                if ($c.R -ge 170 -and $c.G -le 50 -and $c.B -le 50) { $n++ }
            }
        }
    } catch { $n = 0 } finally { if ($b) { $b.Dispose() } }
    if ($n -lt 100) {
        $band0 = Test-LegNt6PublisherBox -Path $p
        if ($band0 -lt 0) { return $false }
        # "Would you like to install this device software?" for a package
        # signed by a publisher the guest does not trust yet (the -nonfree
        # x360c package on 7, read on 2026-10-27). It opens without the
        # focus, like the unsigned-driver question, and its default button
        # is Don't Install. A click at the resting pointer (inside the
        # centred box) gives it the focus; that click can fall on "Always
        # trust software from ...", toggling it and moving the focus there
        # (the leg's guest is a throwaway overlay). Then Alt+I; should the
        # box still stand, the focus is on the check box when the click
        # changed its tick (Tab) and on Don't Install when it did not
        # (Shift+Tab), and Space presses Install. Each screen is kept.
        $t = Get-Date -Format HHmmss
        Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-publisher-$t.png") -Force
        $before = Test-LegNt6TrustTicked -Path $p -Band $band0
        $null = Invoke-Hmp -Port $QmpPort -Line 'mouse_button 1'; Start-Sleep -Milliseconds 300
        $null = Invoke-Hmp -Port $QmpPort -Line 'mouse_button 0'; Start-Sleep -Seconds 2
        try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $true }
        Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-publisher-$t-click.png") -Force
        $band = Test-LegNt6PublisherBox -Path $p
        if ($band -lt 0) { return $true }
        $toggled = ((Test-LegNt6TrustTicked -Path $p -Band $band) -ne $before)
        Send-QmpChord -Port $QmpPort -Chord 'alt-i' -Layout $Layout; Start-Sleep -Seconds 4
        try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return $true }
        Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-publisher-$t-alti.png") -Force
        if ((Test-LegNt6PublisherBox -Path $p) -lt 0) { return $true }
        if ($toggled) { Send-QmpChord -Port $QmpPort -Chord 'tab' } else { Send-QmpChord -Port $QmpPort -Chord 'shift-tab' }
        Start-Sleep -Seconds 1
        Send-QmpChord -Port $QmpPort -Chord 'spc'; Start-Sleep -Seconds 4
        return $true
    }
    Copy-Item -LiteralPath $p -Destination (Join-Path $Dir "$Tag-unsigned-$(Get-Date -Format HHmmss).png") -Force
    # The question opens without the keyboard focus, its taskbar button
    # flashing, and keys reach it only by way of the taskbar, which differs
    # between Vista and 7. A click needs no focus: the pointer, never moved
    # in these guests, rests at the middle of the 800x600 screen, on "Install
    # this driver software anyway" of the centred question (the q35 bases'
    # README; read on Vista and 7 on 2026-10-26).
    $null = Invoke-Hmp -Port $QmpPort -Line 'mouse_button 1'; Start-Sleep -Milliseconds 300
    $null = Invoke-Hmp -Port $QmpPort -Line 'mouse_button 0'; Start-Sleep -Seconds 5
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
                if ($c.R -eq 0 -and $c.G -eq 0 -and $c.B -ge 128 -and $c.B -le 132) { $n++; if ($y -lt $top) { $top = $y; $tx = $x } }
            }
        }
        if ($n -ge 80) {
            $found = $true
            # The title bar sits above the panel's first navy point, which
            # can be well below the panel's top where its picture starts (XP
            # on q35, read 2026-10-26): several columns, up to 80 rows up.
            for ($dx = 0; $dx -le 200 -and -not $active; $dx += 25) {
                $x = [math]::Min($b.Width - 1, $tx + $dx)
                for ($y = $top; $y -gt [math]::Max(0, $top - 80); $y--) {
                    $c = $b.GetPixel($x, $y)
                    if ($c.R -lt 40 -and $c.B -gt 90 -and $c.B -gt $c.G + 40 -and -not ($c.R -eq 0 -and $c.G -eq 0)) { $active = $true; break }
                }
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

# How many points of a 16-pixel grid on the screen are pure navy (0,0,128;
# 0,0,132 at 2000's 16-bit colour): the Found New Hardware wizard's left
# panel on XP and 2000; nothing else on their default desktops is that
# colour. -1 when no screen could be read.
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
                if ($c.R -eq 0 -and $c.G -eq 0 -and $c.B -ge 128 -and $c.B -le 132) { $n++ }
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
        'walk-fnh' {
            # Windows 2000 on q35 raises a Found New Hardware wizard at every
            # start for each platform device it has no driver for, and an
            # install made while one is open, or cancelled (2000 keeps a
            # cancelled one pending until the next start), waits on it (read
            # 2026-10-26: the stick never came, the uninstall exited 4). Each
            # is walked to its end once, when the starting point is made:
            # its first page with Invoke-LegXpFnhPage, then Enter through the
            # search to Finish; a device left with no driver that way raises
            # no wizard again.
            $deadline = $Since.AddSeconds(900)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { break }
                Start-Sleep -Seconds 5
            }
            if (-not ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT')) { return "${Step}: no BOOT line in 900 s" }
            $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 20 -MaxSeconds 120
            $quiet = 0; $walked = 0
            for ($k = 1; $k -le 80 -and $quiet -lt 3 -and -not $Process.HasExited; $k++) {
                if (Invoke-LegXpFnhPage -QmpPort $QmpPort -Dir $Dir -Tag $Tag -Layout $Layout) {
                    $walked++; $quiet = 0
                    for ($j = 1; $j -le 3; $j++) { Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 8 }
                    continue
                }
                $quiet++
                Start-Sleep -Seconds 10
            }
            Save-LegShot $QmpPort $Dir "$Tag-walkfnh"
            if ($quiet -lt 3) { return "${Step}: FAILED, wizards still coming after $walked walked" }
            return "${Step}: $walked wizard pages with the panel walked"
        }        'xp-signing-ignore' {
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
            # 2000 on q35 opens one wizard after another, for each device
            # with no driver (read 2026-10-26), the next one sometimes with
            # the focus: Escape first, and Alt+Tab then Escape only when a
            # wizard is still there.
            for ($i = 1; $i -le 12; $i++) {
                if ($Process.HasExited) { return "${Step}: poller up, the guest switched off before a wizard was read" }
                $n = Get-LegNavyCount -QmpPort $QmpPort -Dir $Dir
                if ($n -lt 0) { Start-Sleep -Seconds 3; continue }
                if ($n -lt 20) { return "${Step}: poller up, no wizard open (after $($i - 1) tries)" }
                Send-QmpChord -Port $QmpPort -Chord 'esc'; Start-Sleep -Seconds 6
                $n = Get-LegNavyCount -QmpPort $QmpPort -Dir $Dir
                if ($n -ge 20) {
                    Send-QmpChord -Port $QmpPort -Chord 'alt-tab'; Start-Sleep -Seconds 3
                    Send-QmpChord -Port $QmpPort -Chord 'esc'; Start-Sleep -Seconds 6
                }
                Save-LegShot $QmpPort $Dir "$Tag-fnh-$i"
            }
            if ($Process.HasExited) { return "${Step}: poller up, the guest switched off before a wizard was read" }
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
            # No wizard when the install already gave the controller its
            # driver (the window's run on the q35 machine, read 2026-10-26):
            # the poller then starts, and no key is sent.
            $deadline = (Get-Date).AddSeconds(300)
            $seen = $false
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir) -eq 'wizard') { $seen = $true; break }
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { return "${Step}: no wizard, the poller up (the controller had its driver)" }
                Start-Sleep -Seconds 5
            }
            if (-not $seen) { Save-LegShot $QmpPort $Dir "$Tag-hcd-nowizard"; return "${Step}: FAILED, neither the wizard nor the poller in 300 s" }
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
        'restart-me' {
            # ME installs the controller found at the start after an install
            # by itself, then asks "System Settings Change: ... restart your
            # computer now?" before the desktop (read 2026-10-25): Yes, its
            # default, ends QEMU at the restart. The poller up first means
            # there was no question.
            $deadline = $Since.AddSeconds(600)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { return "${Step}: poller up, no restart question" }
                if ((Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir) -eq 'question' -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir -GapSeconds 4)) {
                    Save-LegShot $QmpPort $Dir "$Tag-restart-q"
                    # A key sent while ME is still busy behind the question
                    # is lost (read 2026-10-25: Enter changed nothing for
                    # five minutes): Yes again, by its accelerator, while
                    # the question stays.
                    for ($k = 1; $k -le 6; $k++) {
                        if ($k -eq 1) { Send-QmpChord -Port $QmpPort -Chord 'ret' } else { Send-QmpChord -Port $QmpPort -Chord 'alt-y' }
                        if ($Process.WaitForExit(30000)) { return "${Step}: restart question answered Yes ($k keys), QEMU ended at the restart" }
                        if ((Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir) -ne 'question') { break }
                    }
                    if ($Process.WaitForExit(300000)) { return "${Step}: restart question answered Yes, QEMU ended at the restart" }
                    Save-LegShot $QmpPort $Dir "$Tag-restart-end"
                    return "${Step}: FAILED, QEMU still running 300 s after Yes"
                }
                Start-Sleep -Seconds 5
            }
            if ($Process.HasExited) { return "${Step}: QEMU ended before a question was read" }
            Save-LegShot $QmpPort $Dir "$Tag-restart-none"
            return "${Step}: FAILED, neither the poller nor a restart question in 600 s"
        }        'wizard98-stor' {
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
