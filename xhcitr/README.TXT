XHCITR - xHCI98 USB Tree
========================

What it is for
--------------

The xHCI98 USB Tree shows every USB controller in the computer, its hubs,
ports and devices, and every Thunderbolt 3 or USB4 controller driven by
the xhci98 Thunderbolt driver, with the devices connected through it. It
also stops a USB stick or disk, or a device connected through a
Thunderbolt or USB4 port, safely, so that you can unplug it, and it sits
in the notification area (the system tray) beside the clock.

The one driver setting it changes is the drivers' logging level
(below). It approves no Thunderbolt device and updates no firmware. It
needs no administrator rights to start.

Which file
----------

  XHCITR.EXE     Windows 98 SE, ME, 2000, XP, 32-bit Vista and 7
  XHCITR64.EXE   Windows XP x64, Vista x64 and 7 x64

INSTALL.BAT copies the right one to the install directory it asks for
(C:\XHCI98 unless you choose another, or INSTALL /to path) and starts it
each time you log on. You can also run it from the package directly, or
from anywhere else: it does not care where it is.

The window
----------

The left side is the tree; the right side shows the details of what you
select. Under "Thunderbolt and USB4" are the Thunderbolt or USB4
controllers, their ports, the devices connected to each port, the
connections (tunnels) to each device, and the Windows devices behind
them. Under "USB" are the USB controllers, each with its root hub, ports,
hubs and devices, their speed and their Device Manager entry.

Each controller is named with its maker and PCI ID, for example
"... - Intel 8086:02ED"; the USB controller inside a Thunderbolt chip
reads "... - Thunderbolt 3 controller's USB (8086:15C1)". A root hub
shows its ports and how many are USB 2.0 and USB 3.x, for example "Root
hub - 18 ports (14 USB 2.0, 4 USB 3.x)", and each port its kind:
"[Port 15, USB 3]". A USB 3 connector has one port of each kind.

The details of each device end with its resources: the memory and I/O
ranges, the IRQ and (on Windows 2000 and later) the bus numbers Windows
gave it, and its PCI location. A device with a problem code also shows
what it asked for, so a device that wants an IRQ and was given none is
easy to spot.

  File > Refresh (F5)      reads everything again
  File > Save Report...    writes the whole tree with every detail, the
                           drivers' logs and the driver's snapshot to a
                           text file, for a problem report
  File > Save Snapshot...  writes the driver's snapshot files in a
                           folder you pick (below)
  Edit > Copy              copies the details (or the selected part)
  View                     shows or hides each part of the tree; with
                           Descriptors on, the selected USB device's
                           descriptors are read and shown in full
  View > Logging level...  the drivers' logging levels (below)
  Action > Safely Remove   stops the selected USB stick or disk, or
                           Thunderbolt or USB4 device
  Action > Properties      opens the device in Device Manager
                           (Windows 2000 and later)
  Action > Disk benchmark  starts the xHCI98 Disk Benchmark on that disk

A right click on the tree opens the Action menu. The window remembers its
size and the View settings.

Stopping a Thunderbolt or USB4 device
-------------------------------------

Select the device (or anything under it) and choose Action > Safely
Remove, or click the icon in the notification area and choose the device.
The tool lists the devices that will stop and asks you to confirm. It
then checks that nothing changed since you looked, asks Windows to stop
the device, and tells you when you can unplug it. If a program is still
using the device, Windows refuses and the tool tells you which.

Close the programs and windows that use the device first, as with
Windows' own Safely Remove Hardware.

Under the USB4 connection manager on Windows 2000 to 7, the stop also
closes the device's PCIe tunnel once the Thunderbolt driver has the stop
confirmed. Run as an administrator, the tool confirms it at once. Without
administrator rights on XP, Vista and 7 the xHCI98 mark service
confirms it in the background: the tool waits up to 30 seconds for that,
and if the tunnel is still up then it says the tunnel has not been seen
closed and closes if the mark service confirms the eject. On Windows
2000 without administrator rights, or with the mark service stopped,
nothing confirms it and the tunnel stays up until you unplug the
device.

Stopping a USB stick or disk
----------------------------

A USB device with a disk on it - a stick, a card reader, a USB or UAS
disk - can be stopped on every Windows, 98 SE and ME included. The tree
names it by its drive letter, for example "USB Flash Disk (F:)", and
marks it "[can be stopped]". Select it and choose Action > Safely Remove
(or press Delete, or right-click it), or click the icon and choose it
under "USB devices". The tool lists what will stop, asks you to confirm,
asks Windows to stop it and tells you when you can unplug it. If a
program has a file open on it, Windows refuses, nothing changes, and the
tool tells you which drive is in use.

