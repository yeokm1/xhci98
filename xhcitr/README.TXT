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

It changes no setting, approves no Thunderbolt device and updates no
firmware. It needs no administrator rights to start.

Which file
----------

  XHCITR.EXE     Windows 98 SE, ME, 2000, XP, 32-bit Vista and 7
  XHCITR64.EXE   Windows XP x64, Vista x64 and 7 x64

INSTALL.BAT copies the right one to the XHCI98 folder in your Windows
directory and starts it each time you log on. You can also run it from
the package directly.

The window
----------

The left side is the tree; the right side shows the details of what you
select. Under "Thunderbolt and USB4" are the Thunderbolt or USB4
controllers, their ports, the devices connected to each port, the
connections (tunnels) to each device, and the Windows devices behind
them. Under "USB" are the USB controllers, each with its root hub, ports,
hubs and devices, their speed and their Device Manager entry.

  File > Refresh (F5)      reads everything again
  File > Save Report...    writes the whole tree with every detail to a
                           text file, for a problem report
  Edit > Copy              copies the details (or the selected part)
  View                     shows or hides each part of the tree; with
                           Descriptors on, the selected USB device's
                           descriptors are read and shown in full
  Action > Safely Remove   stops the selected USB stick or disk, or
                           Thunderbolt or USB4 device
  Action > Properties      opens the device in Device Manager
                           (Windows 2000 and later)

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

The notification-area icon
--------------------------

  Click            a list of the devices you can stop: USB devices,
                   then Thunderbolt and USB4 devices
  Double-click     opens the window
  Right-click      Open, Refresh, About and Exit

The icon says when a Thunderbolt or USB4 device is connected, is waiting
to be approved, could not be approved or was disconnected, and when the
controller stops answering: in a balloon on Windows ME, 2000 and later,
and in the icon's tooltip on Windows 98 SE.

From a command line
-------------------

  XHCITR                       the window and the icon
  XHCITR /tray                 the icon alone (as started at logon)
  XHCITR /report <file>        writes the report to <file> and exits
  XHCITR /eject id=<id>        stops the USB device or Thunderbolt/USB4
                               device with that Device Manager instance
                               ID and exits

Exit codes: 0 done, 1 refused (nothing changed), 2 failed, 4 Windows
refused or a step remains for you.

Licence
-------

Free software under the GNU General Public License, version 2. See the
package's LICENSE and NOTICE files.
