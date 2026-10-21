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
# off this lab's screens on 2026-10-22): 'insert-disk' (its active title bar
# across y 213), 'copy-files' ("Copying Files..." with its "Copy files from"
# box: an active title bar across y 199 and the white box at y 363),
# 'wizard' (the Add New Hardware wizard's own title bar active at y 77), or
# 'other'.
function Get-Leg98Dialog {
    param([int]$QmpPort, [string]$Dir)
    $p = Join-Path $Dir 'state.png'
    try { Save-QmpScreen -Port $QmpPort -Path $p; Start-Sleep -Milliseconds 400 } catch { return 'other' }
    Add-Type -AssemblyName System.Drawing
    $b = $null
    try {
        $b = New-Object System.Drawing.Bitmap $p
        if ($b.Width -ne 640 -or $b.Height -ne 480) { return 'other' }
        $navy = { param($c) $c.R -lt 40 -and $c.G -lt 40 -and $c.B -gt 120 }
        if (& $navy ($b.GetPixel(400, 213)) -and -not (& $navy ($b.GetPixel(400, 199)))) { return 'insert-disk' }
        $w = $b.GetPixel(300, 363)
        if ((& $navy ($b.GetPixel(400, 199))) -and $w.R -gt 240 -and $w.G -gt 240 -and $w.B -gt 240) { return 'copy-files' }
        if (& $navy ($b.GetPixel(400, 77))) { return 'wizard' }
        return 'other'
    } catch { return 'other' } finally { if ($b) { $b.Dispose() } }
}
# One named host step. Returns a line for the leg's record. The key
# sequences are the ones the 43.5 legs walked by hand on 98 SE with this
# package (Enter takes each wizard page's default button).
function Invoke-LegHostStep {
    param([string]$Step, [int]$QmpPort, [string]$Dir, [datetime]$Since, $Process, [string]$SerialPath, [string]$Tag,
        [string]$InstallDir = 'C:\XHCI98')
    switch ($Step) {
        'esc-wizard98' {
            # A 9x guest with a controller it has no driver for raises the Add
            # New Hardware wizard at every start; until it closes, nothing
            # started at logon runs. Escape it until the poller says BOOT.
            $deadline = $Since.AddSeconds(420)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                if ((Get-LegSerialLines $SerialPath) -match '^XHLEG BOOT') { return "${Step}: poller up" }
                if (((Get-Date) - $Since).TotalSeconds -ge 30) { try { Send-QmpChord -Port $QmpPort -Chord 'esc' } catch { } }
                Start-Sleep -Seconds 10
            }
            return "${Step}: no BOOT line in 420 s"
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
            $typed = 0; $disk = 0; $quiet = 0
            $deadline = (Get-Date).AddSeconds(300)
            while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
                $state = Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir
                if ($state -eq 'insert-disk') {
                    $disk++; Save-LegShot $QmpPort $Dir "$Tag-hcd-disk$disk"
                    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 4; $quiet = 0; continue
                }
                if ($state -eq 'copy-files') {
                    if ($typed -ge 2) { Save-LegShot $QmpPort $Dir "$Tag-hcd-copyagain"; return "${Step}: FAILED, the copy still asks for files after the directory was given twice" }
                    $typed++; Save-LegShot $QmpPort $Dir "$Tag-hcd-copy$typed"
                    Send-QmpText -Port $QmpPort -Text $InstallDir
                    Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 5; $quiet = 0; continue
                }
                if ($state -eq 'wizard' -and (Test-ScreenSettled -QmpPort $QmpPort -Dir $Dir -GapSeconds 4)) { $quiet++ } else { $quiet = 0 }
                if ($quiet -ge 2) { break }
                Start-Sleep -Seconds 2
            }
            if ($Process.HasExited) { return "${Step}: FAILED, QEMU ended before the wizard's last page" }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-finish"
            Send-QmpChord -Port $QmpPort -Chord 'ret'
            $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 6 -MaxSeconds 60
            Save-LegShot $QmpPort $Dir "$Tag-hcd-restart"
            Send-QmpChord -Port $QmpPort -Chord 'ret'
            if ($Process.WaitForExit(300000)) { return "${Step}: walked (Insert Disk $disk, directory typed $typed), QEMU ended at the restart" }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-end"
            return "${Step}: FAILED, QEMU still running 300 s after the restart answer (Insert Disk $disk, directory typed $typed)"
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
                if ($state -ne 'other') { break }
                Start-Sleep -Seconds 3
            }
            Save-LegShot $QmpPort $Dir "$Tag-stor-0"
            if ($state -eq 'other') { return "${Step}: no wizard came in 120 s" }
            for ($i = 1; $i -le 8; $i++) {
                $null = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since (Get-Date) -MinSeconds 3 -MaxSeconds 60
                $state = Get-Leg98Dialog -QmpPort $QmpPort -Dir $Dir
                if ($state -eq 'other') { return "${Step}: walked, $($i - 1) pages" }
                if ($state -eq 'copy-files') { Send-QmpText -Port $QmpPort -Text $InstallDir }
                Send-QmpChord -Port $QmpPort -Chord 'ret'
                Save-LegShot $QmpPort $Dir "$Tag-stor-$i"
            }
            return "${Step}: FAILED, the wizard was still up after 8 pages"
        }        default { throw "unknown host step '$Step'" }
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