A keyboard, mouse, hub, camera, Bluetooth adapter or any other USB
device without a disk is shown but not stopped from here.

On Windows 98 SE and ME the device leaves Device Manager when it is
stopped; until you unplug it, Device Manager's Refresh can start it
again. On Windows 2000 and later Windows holds it stopped until you
unplug it, and the tree shows it "[stopped - unplug it]".

On Windows Vista and 7, if Windows refuses because the device needs
administrator rights to stop, the tool offers to try again as an
administrator.

On Windows 2000 to 7, a Thunderbolt or USB4 device that Windows does not
mark removable (for example the first time it is plugged in) is missing
from Windows' own Safely Remove Hardware. The tool still stops it, with
administrator rights: when it runs without them it says so and offers
"Stop as administrator...", which asks for them (User Account Control on
Vista and 7, the Run As dialog on 2000 and XP) for that one stop. If you
cancel, nothing is stopped.
The drivers' logs
-----------------

Each xHCI controller the xhci98 driver runs has a "Driver log (xhci98)"
item under it, and each Thunderbolt or USB4 controller a "Driver log
(xhcicm)" item. Select one to read the driver's log and counters from the
running driver: whether it is recording, the counters that are not zero,
and the log, oldest line first. File > Save Report and /report include
both logs. Nothing in them is an internal address.

Where a log is off, the item says how to turn it on (the xhcicm
driver records by default):

  xhci98   set its level to 2 in View > Logging level... (below), then
           restart Windows.
  xhcicm   set the DWORD value XhciCmLogVerbosity to 2 under the key
           the item names (on Windows 2000 and later as an
           administrator), then restart Windows.

Reading a log changes nothing; Logging level (below) is the one driver
setting the tool writes.

The driver's snapshot
---------------------

The snapshot is the xhci98 driver's own report: a device report (each
xHCI controller's and this package's Device Manager entries, status,
problem code and resources, and the installed driver files' sizes,
versions and SHA-256 hashes) and a dump of the driver's state (its log,
the devices in their slots, each root port's enumeration and, at level
3, the port registers). It replaces XHCISNAP and its SNAPON.BAT,
SNAPGET.BAT and SNAPOFF.BAT, which the package no longer carries.

File > Save Report and /report end with the snapshot's text for each
xHCI controller the xhci98 driver runs, so the report alone is what a
problem report needs. At level 4 the dump can hold memory addresses, so
the report leaves it out and says so.

File > Save Snapshot... and XHCITR /snapshot write the snapshot's own
files, for when a maintainer asks for them:

  NAME.DEV   the device report
  NAME.TXT   the dump, as text
  NAME.BIN   the driver's raw state, for the maintainer
  NAME.PSC   the raw port registers

NAME is made from the date and time (for example 3141022A for 14 March
at 10:22), or is the 1 to 8 letter name you give; an earlier set is
never overwritten.

PCI configuration
-----------------

Each xHCI controller the xhci98 driver runs, and each Thunderbolt or USB4
controller, has a "PCI configuration" item. It shows, for each PCI
function, what Windows does not: the vendor, device, subsystem, revision
and class; the Command and Status registers; the interrupt pin and line;
the BARs as read; for a bridge its buses and its I/O, memory and
prefetchable windows; and its capabilities - MSI and MSI-X (enabled or
not, vectors or table size), the PCI Express link (speed and width now,
and what the link is capable of) and power management.

Under a Thunderbolt or USB4 controller the item lists the controller,
its tunnel ports and the devices behind each port. On Windows 98 SE and
ME the devices behind a port are not read and say so. Under an xHCI
controller it shows that controller. The drivers read configuration
space and never change it. File > Save Report and /report include it.

Logging level
-------------

View > Logging level... (or a right click on a USB controller driven by
xhci98, on a Thunderbolt or USB4 controller, or on either one's Driver
log) shows how much each of the two drivers records in its log, side by
side, and sets it:

  0  off
  1  counters only
  2  the log (default)
  3  adds port status
  4  adds memory addresses (for developers)

xhci98's level is the XhciLogVerbosity value in each of its USB
controllers' driver keys; the Thunderbolt driver's is XhciCmLogVerbosity
in its controller's driver key. From 3.1.0.0 both drivers read the same
value first from HKLM\System\CurrentControlSet\Services\USB, the
machine-wide key, and a value there wins for every controller; the list
shows that value under each driver's heading and, under each device whose
own value it overrides, the device's own value as not in force. The list
at the top shows each value as it is: no value reads as 2, the default,
and a value outside 0 to 4 is shown as out of range. Choose a level for
either driver and click Set; a value out of range is never written. Where
a machine-wide value is set, Set writes that one, the value in force for
every device, and leaves the devices' own values as they are. Each driver
reads its level only when it starts, so the tool then offers to restart
Windows.

