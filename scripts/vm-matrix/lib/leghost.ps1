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
    Remove-Item -LiteralPath $d -Recurse -Force
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

# One named host step. Returns a line for the leg's record. The key
# sequences are the ones the 43.5 legs walked by hand on 98 SE with this
# package (Enter takes each wizard page's default button).
function Invoke-LegHostStep {
    param([string]$Step, [int]$QmpPort, [string]$Dir, [datetime]$Since, $Process, [string]$SerialPath, [string]$Tag)
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
            # Next x3, Next (install), OK at "Insert Disk", the install
            # directory typed into "Copy files from", OK, Finish, Yes to the
            # restart (which ends QEMU).
            $settled = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since $Since -MinSeconds 40
            Save-LegShot $QmpPort $Dir "$Tag-hcd-0"
            $seq = @(@('ret', 12), @('ret', 15), @('ret', 12), @('ret', 30), @('ret', 8))
            $i = 1
            foreach ($s in $seq) {
                if ($Process.HasExited) { return "${Step}: QEMU ended at key $i" }
                Send-QmpChord -Port $QmpPort -Chord $s[0]; Start-Sleep -Seconds $s[1]
                Save-LegShot $QmpPort $Dir "$Tag-hcd-$i"; $i++
            }
            Send-QmpText -Port $QmpPort -Text 'C:\XHCI98'
            Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 30
            Save-LegShot $QmpPort $Dir "$Tag-hcd-$i"; $i++
            Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 30
            Save-LegShot $QmpPort $Dir "$Tag-hcd-$i"; $i++
            Send-QmpChord -Port $QmpPort -Chord 'ret'
            if ($Process.WaitForExit(300000)) { return "${Step}: walked (screen settled: $settled), QEMU ended at the restart" }
            Save-LegShot $QmpPort $Dir "$Tag-hcd-end"
            return "${Step}: walked (screen settled: $settled), QEMU still running 300 s after the restart answer"
        }
        'wizard98-stor' {
            # A first Bulk-Only stick on 98 SE raises "USB Mass Storage
            # Device" (roadmap-usb4 43.42, open): Next x4, Finish; the INF is
            # found through DevicePath with no prompt.
            $settled = Wait-LegScreen -QmpPort $QmpPort -Dir $Dir -Since $Since -MinSeconds 20 -MaxSeconds 120
            Save-LegShot $QmpPort $Dir "$Tag-stor-0"
            for ($i = 1; $i -le 5; $i++) {
                Send-QmpChord -Port $QmpPort -Chord 'ret'; Start-Sleep -Seconds 10
                Save-LegShot $QmpPort $Dir "$Tag-stor-$i"
            }
            return "${Step}: walked (screen settled: $settled)"
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
