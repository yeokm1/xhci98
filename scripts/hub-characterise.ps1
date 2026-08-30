# Characterise USB hubs and devices on a Windows host, with no download and no
# Linux.
#
# Roadmap batch 13-H requires each hub and each device to be characterised
# before a bench trip, in the rig it will be used in, because none of the
# properties below is printed on the box and Windows' PnP layer will not answer
# for them afterwards. The roadmap names `lsusb -v` or USB Device Tree Viewer;
# this does the same job from a stock Windows host.
#
# Reports, per hub and per connected device:
#   bDeviceProtocol  0 = Full-Speed hub (no TT), 1 = single-TT, 2 = multi-TT
#   interface alternate settings - a multi-TT hub offers a second one (alt 1,
#                    protocol 2), which is what this driver's MTT follows
#   wHubCharacteristics, with TTT decoded from bits 6:5 - the field the
#                    context-field clause compares the hub slot's DW2 against
#   the negotiated speed of every attached device
#   the manufacturer and product strings, so that two units of one VID:PID can
#                    be told apart in the record
#   the full interface list, so that "it is a composite" is a measurement
#   interface association descriptors, i.e. whether a function is IAD-grouped
#   every endpoint's bInterval, with its service period decoded for the speed
#                    the device actually negotiated
#   bMaxPacketSize0  the EP0 max packet size, and for a Full-Speed device
#                    whether it is 8 (no correction needed) or 16/32/64 (the
#                    driver addresses at 8 and then issues Evaluate Context).
#                    Added for batch 13-E's Finding 2.
#
# The last three are what task 13-H.1 used to send someone to usbview by hand
# for; they are here because they decide whether task 13-E.3 clause 3
# ("a device with bInterval > 1") can be taken with the device in the bag at
# all, and that is a pre-trip question rather than a bench one.
#
# Mechanism: the same IOCTLs usbview.exe uses against the USB hub driver.
# Windows' own PnP layer is NOT sufficient here - it collapses every hub's
# compatible IDs to USB\USB20_HUB / USB\USB30_HUB and never exposes the
# protocol byte.
#
# Note the structure layouts below are load-bearing and self-checked at runtime;
# see the comments at each check. USB_NODE_CONNECTION_INFORMATION_EX is fully
# packed, so DeviceAddress is unaligned at 25 and ConnectionStatus is at 31 -
# the naturally-aligned offsets are wrong and read as "no device connected" on
# every port, silently.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\hub-characterise.ps1
#        set HUBDEBUG=1 to add a raw hexdump of each connection record.
#
#        -Walk [-Hub 1A40:0201] [-Seconds 120] [-PollMs 300]
#            socket-walk mode: watch every port and number each arrival, so
#            that walking one device down a hub's sockets in physical order
#            yields the physical-socket-to-logical-port map in one pass. The
#            report above cannot answer that question at all - see the
#            comment on the mode itself, just above the report loop.

param(
    # Socket-walk mode - see the comment block just above the main report loop.
    # Off by default, so the bare invocation the roadmap and the run sheets name
    # keeps printing exactly what it always printed.
    [switch]$Walk,
    # Restrict the walk to one hub: "1A40:0201", or any substring of the device
    # interface path. Omitted, every hub is watched, root hubs included - which
    # is also how a laptop's physical connectors get mapped to root port numbers
    # without one cold boot per socket.
    [string]$Hub,
    # Bound the walk, so it can be run unattended or from a wrapper. 0 = until
    # Ctrl+C; the arrival summary prints either way.
    [int]$Seconds = 0,
    [int]$PollMs = 300
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class UsbHubQuery
{
    const int DIGCF_PRESENT = 0x2;
    const int DIGCF_DEVICEINTERFACE = 0x10;

    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVICE_INTERFACE_DATA
    {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public int Flags;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid gc, IntPtr enumerator, IntPtr parent, int flags);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr h, IntPtr devInfo, ref Guid gc, int index, ref SP_DEVICE_INTERFACE_DATA data);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr h, ref SP_DEVICE_INTERFACE_DATA data, IntPtr detail, int detailSize, ref int required, IntPtr devInfoData);

    [DllImport("setupapi.dll")]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr h);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr templ);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inBuf, int inSize, byte[] outBuf, int outSize, ref int returned, IntPtr ov);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    public static string[] HubPaths()
    {
        Guid g = new Guid("f18a0e88-c30c-11d0-8815-00a0c906bed8");
        IntPtr set = SetupDiGetClassDevs(ref g, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        List<string> list = new List<string>();
        SP_DEVICE_INTERFACE_DATA d = new SP_DEVICE_INTERFACE_DATA();
        d.cbSize = Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
        int i = 0;
        while (SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref g, i, ref d))
        {
            int req = 0;
            SetupDiGetDeviceInterfaceDetail(set, ref d, IntPtr.Zero, 0, ref req, IntPtr.Zero);
            IntPtr det = Marshal.AllocHGlobal(req);
            Marshal.WriteInt32(det, IntPtr.Size == 8 ? 8 : 6);
            if (SetupDiGetDeviceInterfaceDetail(set, ref d, det, req, ref req, IntPtr.Zero))
                list.Add(Marshal.PtrToStringUni(new IntPtr(det.ToInt64() + 4)));
            Marshal.FreeHGlobal(det);
            i++;
        }
        SetupDiDestroyDeviceInfoList(set);
        return list.ToArray();
    }

    // Returns the buffer on success, null on failure (with Win32Error set).
    public static int Win32Error = 0;

    // Bytes the last successful Ioctl actually returned. A descriptor fetch can
    // succeed and still be short of what the descriptor's own wTotalLength
    // claims - the hub driver caps the transfer rather than failing it - so a
    // caller walking a descriptor chain must bound the walk by this and not by
    // wTotalLength. A truncated walk stops early and silently, which on an
    // audio device would drop exactly the endpoints its bInterval is read from.
    public static int Returned = 0;

    public static byte[] Ioctl(string path, uint code, byte[] buf, int outSize)
    {
        Win32Error = 0;
        Returned = 0;
        IntPtr h = CreateFileW(path, 0x40000000u, 0x2u, IntPtr.Zero, 3u, 0u, IntPtr.Zero);
        if (h.ToInt64() == -1)
        {
            Win32Error = Marshal.GetLastWin32Error();
            return null;
        }
        byte[] outBuf = new byte[outSize];
        Array.Copy(buf, outBuf, Math.Min(buf.Length, outSize));
        int ret = 0;
        // These IOCTLs share one buffer and validate InputBufferLength against the
        // whole structure, not just the leading ConnectionIndex.
        bool ok = DeviceIoControl(h, code, outBuf, outSize, outBuf, outSize, ref ret, IntPtr.Zero);
        if (!ok) Win32Error = Marshal.GetLastWin32Error();
        else Returned = ret;
        CloseHandle(h);
        return ok ? outBuf : null;
    }
}
'@

