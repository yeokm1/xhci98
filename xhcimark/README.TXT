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

Windows 2000, XP, Vista and 7, 32-bit and 64-bit, only when the
Thunderbolt 3 / USB4 component is installed. Never on Windows 98 SE or ME.

  XHCIMARK.EXE  32-bit Windows 2000, XP, Vista and 7
  XHCIMK64.EXE  Windows XP x64, Vista x64 and 7 x64

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

The log
-------

%SystemRoot%\System32\LogFiles\xhci98\XHCIMARK.LOG, one line per notice,
check, write, removal and restart. Send it with a Thunderbolt report.

Settings
--------

In HKLM\SYSTEM\CurrentControlSet\Services\xhcimark\Parameters, both
REG_DWORD, both optional:

  Restart   1 (the default): restart a first plug the rebuild overtook.
            0: never; such a device is listed after its next start.
  LogBytes  the log's size bound, 262144 by default.

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