On Windows 98 SE and ME, and for an administrator, Set writes the values
at once. On Windows 2000 to 7 without administrator rights the button
reads "Set as administrator..." and asks for them (User Account Control
on Vista and 7, the Run As dialog on 2000 and XP) for that one change.
If you cancel, nothing is changed.

The levels are also in Save Report and XHCITR /report.

The notification-area icon
--------------------------

  Click            a list of the devices you can stop: USB devices,
                   then Thunderbolt and USB4 devices
  Double-click     opens the window
  Right-click      Open, Refresh, About, Start at login, Tell me when a
                   device can run faster, and Exit

Start at login
--------------

To stop the icon starting when you log on, untick "Start at login" in
the View menu or the icon's right-click menu; tick it again to bring it
back. It is your own setting and needs no administrator rights. If the
item is grey ("off for this computer"), the program was installed with
INSTALL /notray, so it starts for nobody; run INSTALL again without
/notray to turn it back on.

The icon says when a Thunderbolt or USB4 device is connected, is waiting
to be approved, could not be approved or was disconnected, and when the
controller stops answering: in a balloon on Windows ME, 2000 and later,
and in the icon's tooltip on Windows 98 SE. Sleep, hibernation and a
stop of the controller are not reported as the controller not answering;
a controller that does not start again after a sleep is.

Windows 98 SE has no balloons, so there the tool also shows a pop-up of
its own beside the icon in their place, for each event a balloon
reports elsewhere and for the outcome of a removal started from the
icon. The pop-up takes no focus; it closes after 10 seconds, or by its
close box or a right click; a click on it does what a click on a
balloon does; several wait their turn.

Can run faster
--------------

When a USB 3 device runs slower than it can on a controller xhci98.sys
drives - on a USB 2.0 port, behind a USB 2.0 hub, fallen back to USB 2.0
in a USB 3 port, or a 10 Gbit/s device at 5 Gbit/s - or a High Speed
device runs at Full Speed behind a Full Speed hub (one not itself
behind a High Speed hub), a balloon names it
(the tooltip on Windows 98 SE) and the icon turns red until every such
device is reconnected at its speed or removed. Click the balloon, or the
first item of the icon's menu while it is red, for each device's port and
hub, the speed it runs at and the speed it can, and what to try. A hub is
never listed itself: the USB 2.0 half of a USB 3 hub always runs at High
Speed.

"Tell me when a device can run faster" in the View menu and the icon's
right-click menu turns this on or off for you. The installer's box of the
same name (or INSTALL /faster and /nofaster) sets it for the computer;
your own choice overrides that. It is on by default on every Windows:
Windows' own "can perform faster" notices do not appear under xhci98.sys.

From a command line
-------------------

  XHCITR                       the window and the icon
  XHCITR /tray                 the icon alone (as started at logon)
  XHCITR /report <file>        writes the report, the drivers' logs, PCI
                               configuration and the snapshot included,
                               to <file> and exits
  XHCITR /eject id=<id>        stops the USB device or Thunderbolt/USB4
                               device with that Device Manager instance
                               ID and exits
  XHCITR /setlevel xhci98=<n> xhcicm=<n>
                               sets either driver's logging level (0 to
                               4; one or both) and exits; needs
                               administrator rights on 2000 to 7
  XHCITR /snapshot [NAME] [/c N] [/nosysinfo]
                               writes the snapshot's four files in the
                               current directory (/c N: controller N,
                               default 0; /nosysinfo: without the
                               Windows, processor and BIOS details)
  XHCITR /dump [/c N] [/o NAME] [/nosysinfo]
                               writes the dump alone (NAME.TXT, .BIN and
                               .PSC), replacing an earlier set of NAME
  XHCITR /probe                checks the route to the driver and
                               writes nothing
  XHCITR /snapshot /?          the snapshot's long help

The snapshot commands show their text in a message box, or write it to
where their output is redirected.

Exit codes: 0 done, 1 refused (nothing changed), 2 failed, 4 Windows
refused or a step remains for you. /snapshot: 0 both files written in
full, 2 a name or switch refused, 3 to 8 as XHCITR /snapshot /? says.

Licence
-------

Free software under the GNU General Public License, version 2. See the
package's LICENSE and NOTICE files.