$IOCTL_NODE_INFO    = 0x220408
$IOCTL_CONN_INFO_EX = 0x220448
$IOCTL_DESC_FROM_NC = 0x220410
$IOCTL_NODE_CONN_NAME = 0x220414

$speedName = @{ 0 = 'Low'; 1 = 'Full'; 2 = 'High'; 3 = 'Super' }
# Protocol 3 is a SuperSpeed hub - the SuperSpeed half of a USB 3.x hub unit.
# It has no transaction translator at all, so the TTT printed on its own HUB
# line below is a decode of reserved bits and means nothing. Left printed rather
# than suppressed because deciding it needs the hub's protocol at the point the
# hub's own descriptor is printed, and that is known only from its parent's
# connection record; naming it here is the honest half of the fix. This driver
# never manages a SuperSpeed port, so no clause depends on it.
$protoName = @{ 0 = 'Full-Speed hub (no TT)'; 1 = 'single-TT'; 2 = 'multi-TT'
                3 = 'SuperSpeed hub (no TT - ignore its TTT)' }
$xferName  = @{ 0 = 'Control'; 1 = 'Isoch'; 2 = 'Bulk'; 3 = 'Interrupt' }

# USB_CONNECTION_STATUS. Only DeviceConnected yields a characterisation; the
# rest are reported rather than skipped, because a device that is plugged in and
# failed is exactly what this script exists to notice. Skipping them silently
# reads as "nothing plugged in", which on a characterisation bench is the one
# answer that must never be guessed - a device drawing too much current behind a
# bus-powered hub is a rig fault, not an absent device, and the two look
# identical if the port is not printed.
$connStatus = @{
    0 = 'NoDeviceConnected';  1 = 'DeviceConnected'
    2 = 'DeviceFailedEnumeration'; 3 = 'DeviceGeneralFailure'
    4 = 'DeviceCausedOvercurrent'; 5 = 'DeviceNotEnoughPower'
    6 = 'DeviceNotEnoughBandwidth'; 7 = 'DeviceHubNestedTooDeeply'
    8 = 'DeviceInLegacyHub'; 9 = 'DeviceEnumerating'; 10 = 'DeviceReset'
}

# A composite audio device's configuration descriptor runs to several hundred
# bytes; the 500-byte request this script used until batch 13-H was under that
# for real specimens, and the shortfall would have been silent.
$CONFIG_BUF = 2048

function Get-DescriptorRequest([string]$path, [int]$port, [uint16]$wValue, [uint16]$wIndex, [int]$size) {
    # USB_DESCRIPTOR_REQUEST: ULONG ConnectionIndex + 8-byte setup packet + data
    $buf = New-Object byte[] 12
    [BitConverter]::GetBytes([uint32]$port).CopyTo($buf, 0)
    $buf[4] = 0x80                                     # bmRequest: device-to-host
    $buf[5] = 0x06                                     # bRequest: GET_DESCRIPTOR
    [BitConverter]::GetBytes($wValue).CopyTo($buf, 6)
    [BitConverter]::GetBytes($wIndex).CopyTo($buf, 8)
    [BitConverter]::GetBytes([uint16]($size - 12)).CopyTo($buf, 10)   # wLength
    return [UsbHubQuery]::Ioctl($path, $IOCTL_DESC_FROM_NC, $buf, $size)
}

