XHCIMARK - the Thunderbolt safe-removal mark service
====================================================

What it is for
--------------

Windows lists a Thunderbolt or USB4 device behind this package's
connection manager in "Safely Remove Hardware" (2000: "Unplug or Eject
Hardware") because the driver's small filter on the device says it is
removable. Until 3.1.0.0 that filter was lost whenever Windows rebuilt the
device's drivers: its first plug's driver install, a disable and enable,
or a driver update. The device was then not listed until it was unplugged
and plugged again.

This service keeps the filter across those rebuilds. For each device the
driver reports behind one of its verified tunnel ports it adds the
driver's name, xhcicm, to the device's own LowerFilters value, which
Windows reads every time it builds the device's drivers. It removes the
name again when the device leaves. It touches nothing else.

Where it runs
-------------

Windows 2000 to 10, 32-bit and 64-bit, only when the Thunderbolt 3 / USB4
component is installed. Never on Windows 98 SE or ME.

  XHCIMARK.EXE  32-bit Windows 2000, XP, Vista, 7, 8, 8.1 and 10
  XHCIMK64.EXE  Windows XP x64, Vista x64, 7 x64, 8 x64, 8.1 x64 and 10 x64

The installer copies the right one into the Windows system directory
(System32), registers it as the service "xhcimark" (display name "xHCI98
Thunderbolt safe-removal mark service", the LocalSystem account, started
automatically) and starts it. The uninstall stops and deletes it, and
removes every mark it or an earlier copy wrote. You do not run it
yourself.

What it does
------------

  - About once a second it reads what the driver reports (the query
    device \.\XHCICM<n>) and checks each device against Windows: the
    tunnel port must be one the driver verified and that Windows has at
    that place, the device must be that port's child, and it must not be
    the Thunderbolt controller, a parent of it or the computer's own USB
    controller.
  - Before it adds the name it records the device under
    HKLM\SYSTEM\CurrentControlSet\Services\xhcimark\Marks, so a mark is
    never left behind unrecorded.
  - When the device leaves, or is no longer behind a verified port, the
    name is removed within a poll. When the driver is not answering, it
    waits: a device is never unmarked because the driver cannot be asked.
  - A device plugged in for the first time can be rebuilt by Windows
    before the mark is written. The service then restarts that one device
    once, the way Device Manager applies a property change. Windows asks
    the device's drivers first, so a disk with open files refuses, and the
    refusal stands: the device is then listed after its next start.
  - On Windows XP and later, when Windows holds a device for removal after
    you stopped it, the service tells the driver, which then tears its
    tunnel down (the USB4 connection manager).

Stor98 on a drive plugged in later (Windows 8.1)
------------------------------------------------

Windows 8.1 has its own NVMe driver, stornvme, and gives it to an NVMe
drive behind a Thunderbolt or USB4 tunnel that is plugged in after the
install; but 8.1's stornvme holds a write for good when such a drive is
unplugged without Safely Remove, so on 8.1 the tunnelled drive gets the
bundled Stor98 (Windows 10 keeps its own stornvme, which does not, and
the service moves nothing there). When the installer installed Stor98 on
8.1, it names the staged Stor98 in the setting
Stor98Inf below, and the service then moves each such drive to Stor98,
once, a second or two after Windows started it: it stops the drive (as
Device Manager's Disable does), installs Stor98 on that one device and
starts it again. An internal NVMe drive is never touched, nor a drive
with another maker's driver. A drive with a file open, a paging file or
Windows itself on it refuses the stop and stays on stornvme. A drive
moved to Stor98 is never moved again by the service, so a driver you
choose later for it stays. While the installer runs, the service waits.
Its records are under ...\Services\xhcimark\Stor98, one per drive.

The log
-------

%SystemRoot%\System32\LogFiles\xhci98\XHCIMARK.LOG, one line per notice,
check, write, removal and restart. Send it with a Thunderbolt report.

Settings
--------

In HKLM\SYSTEM\CurrentControlSet\Services\xhcimark\Parameters, all
optional:

  Restart    REG_DWORD. 1 (the default): restart a first plug the rebuild
             overtook. 0: never; such a device is listed after its next
             start.
  LogBytes   REG_DWORD. The log's size bound, 262144 by default.
  Stor98Inf  REG_SZ. The staged Stor98 INF (%SystemRoot%\INF\oemNN.inf)
             that drives on stornvme behind a tunnel are moved to.
             Written by the installer on Windows 8.1 when Stor98 is
             installed, and deleted on any other system; absent, or on
             any Windows but 8.1: no drive is moved.

For testing only
----------------

  XHCIMARK -selftest     checks its own decisions; touches nothing
  XHCIMARK -once         one poll in a console (an administrator)
  XHCIMARK -register     register and start it from System32
  XHCIMARK -unregister   stop and delete it

-register and -unregister are for a test machine before the installer
does it. Deleting the service leaves the marks it wrote; a mark naming a
driver that is no longer installed stops the device (Code 39), so remove
the package with its uninstall, which takes the marks off first.