function Get-ChildHubName([string]$path, [int]$port) {
    # USB_NODE_CONNECTION_NAME: ULONG ConnectionIndex, ULONG ActualLength,
    # WCHAR NodeName[]. Only a port with a hub behind it answers. This is the
    # link that makes the flat list below a tree - and it is load-bearing rather
    # than cosmetic: both known multi-TT specimens are 05E3:0610 and the strings
    # do not discriminate either (measured, both units report
    # GenesysLogic / "USB2.1 Hub" with no serial), so a hub's position in the
    # tree is the only thing in this output that identifies which unit it is.
    $buf = New-Object byte[] 8
    [BitConverter]::GetBytes([uint32]$port).CopyTo($buf, 0)
    $r = [UsbHubQuery]::Ioctl($path, $IOCTL_NODE_CONN_NAME, $buf, 520)
    if ($null -eq $r) { return $null }
    # ActualLength counts the whole structure, not just the name, so the name is
    # ActualLength - 8 bytes long and the bound to check it against is Returned
    # itself. Reading it as a name length silently rejected every hub.
    $len = [BitConverter]::ToUInt32($r, 4)
    if ($len -lt 10 -or $len -gt [UsbHubQuery]::Returned) { return $null }
    return ('\\?\' + [System.Text.Encoding]::Unicode.GetString($r, 8, $len - 8).TrimEnd([char]0))
}

function Get-DeviceString([string]$path, [int]$port, [int]$index) {
    # Index 0 is the LANGID array rather than a string, and a device with no
    # string for a field reports index 0 there.
    if ($index -le 0) { return $null }
    $r = Get-DescriptorRequest $path $port ([uint16](0x0300 -bor $index)) ([uint16]0x0409) 268
    if ($null -eq $r) { return $null }
    $len = $r[12]
    if ($len -lt 4 -or $r[13] -ne 3) { return $null }   # bLength / bDescriptorType STRING
    $len = [Math]::Min($len, [UsbHubQuery]::Returned - 12)
    if ($len -lt 4) { return $null }
    return [System.Text.Encoding]::Unicode.GetString($r, 14, $len - 2).Trim()
}

function Get-ConfigDescriptor([string]$path, [int]$port, [int]$index) {
    # A device may carry more than one configuration, and reading only index 0
    # can miss the one that matters. A UAC 2.0 audio device often ships a UAC
    # 1.0 fallback configuration, and on Windows 98 and Windows 2000 - whose
    # in-box audio drivers are UAC 1.0 only - that fallback is the difference
    # between a device whose isochronous endpoints are ever opened and one whose
    # descriptors merely look promising. Read every configuration and let the
    # reader see which is which.
    $r = Get-DescriptorRequest $path $port ([uint16](0x0200 -bor $index)) ([uint16]0) $CONFIG_BUF
    if ($null -eq $r) { return $null }
    if ($r[12] -lt 9 -or $r[13] -ne 2) { return $null }   # bLength / bDescriptorType CONFIGURATION
    $total = [BitConverter]::ToUInt16($r, 12 + 2)
    $have  = [Math]::Max([UsbHubQuery]::Returned - 12, 0)

    $items = @()
    $off = 12 + $r[12]
    $end = 12 + [Math]::Min($total, $have)
    while ($off + 2 -le $end -and $r[$off] -gt 0 -and $off + $r[$off] -le $end) {
        switch ($r[$off + 1]) {
            4 {
                # INTERFACE: bInterfaceNumber, bAlternateSetting, bNumEndpoints,
                # bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol
                $items += [pscustomobject]@{
                    Kind       = 'Interface'
                    Interface  = $r[$off + 2]
                    Alt        = $r[$off + 3]
                    NumEp      = $r[$off + 4]
                    Class      = $r[$off + 5]
                    SubClass   = $r[$off + 6]
                    Protocol   = $r[$off + 7]
                }
            }
            5 {
                # ENDPOINT: bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
                $items += [pscustomobject]@{
                    Kind       = 'Endpoint'
                    Address    = $r[$off + 2]
                    Attributes = $r[$off + 3]
                    MaxPacket  = [BitConverter]::ToUInt16($r, $off + 4)
                    BInterval  = $r[$off + 6]
                }
            }
            11 {
                # INTERFACE ASSOCIATION: bFirstInterface, bInterfaceCount,
                # bFunctionClass, bFunctionSubClass, bFunctionProtocol. Its
                # presence is what "IAD-grouped" means - task 9-V.2 measured the
                # QEMU passthrough rung shut for IAD-grouped multi-interface
                # functions, so it is a property worth reading off a device
                # before it is carried anywhere.
                $items += [pscustomobject]@{
                    Kind       = 'IAD'
                    First      = $r[$off + 2]
                    Count      = $r[$off + 3]
                    Class      = $r[$off + 4]
                    SubClass   = $r[$off + 5]
                    Protocol   = $r[$off + 6]
                }
            }
        }
        $off += $r[$off]
    }

    return [pscustomobject]@{
        Items     = $items
        Total     = $total
        Have      = $have
        Truncated = ($total -gt $have)
        Value     = $r[12 + 5]   # bConfigurationValue
    }
}

function Format-Period([int]$bInterval, [int]$xfer, [int]$speed) {
    # USB 2.0 section 9.6.6. The unit bInterval is expressed in is a property of
    # the speed AND of the transfer type, so it cannot be decoded from the
    # descriptor alone - which is one more reason the reading is taken in the
    # rig: a child's negotiated speed is a property of the tree it sits in.
    if ($xfer -ne 1 -and $xfer -ne 3) { return '' }
    if ($bInterval -eq 0) { return '  (bInterval 0 - out of spec for this endpoint type)' }
    if ($speed -ge 2) {
        # High/Super Speed, isochronous and interrupt alike: 2^(bInterval-1)
        # microframes. This is the value task 9-A.2's Endpoint Context Interval
        # derivation consumes.
        $uframes = [Math]::Pow(2, $bInterval - 1)
        return ('  = {0} microframes = {1} ms' -f $uframes, ($uframes / 8))
    }
    if ($xfer -eq 1) {
        # Full-Speed isochronous: 2^(bInterval-1) frames.
        $frames = [Math]::Pow(2, $bInterval - 1)
        return ('  = {0} frames = {0} ms' -f $frames)
    }
    # Full/Low-Speed interrupt: bInterval is frames directly.
    return ('  = {0} frames = {0} ms' -f $bInterval)
}

function Write-ConfigDetail([string]$path, [int]$port, [int]$speed, [string]$indent, [int]$numConfigs) {
    for ($ci = 0; $ci -lt [Math]::Max($numConfigs, 1); $ci++) {
        Write-OneConfig $path $port $speed $indent $ci $numConfigs
    }
}

function Write-OneConfig([string]$path, [int]$port, [int]$speed, [string]$indent, [int]$index, [int]$numConfigs) {
    $cfg = Get-ConfigDescriptor $path $port $index
    if ($null -eq $cfg) {
        Write-Output ("{0}(configuration {1} unreadable, Win32 error {2})" -f $indent, $index, [UsbHubQuery]::Win32Error)
        return
    }
    if ($numConfigs -gt 1) {
        Write-Output ("{0}--- configuration index {1} (bConfigurationValue {2}) of {3} ---" -f `
            $indent, $index, $cfg.Value, $numConfigs)
    }
    if ($cfg.Truncated) {
        Write-Output ("{0}WARNING: configuration descriptor truncated (wTotalLength={1}, got {2}) - the list below is incomplete" -f `
            $indent, $cfg.Total, $cfg.Have)
    }

    $ifaceNums = @($cfg.Items | Where-Object { $_.Kind -eq 'Interface' } | ForEach-Object { $_.Interface } | Sort-Object -Unique)
    $iads      = @($cfg.Items | Where-Object { $_.Kind -eq 'IAD' })
    $iadText   = if ($iads.Count -gt 0) { "IAD-grouped ({0} association(s))" -f $iads.Count } else { 'no IAD' }
    Write-Output ("{0}config: {1} interface(s) [{2}], {3}" -f `
        $indent, $ifaceNums.Count, ($ifaceNums -join ','), $iadText)

    foreach ($it in $cfg.Items) {
        switch ($it.Kind) {
            'IAD' {
                Write-Output ("{0}  IAD: interfaces {1}..{2}, function class {3:X2}/{4:X2} proto {5}" -f `
                    $indent, $it.First, ($it.First + $it.Count - 1), $it.Class, $it.SubClass, $it.Protocol)
            }
            'Interface' {
                Write-Output ("{0}  interface {1} alt {2}: {3} endpoints, class {4:X2}/{5:X2} proto {6}" -f `
                    $indent, $it.Interface, $it.Alt, $it.NumEp, $it.Class, $it.SubClass, $it.Protocol)
            }
            'Endpoint' {
                $xfer = $it.Attributes -band 0x3
                $dir  = if (($it.Address -band 0x80) -ne 0) { 'IN ' } else { 'OUT' }
                Write-Output ("{0}    ep 0x{1:X2} {2} {3} maxpkt={4} bInterval={5}{6}" -f `
                    $indent, $it.Address, $dir, $xferName[[int]$xfer], $it.MaxPacket, `
                    $it.BInterval, (Format-Period $it.BInterval $xfer $speed))
            }
        }
    }
}

# The packed summary fields, decoded in one place because two callers need
# them: the report loop below and the socket-walk mode above it.
# USB_NODE_CONNECTION_INFORMATION_EX is fully packed, so ConnectionStatus is at
# 31 and DeviceAddress at 25 - the naturally-aligned offsets are wrong and read
# as "no device connected" on every port, silently, which is why these live
# here rather than being written out twice. Every other offset the report reads
# stays inline there, because only the report reads it.
function Read-PortSummary([byte[]]$c) {
    return [PSCustomObject]@{
        Status = [BitConverter]::ToUInt32($c, 31)
        Speed  = $c[23]
        IsHub  = $c[24]
        Addr   = [BitConverter]::ToUInt16($c, 25)
        Vid    = [BitConverter]::ToUInt16($c, 4 + 8)
        Pid    = [BitConverter]::ToUInt16($c, 4 + 10)
    }
}

function Get-HubLabel([string]$path) {
    # The interface path is long and mostly noise in a transcript; the VID&PID
    # (or root_hubNN) plus the instance is what names a physical unit, and the
    # instance is what separates two units of one VID:PID - which is ordinary
    # in this fleet, three 05E3:0610 among them.
    if ($path -match 'vid_([0-9a-f]{4})&pid_([0-9a-f]{4})#([^#]+)') {
        return ('{0}:{1} [{2}]' -f $Matches[1].ToUpper(), $Matches[2].ToUpper(), $Matches[3])
    }
    if ($path -match '(root_hub[0-9]*)#([^#]+)') {
        return ('{0} [{1}]' -f $Matches[1], $Matches[2])
    }
    return $path
}

function Get-PortSignature([string]$path, [int]$p) {
    $inb = New-Object byte[] 4
    [BitConverter]::GetBytes([uint32]$p).CopyTo($inb, 0)
    $c = [UsbHubQuery]::Ioctl($path, $IOCTL_CONN_INFO_EX, $inb, 512)
    # A failed IOCTL gets its own signature rather than being folded into
    # "empty": a hub unplugged mid-walk would otherwise print as every one of
    # its children leaving, which is a reading rather than an error message.
    if ($null -eq $c) { return ('?ioctl-failed({0})' -f [UsbHubQuery]::Win32Error) }
    $s = Read-PortSummary $c
    if ($s.Status -eq 0) { return '' }
    if ($s.Status -ne 1) {
        $n = $connStatus[[int]$s.Status]
        if (-not $n) { $n = "unknown($($s.Status))" }
        return ('** {0} **' -f $n)
    }
    $t = '{0:X4}:{1:X4} addr={2} speed={3}' -f $s.Vid, $s.Pid, $s.Addr, $speedName[[int]$s.Speed]
    if ($s.IsHub -ne 0) { $t += ' HUB' }
    return $t
}

# ---------------------------------------------------------------------------
# Socket-walk mode.
#
# The report below answers "what is behind this hub". It cannot answer "which
# physical socket is logical port 3", and no amount of reading will make it:
# that correspondence is a fact about the plastic, and only the operator, with
# the hub in hand, can supply it. The two are routinely assumed to agree, and
# on 1A40:0201 they disagree completely - its numbering runs opposite to its
# physical order - so the assumption is not merely unverified here, it is known
# false on the hub that stands in position T.
#
# Batch 13-E's first bench session was blocked twice by exactly this question
# (docs/contributing/runs/run-13e.md, "Two socket maps that do not exist, and
# were needed twice"), and stage E3's readings are per-child and must be
# comparable across a hub swap, so they cannot be taken until the maps are
#settled.
# Settling one by re-running the full report once per socket costs a run and a
# by-hand correlation per socket - eighteen of each for the three hubs in the
# bag. This mode instead watches every port and numbers each arrival, so the
# operator walks ONE device down the sockets in physical order and the numbered
# arrivals ARE the map: the physical order is supplied by the walking itself
# rather than by a note taken beside it, which is the part that went wrong.
#
# It reports the LOGICAL port, which is what appears in a route string, in a
# driver trace, and in the report below. The moulded numbers on the case are
# not evidence of anything.
#
# **The transition branches fired for the first time in the socket-map walks**, on
# 1A40:0201, and the arrival branch was wrong - see the three-way `if` below.
# It counted every non-empty signature, so the ** DeviceEnumerating ** state a
# plug passes through was numbered as a socket of its own: twelve numbers for
# seven sockets, and not even uniformly, since two of the seven transients fell
# between polls. That walk's map was still readable, but only because this
# hub's ports run in a monotone sequence - on a hub whose numbering is
# scrambled the summary would have been unreadable and nothing would have said
# so. The lesson worth keeping: **the first real use of a mode is its first
# test, and a walk whose arrival count does not equal the socket count walked
# is a failed run, not a curiosity.** Check that count before recording a map.
if ($Walk) {
    $paths = @([UsbHubQuery]::HubPaths())
    if ($Hub) {
        $needle = $Hub
        if ($needle -match '^([0-9A-Fa-f]{4})[:_&]?([0-9A-Fa-f]{4})$') {
            $needle = 'vid_{0}&pid_{1}' -f $Matches[1], $Matches[2]
        }
        $paths = @($paths | Where-Object { $_ -like "*$needle*" })
        if ($paths.Count -eq 0) {
            Write-Output "No hub interface path matches '$Hub'. Hubs present:"
            foreach ($h in [UsbHubQuery]::HubPaths()) { Write-Output ("  {0}" -f (Get-HubLabel $h)) }
            return
        }
    }

    # Port counts are read once: a hub's port count does not change, and a hub
    # that cannot be read here is one to drop rather than one to retry on every
    # poll for as long as the walk lasts.
    $portCount = @{}
    foreach ($h in $paths) {
        $ni = [UsbHubQuery]::Ioctl($h, $IOCTL_NODE_INFO, (New-Object byte[] 96), 96)
        if ($null -eq $ni) {
            Write-Output ("skipping {0} - IOCTL_USB_GET_NODE_INFORMATION failed, Win32 error {1}" -f (Get-HubLabel $h), [UsbHubQuery]::Win32Error)
            continue
        }
        $portCount[$h] = [int]$ni[4 + 2]
    }
    $paths = @($paths | Where-Object { $portCount.ContainsKey($_) })
    if ($paths.Count -eq 0) { Write-Output "No hub could be read."; return }

    Write-Output ""
    Write-Output ("socket walk - {0} hub(s), polling every {1} ms" -f $paths.Count, $PollMs)
    foreach ($h in $paths) {
        Write-Output ("  {0}  ({1} ports)" -f (Get-HubLabel $h), $portCount[$h])
    }

    $state = @{}
    $initial = @{}
    Write-Output ""
    Write-Output "Occupied before the walk starts. This is NOT the map - the numbered arrivals are:"
    $anyInitial = $false
    foreach ($h in $paths) {
        for ($i = 1; $i -le $portCount[$h]; $i++) {
            $s = Get-PortSignature $h $i
            $state["$h|$i"] = $s
            $initial["$h|$i"] = $s
            if ($s) {
                $anyInitial = $true
                Write-Output ("  {0} port {1}: {2}" -f (Get-HubLabel $h), $i, $s)
            }
        }
    }
    if (-not $anyInitial) { Write-Output "  (nothing)" }

    Write-Output ""
    Write-Output "Walk ONE device down the sockets in physical order, starting from an end of the"
    Write-Output "case you can name, and leave it in each socket until its arrival prints."
    Write-Output "Ctrl+C ends the run and prints the summary."
    Write-Output ""

    $n = 0
    $arrivals = New-Object System.Collections.ArrayList
    # Per-port handle on the arrival record, so the settle branch can
    # amend it without scanning the list.
    $lastArrival = @{}
    # Ports currently in an IOCTL-failure episode - see the branch below.
    $errored = @{}
    $deadline = [DateTime]::MaxValue
    if ($Seconds -gt 0) { $deadline = (Get-Date).AddSeconds($Seconds) }
    try {
        while ((Get-Date) -lt $deadline) {
            foreach ($h in $paths) {
                for ($i = 1; $i -le $portCount[$h]; $i++) {
                    $k = "$h|$i"
                    $now = Get-PortSignature $h $i

                    # A FAILED IOCTL IS NOT A READING, and must not reach the
                    # transition logic below. Get-PortSignature gives it a
                    # signature of its own rather than folding it into "empty",
                    # which is right - a hub unplugged mid-walk would otherwise
                    # print as every child leaving - but that signature is
                    # non-empty, so on the empty -> occupied rule below it
                    # counted as an ARRIVAL. Pulling a 4-port hub out at the
                    # end of a walk appended four of them (
                    # 1A40:0101, arrivals #5-#8, all ?ioctl-failed(2) =
                    # ERROR_FILE_NOT_FOUND). Same mistake as the transient
                    # below, one level down: a signature that is not a device
                    # was read as one.
                    #
                    # So an episode is printed once per port, and $state is
                    # left holding the last REAL signature - which means a hub
                    # that comes back is compared against what was actually in
                    # its ports before it went, and a device swapped while the
                    # hub was unreadable still reads as the arrival it is.
                    if ($now -and $now.StartsWith("?ioctl-failed")) {
                        if (-not $errored[$k]) {
                            $errored[$k] = $true
                            Write-Output ("[{0}]      {1} port {2}  ! {3}" -f (Get-Date).ToString("HH:mm:ss"), (Get-HubLabel $h), $i, $now)
                        }
                        continue
                    }
                    if ($errored[$k]) { $errored.Remove($k) }

                    if ($now -eq $state[$k]) { continue }
                    $was = $state[$k]
                    $state[$k] = $now
                    $stamp = (Get-Date).ToString('HH:mm:ss')
                    # THREE branches, not two, and the third is why: a plug is
                    # seen as EMPTY -> ** DeviceEnumerating ** -> the settled
                    # identity, so counting every non-empty signature as an
                    # arrival numbers one socket twice. Worse, it does not do so
                    # consistently - whether the transient is caught at all
                    # depends on a poll landing inside it, so the first real
                    # walk (1A40:0201) produced TWELVE numbers for
                    # SEVEN sockets, five doubled and two not. #N is the one
                    # thing this mode exists to produce, so an arrival is now
                    # strictly the EMPTY -> occupied edge, and a change between
                    # two occupied signatures is a settle that amends the
                    # arrival already recorded rather than adding one.
                    if (-not $now) {
                        Write-Output ("[{0}]      {1} port {2}  - gone ({3})" -f $stamp, (Get-HubLabel $h), $i, $was)
                        $lastArrival.Remove($k)
                    } elseif ($was) {
                        # The settle. Printed, because it carries the identity
                        # and because ** DeviceFailedEnumeration ** arrives this
                        # way too - but with no number, and the arrival it
                        # belongs to is amended in place so the summary names
                        # the device rather than the transient it was first
                        # seen as.
                        Write-Output ("[{0}]      {1} port {2}  = {3}" -f $stamp, (Get-HubLabel $h), $i, $now)
                        if ($lastArrival.ContainsKey($k)) { $lastArrival[$k].What = $now }
                    } else {
                        $n++
                        Write-Output ("[{0}] #{1,-3} {2} port {3}  + {4}" -f $stamp, $n, (Get-HubLabel $h), $i, $now)
                        $rec = [PSCustomObject]@{ N = $n; Hub = (Get-HubLabel $h); Port = $i; What = $now }
                        [void]$arrivals.Add($rec)
                        # Keyed by port, so a settle amends THIS port's arrival
                        # rather than the most recent one anywhere: a walk is
                        # one device at a time, but -Hub omitted watches every
                        # hub, and a second device may move on another.
                        $lastArrival[$k] = $rec
                    }
                }
            }
            Start-Sleep -Milliseconds $PollMs
        }
    } finally {
        # In a finally block so that Ctrl+C - the ordinary way to end a walk -
        # still prints the summary the walk was run for.
        #
        # **Write-Host throughout, and that is the whole point of the block**
        # (pre-cut audit, finding B5). After Ctrl+C the pipeline is
        # already stopping, so the FIRST Write-Output here throws
        # PipelineStoppedException and every line below it dies with it -
        # reproduced on this host. -Seconds defaults to walk-until-Ctrl+C, so
        # that is not an edge case: it is the default invocation, discarding the
        # one artifact the mode exists to produce. Write-Host does not write to
        # the pipeline and survives. Do not "tidy" these back.
        Write-Host ""
        Write-Host "Arrivals in order. #N is the Nth socket walked into, so this is the map:"
        if ($arrivals.Count -eq 0) {
            Write-Host "  (none - nothing was plugged in during the walk)"
        } else {
            foreach ($a in $arrivals) {
                Write-Host ("  #{0,-3} {1} port {2}   {3}" -f $a.N, $a.Hub, $a.Port, $a.What)
            }
        }
        # The coverage check, added after the 05E3:0608 walk came
        # back with six arrivals for seven sockets. The cause was benign - the
        # drive was already in the first socket, so that socket was listed as
        # pre-occupied instead of being numbered - and the map was recoverable
        # by hand. Recovering a map by hand is exactly what this mode exists to
        # stop, so the invariant is now checked and printed rather than left to
        # whoever reads the transcript: every watched port should appear exactly
        # once above, and anything that does not is named with the reason.
        Write-Host ""
        Write-Host "Coverage. Every socket walked should appear exactly once in the list above;"
        Write-Host "anything named here did not, and the map is incomplete until each is explained:"
        $totalPorts = 0
        $gaps = @()
        foreach ($h in $paths) {
            $totalPorts += $portCount[$h]
            for ($i = 1; $i -le $portCount[$h]; $i++) {
                $hits = @($arrivals | Where-Object { $_.Hub -eq (Get-HubLabel $h) -and $_.Port -eq $i })
                if ($hits.Count -eq 1) { continue }
                if ($hits.Count -gt 1) {
                    $why = "{0} arrivals - was this socket walked into twice?" -f $hits.Count
                } elseif ($initial["$h|$i"]) {
                    # The common one, and it is the operator's to fix: start
                    # with the device OUT of the hub, or this socket has no
                    # number and the ones after it are off by one.
                    $why = "no arrival - OCCUPIED when the walk started, by {0}" -f $initial["$h|$i"]
                } else {
                    $why = "no arrival - never walked into, or the plug was shorter than one poll"
                }
                $gaps += ("  {0} port {1}: {2}" -f (Get-HubLabel $h), $i, $why)
            }
        }
        if ($gaps.Count -eq 0) {
            Write-Host ("  (none - {0} arrivals for {1} ports watched)" -f $arrivals.Count, $totalPorts)
        } else {
            foreach ($g in $gaps) { Write-Host $g }
            Write-Host ("  {0} arrival(s) for {1} port(s) watched." -f $arrivals.Count, $totalPorts)
            Write-Host "  A port listed as an internal link between two chips of one enclosure is"
            Write-Host "  expected here and is not a gap; anything else means re-walk it."
        }

        Write-Host ""
        Write-Host 'Record it against a named physical end ("counting from the cable end"), and'
        Write-Host 'assign H1-H4 from the LOGICAL port numbers - build-and-test.md, "A hub socket'
        Write-Host 'is not a hub port, and the two must be mapped".'
    }
    return
}

foreach ($path in [UsbHubQuery]::HubPaths()) {

    $ni = [UsbHubQuery]::Ioctl($path, $IOCTL_NODE_INFO, (New-Object byte[] 96), 96)
    if ($null -eq $ni) {
        Write-Output ""
        Write-Output "HUB $path"
        Write-Output "  (IOCTL_USB_GET_NODE_INFORMATION failed, Win32 error $([UsbHubQuery]::Win32Error))"
        continue
    }

    # USB_NODE_INFORMATION: ULONG NodeType, then USB_HUB_DESCRIPTOR (packed) at +4
    $nodeType  = [BitConverter]::ToUInt32($ni, 0)
    $numPorts  = $ni[4 + 2]
    $hubChar   = [BitConverter]::ToUInt16($ni, 4 + 3)
    $ttt       = ($hubChar -shr 5) -band 0x3

    Write-Output ""
    Write-Output "HUB $path"

    # Layout self-check: a hub descriptor is bLength 9, bDescriptorType 0x29.
    if ($ni[4] -ne 9 -or $ni[5] -ne 0x29) {
        Write-Output ("  hub-descriptor layout check FAILED (bLength={0} bType=0x{1:X2}) - values below are not trustworthy" -f $ni[4], $ni[5])
    }
    # bPowerOnToPowerGood is in 2 ms units; bHubControlCurrent is in mA.
    $pwr2good = $ni[4 + 5]
    $ctrlCurrent = $ni[4 + 6]

    Write-Output ("  NodeType={0}  Ports={1}  wHubCharacteristics=0x{2:X4}  TTT={3} ({4} FS bit times)  PwrOn2PwrGood={5}ms  HubCurrent={6}mA" -f `
        $nodeType, $numPorts, $hubChar, $ttt, (8 * ($ttt + 1)), (2 * $pwr2good), $ctrlCurrent)

    # USB_HUB_INFORMATION declares a BOOLEAN HubIsBusPowered after the 71-byte
    # packed descriptor, i.e. at 4 + 71 = 75. Do NOT read it: measured
    # As of batch 7b, this driver leaves every byte past offset 12 zero, so that
    # field reads 0 ("self-powered") on every hub including bus-powered ones -
    # a zero-by-construction reading, not a measurement. Determine hub power
    # from whether it has a barrel jack.
    #
    # And it is not just that one field. Measured on 1A40:0201:
    # connecting the barrel jack changed **nothing** in this script's entire
    # output - same wHubCharacteristics, same bHubControlCurrent, same device
    # address, no re-enumeration. bHubControlCurrent is what the hub's own
    # controller draws, not the per-port budget, so it is static either way.
    # The difference self-power makes is the current available to children, and
    # that is only visible as a brown-out under a hungry child. So the power
    # state is not derivable from any reading here and has to be written into
    # the characterisation record by hand.

    for ($p = 1; $p -le $numPorts; $p++) {
        $inb = New-Object byte[] 4
        [BitConverter]::GetBytes([uint32]$p).CopyTo($inb, 0)
        $c = [UsbHubQuery]::Ioctl($path, $IOCTL_CONN_INFO_EX, $inb, 512)
        if ($null -eq $c) {
            Write-Output ("  port {0}: IOCTL failed, Win32 error {1}" -f $p, [UsbHubQuery]::Win32Error)
            continue
        }

        if ($env:HUBDEBUG) {
            $hex = ($c[0..47] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
            Write-Output ("  port {0} raw[0..47]: {1}" -f $p, $hex)
        }

        # USB_NODE_CONNECTION_INFORMATION_EX is fully packed: the USHORT
        # DeviceAddress at 25 is unaligned, so ConnectionStatus lands at 31 and
        # PipeList at 35. Confirmed against raw bytes on this host - the first
        # USB_PIPE_INFO's endpoint descriptor (bLength 7, bDescriptorType 5)
        # appearing at 35 is what pins every field before it.
        $sum = Read-PortSummary $c
        $status = $sum.Status
        if ($status -eq 0) { continue }   # nothing plugged in - the only silent case
        if ($status -ne 1) {
            $sName = $connStatus[[int]$status]
            if (-not $sName) { $sName = "unknown ($status)" }
            Write-Output ("  port {0}: ** {1} ** - a device is present here and did not enumerate" -f $p, $sName)
            continue
        }

        # Layout check: device descriptor sits at +4 and must be bLength 18, type 1.
        if ($c[4] -ne 18 -or $c[5] -ne 1) {
            Write-Output ("  port {0}: layout check FAILED (bLength={1} bType={2}) - do not trust" -f $p, $c[4], $c[5])
            continue
        }

        $bDeviceClass    = $c[4 + 4]
        $bDeviceSubClass = $c[4 + 5]
        $bDeviceProtocol = $c[4 + 6]
        $vid  = $sum.Vid
        $prod = $sum.Pid
        $bcdUSB = [BitConverter]::ToUInt16($c, 4 + 2)
        # bMaxPacketSize0 - device descriptor offset 7, so 4 + 7 here. Added
        # because batch 13-E's first bench session made it
        # load-bearing: two Creative Full-Speed audio units do not enumerate at
        # all on xhci98.sys while an independent-vendor Full-Speed unit does,
        # and this is the one field that varies ONLY at Full Speed (8/16/32/64,
        # unknowable until the descriptor is read). It is what decides whether a
        # device exercises the driver's Evaluate Context correction, which LS
        # (always 8) and HS (always 64) never reach.
        $bMaxPacketSize0 = $c[4 + 7]
        $iManufacturer = $c[4 + 14]
        $iProduct      = $c[4 + 15]
        $iSerial       = $c[4 + 16]
        $numConfigs    = $c[4 + 17]
        $speed  = $sum.Speed
        $isHub  = $sum.IsHub
        $addr   = $sum.Addr
        $nPipes = [BitConverter]::ToUInt32($c, 27)

        # Second layout witness: with pipes open, PipeList[0]'s endpoint
        # descriptor must be at 35 with bLength 7 / bDescriptorType 5.
        if ($nPipes -gt 0 -and ($c[35] -ne 7 -or $c[36] -ne 5)) {
            Write-Output ("  port {0}: pipe-list layout check FAILED - offsets not trustworthy" -f $p)
        }

        # USB_NODE_CONNECTION_INFORMATION_EX **reports a SuperSpeed device as
        # UsbHighSpeed** - measured, when a drive declaring bcdUSB
        # 0320, carrying 1024-byte bulk endpoints and offering a UAS alternate
        # setting came back "High"; every SuperSpeed hub read that evening was
        # labelled the same way. usbview uses
        # IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 to separate the two.
        # **That request was tried here and refused**: 0x22045C returns
        # ERROR_INVALID_PARAMETER from this handle at every buffer size and
        # Length combination, while an unimplemented control code returns
        # ERROR_NOT_SUPPORTED - so the code exists and wants something the
        # handle does not have. Rather than ship a probe that never fires, the
        # ambiguity is *named* where it occurs.
        #
        # It is an annotation and not a verdict because the script genuinely
        # cannot tell the two cases apart, and they are opposites: a USB 3.x
        # device operating at SuperSpeed, versus one that fell back to its USB
        # 2.0 companion path - which is exactly the observation the rig wants
        # taken once in position D. **The configuration descriptor below is the
        # discriminator**, because a device presents a different one per speed:
        # 1024-byte bulk endpoints and a UAS alternate setting are what the
        # SuperSpeed path looks like, 512-byte and BOT-only is the USB 2.0 one.
        # Read those lines rather than this field.
        $speedText = $speedName[[int]$speed]
        if ($bcdUSB -ge 0x0300 -and $speed -eq 2) {
            $speedText += '?(bcdUSB>=3.00 - Super or fell back; read the endpoints)'
        }

        $line = "  port {0}: {1:X4}:{2:X4} addr={3} bcdUSB={4:X4} speed={5} class={6:X2}/{7:X2} proto={8} mps0={9}" -f `
            $p, $vid, $prod, $addr, $bcdUSB, $speedText, $bDeviceClass, $bDeviceSubClass, $bDeviceProtocol, $bMaxPacketSize0
        if ($isHub -ne 0) { $line += "  HUB -> $($protoName[[int]$bDeviceProtocol])" }
        Write-Output $line
        # speed 1 is UsbFullSpeed in this enumeration. A Full-Speed device whose
        # bMaxPacketSize0 is not 8 is addressed at 8 and then corrected by an
        # Evaluate Context; one that already reads 8 never takes that path. Call
        # it out rather than leaving it to be spotted in a column.
        if ($speed -eq 1) {
            if ($bMaxPacketSize0 -ne 8) {
                Write-Output ("    ^ Full Speed with mps0={0} - EXERCISES the driver's EP0 max-packet correction" -f $bMaxPacketSize0)
            } else {
                Write-Output "    ^ Full Speed with mps0=8 - does NOT exercise the EP0 max-packet correction"
            }
        }

        if ($isHub -ne 0) {
            $childName = Get-ChildHubName $path $p
            if ($childName) { Write-Output ("      its own HUB entry below: {0}" -f $childName) }
        }

        # Two units of one VID:PID are ordinary in this fleet - both known
        # multi-TT specimens are 05E3:0610 - so the strings are what let a row
        # of the characterisation record name a physical unit and not a model.
        $mfg = Get-DeviceString $path $p $iManufacturer
        $pn  = Get-DeviceString $path $p $iProduct
        $sn  = Get-DeviceString $path $p $iSerial
        if ($mfg -or $pn -or $sn) {
            Write-Output ("      id: mfg='{0}' product='{1}' serial='{2}' bNumConfigurations={3}" -f $mfg, $pn, $sn, $numConfigs)
        }

        Write-ConfigDetail $path $p ([int]$speed) '      ' ([int]$numConfigs)
    }
}
