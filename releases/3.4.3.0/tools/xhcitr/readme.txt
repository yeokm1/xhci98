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

It shows and changes the drivers' settings in the registry (Settings,
below) and their logging level (Logging level, below). It approves no
Thunderbolt device and updates no firmware. It needs no administrator
rights to start.

Which file
----------

  XHCITR.EXE     Windows 98 SE, ME, 2000, XP and 32-bit Vista to 10
  XHCITR64.EXE   Windows XP x64 and 64-bit Vista to 10

INSTALL.BAT copies the right one to the install directory it asks for
(C:\XHCI98 unless you choose another, or INSTALL /to path) and starts it
each time you log on. You can also run it from the package directly, or
from anywhere else: it does not care where it is. XHCITR.EXE started on
64-bit Windows starts XHCITR64.EXE from its own folder in its place.
When that is not there, it says so and runs itself, with limits: it reads
the 32-bit view of HKLM\Software, so what it shows from the start at login
(Run) and from Setup's records (Software\xhci98) may be wrong, it does not
show the devices' resources, and Eject may fail. Its snapshot commands
(below) run in XHCITR.EXE itself.

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
shows its ports and how many are USB 2.0 and USB 3, for example "Root
hub - 18 ports (14 USB 2.0, 4 USB 3)", and each port its kind:
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
  File > Save Firmware Tables...
                           writes this computer's firmware (ACPI)
                           tables in a folder you pick (below)
  Edit > Copy              copies the details (or the selected part)
  View                     shows or hides each part of the tree; with
                           Descriptors on, the selected USB device's
                           descriptors are read and shown in full
  View > Logging level...  the drivers' logging levels (below)
  View > Settings...       the drivers' settings (below)
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

Under the USB4 connection manager on Windows 2000 to 10, the stop also
closes the device's PCIe tunnel once the Thunderbolt driver has the stop
confirmed. Run as an administrator, the tool confirms it at once. Without
administrator rights on XP and later the xHCI98 mark service
confirms it in the background: the tool waits up to 30 seconds for that,
and if the tunnel is still up then it says the tunnel has not been seen
closed and closes if the mark service confirms the eject. On Windows
2000 without administrator rights, or with the mark service stopped,
nothing confirms it and the tunnel stays up until you unplug the
device. The tunnel also stays up when the Thunderbolt driver does not
know which of its ports the device is on, as for a device already
connected when Windows started on some computers; the tool then says
so. The driver learns the port when a device is plugged in while
Windows runs.

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

On Windows Vista and later, if Windows refuses because the device needs
administrator rights to stop, the tool offers to try again as an
administrator.

On Windows 2000 to 10, a Thunderbolt or USB4 device that Windows does not
mark removable (for example the first time it is plugged in) is missing
from Windows' own Safely Remove Hardware. The tool still stops it, with
administrator rights: when it runs without them it says so and offers
"Stop as administrator...", which asks for them (User Account Control on
Vista and later, the Run As dialog on 2000 and XP) for that one stop. If
you cancel, nothing is stopped.

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
files, which a problem report attaches beside the report:

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

PCI and bridge diagnostics
--------------------------

The last item of the tree, "PCI and bridge diagnostics", shows what the
package's drivers recorded about PCI bridges and interrupts, each value
by its registry name, its raw value beside it in plain words, and "not
recorded" where it is absent:

- XHCIPFW's log: the PCI bridges whose prefetchable windows above 4 GB it
  closed at the start, and any xHCI controller it found above 4 GB;
- for each Thunderbolt or USB4 controller on Windows 98 SE and ME, the
  connection manager's last bridge numbering and its last start of a
  device with no interrupt;
- on Windows 2000, the interrupt the connection manager gave a device
  plugged in behind a tunnel, from its log.

Select it and choose Edit > Copy with nothing selected to copy all of it,
for a problem report; File > Save Report and /report include it.

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
at once. On Windows 2000 to 10 without administrator rights the button
reads "Set as administrator..." and asks for them (User Account Control
on Vista and later, the Run As dialog on 2000 and XP) for that one change.
If you cancel, nothing is changed.

The levels are also in Save Report and XHCITR /report.

Settings
--------

View > Settings... (or a right click on a USB controller driven by
xhci98 or on a Thunderbolt or USB4 controller, or the icon's menu) shows
the drivers' settings in the registry and changes them: the ones Setup
offers, with the same meanings, choices and defaults, and a few more.
Each tab is shown only where its driver is installed:

  USB                  the xhci98 driver's settings
  Thunderbolt / USB4   the Thunderbolt and USB4 driver's (xhcicm)
  Stor98               the bundled Stor98's
  xHCI98 USB Tree      this tool's own settings, for you alone, each in
                       force at once: Start at login, Warn when a device
                       is removed unexpectedly (2000 and later), Tell me
                       when a device can run faster, Watch Windows' own
                       USB ports too, the quick-save hotkey and its sound
                       (also in the View menu, but for the last two)

The list shows the common settings; tick "Show advanced settings" for
the rest, which you should change only if the release notes advise it.
Each row shows the value now and the value the install writes. Select a
row to read what it does and pick a value; the row then shows the new
value and a *. Apply saves every change at once; Close without Apply
saves nothing.

On the USB tab the common settings are XhciIntelPortSwitch (Intel 7-, 8-
and 9-series and C610 computers: the shared, usually blue, connectors
moved from the USB 2.0 controller to xhci98), XhciIntelIrqRoute and
XhciIntelNoRouteIrq (the Intel interrupt repairs; the second on Windows
2000 and XP), XhciInterruptMode, XhciForceBulkOnly and XhciSilentInstall
(Windows 98 SE and ME: turning it on also puts the list of device classes
in place, as Setup and silent-on.bat do). Logging level... opens the
logging level window (above). On the Thunderbolt / USB4 tab they are
XhciCmMode, XhciCmApprove and XhciCmSafeEject; on the Stor98 tab
StorWriteThrough and StorPollMode. The release notes, "Registry
settings", say what each does.

"Applies to" picks the controllers: every one (the default), or one of
them, named with its maker, PCI ID and location. For every controller
the value is written where the drivers read it: into the machine-wide
key, HKLM\System\CurrentControlSet\Services\USB, where a value is set
there (it wins for every controller), else into each controller's own
driver key. One controller's own value can be set only while no
machine-wide value is set. A Stor98 setting is one value for every
Stor98 drive. Stor98's tab is read-only where its settings are shared
with a Stor98 that Setup did not install.

Changing XhciIntelPortSwitch asks first: at Windows' next start the
shared connectors move to xhci98 (or back to the USB 2.0 controller), so
a device on them is found again then; do not have a drive on them busy
when you restart.

Most settings take effect when the driver next starts. After Apply the
tool says which, and offers to restart Windows now (save your work first:
every USB device stops while Windows restarts). For some, disabling and
enabling the controller in Device Manager applies them too, and the tool
says so; it never does that itself. XhciForceBulkOnly and
XhciBulkOnlyOwnId take effect when a drive is plugged in again. Until
Windows restarts the window lists the changes made, with Restart now....

On Windows 98 SE and ME, and for an administrator, Apply writes at once.
On Windows 2000 to 10 without administrator rights the button reads
"Apply as administrator..." and asks for them, once for all the changes.
Apply refuses while Setup is running.

The settings are also in Save Report and XHCITR /report, and File > Save
Snapshot... writes them beside the snapshot as XHCISET.TXT.

Quick save
----------

The icon's menu has "Quick save (report + snapshot)": with no question
asked it saves the report (as Save Report), the driver's snapshot (as
Save Snapshot), the settings (XHCISET.TXT) and a short README.TXT in a
new folder named by the date and time, for example

  C:\XHCI98\Reports\2026-11-07_121530

in the folder Setup installed the tools in. Where that folder cannot be
written (Windows Vista and later without administrator rights, for
example), it saves in your documents folder instead, under "xHCI98
Reports". It runs in the background; when it ends, a balloon on the icon
(on Windows 98 SE a small window in its place) says where the files are
and whether everything was saved; click it to open the folder (not on
Windows 2000, whose balloons take no clicks). "Sound
when a quick save ends" in the Settings window decides whether it plays
a sound.

The Settings window can also give quick save a key of its own (Quick-save
hotkey, off until you tick it): Ctrl+Alt+F11, or one of the other
keys offered (Ctrl+Shift+F11, Ctrl+Alt+F10, Ctrl+Alt+F9, Ctrl+F11,
Ctrl+Alt+Shift+F11; the two-key Ctrl+F11 clashes with other programs
more often). The key works only while the xHCI98 USB Tree is running (it
starts at login with its icon), and only while Windows still takes
keyboard input: when it has frozen altogether, no key does anything. A
USB keyboard on the controller that is misbehaving may not get the key
through; a PS/2 keyboard, or one on another controller, does. If another
program has the key, the window says so; choose another.

The notification-area icon
--------------------------

  Click            a list of the devices you can stop: USB devices,
                   then Thunderbolt and USB4 devices
  Double-click     opens the window
  Right-click      Open, Settings..., About, Quick save (report +
                   snapshot) and Exit

Start at login
--------------

To stop the icon starting when you log on, untick "Start at login" in
the View menu or the Settings window; tick it again to bring it
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

USB drive notices are off unless you tick "USB drive notices" on the
Settings window's xHCI98 USB Tree tab. Then the icon says when a USB drive
on a controller xhci98.sys drives is connected or removed, naming it with
its drive letter where Windows gave it one ("SanDisk Ultra (F:) is
connected."), or by its product name where there is no storage driver,
as on Windows 98 SE without one. A drive connected or removed while the
computer sleeps is not reported.

Can run faster
--------------

When a USB 3 device runs slower than it can on a controller xhci98.sys
drives - on a USB 2.0 port, behind a USB 2.0 hub, fallen back to USB 2.0
in a USB 3 port, or a 10 Gbit/s device at 5 Gbit/s - or a High Speed
device runs at Full Speed behind a Full Speed hub (one not itself
behind a High Speed hub), a balloon names it
(the tooltip on Windows 98 SE) and, but for a 10 Gbit/s device at 5
Gbit/s, the icon turns red until every such device is reconnected at its
speed or removed. Click the balloon for each device's port and hub, the
speed it runs at and the speed it can, and what to try (a click opens
nothing on Windows 2000, whose balloon says to click the icon instead,
nor when the tool cannot tell which of its balloons is on the screen;
the icon's menu then leads on); the first item
of the icon's menu shows the same when only a 10 Gbit/s device at 5
Gbit/s is counted, and while the icon is red opens the window on the
device instead. A hub is
never listed itself: the USB 2.0 half of a USB 3 hub always runs at High
Speed.

"Tell me when a device can run faster" in the View menu and the Settings
window turns this on or off for you. The installer's box of the
same name (or INSTALL /faster and /nofaster) sets it for the computer;
your own choice overrides that. It is on by default on every Windows:
Windows' own "can perform faster" notices do not appear under xhci98.sys.

"Can run at" is never faster than this computer's USB 3 ports. On a
computer whose USB 3 ports run at 5 Gbit/s at most, a 10 Gbit/s device
at 5 Gbit/s is not listed, and the device's details say so in a note. A
hub of 5 Gbit/s on the way holds a device to 5 Gbit/s too.

Windows' own USB ports: by default only the ports xhci98.sys runs are
watched. A USB 3 device on a USB 2.0 port of the board's own EHCI, OHCI
or UHCI controller gives no notice. "Watch Windows' own USB ports too"
in the View menu and the Settings window turns that on for you.
It is off by default. The tool then asks such a device for its BOS
descriptor through Windows' hub driver, once per plug, and only when it
says USB 2.10 or later. A device that declares SuperSpeed there is
listed with "Connect it to a USB 3 port of this computer." Where
Windows' hub driver does not give the descriptor, nothing is said. On
Windows 98 SE the tool reads no port of Windows' own USB controllers,
so there the option does nothing.

The firmware tables
-------------------

File > Save Firmware Tables... and XHCITR /acpi <folder> write this
computer's firmware (ACPI) tables in the folder: one file a table
(RSDP1.DAT, SSDT1.DAT, SSDT2.DAT, ...) and TABLES.TXT listing them.
Nothing is written if the folder already has one of these files.

On XP x64, Vista and later the tables come from Windows, which gives
one table of each name (the first SSDT of several). Where Windows
gives no DSDT, as XP x64 may, the DSDT, and the FACP, FACS and RSDT
it did not give, come from the copies Windows keeps in the registry.
On 98 SE and ME
the xhci98 driver reads them from memory when you save them, the DSDT
and every SSDT included (XhciAcpiTables 0 turns this off); where the
xhcicm driver runs, the copies it read at its start are saved too
where they differ. On 2000 and 32-bit XP they come from the copies
Windows keeps in the registry: the DSDT, FACP, FACS and RSDT, but no
SSDT. A table that does not check out is listed in TABLES.TXT with
the reason and not saved.

The MSDM and SLIC tables hold this computer's Windows licence data.
They are never saved: TABLES.TXT lists each by its header alone.

The files are your computer's own firmware. They help tell whether
another board's USB4 firmware can be added; send them only if you
choose to.

From a command line
-------------------

  XHCITR                       the window and the icon
  XHCITR /tray                 the icon alone (as started at logon)
  XHCITR /report <file>        writes the report, the drivers' logs, PCI
                               configuration and the snapshot included,
                               to <file> and exits
  XHCITR /eject id=<id>        stops the USB device or Thunderbolt/USB4
                               device with that Device Manager instance
                               ID and exits; the ID of a device inside
                               a Thunderbolt/USB4 device (its drive,
                               say) stops that whole device
  XHCITR /acpi <folder>        writes this computer's firmware (ACPI)
                               tables in <folder> and exits
  XHCITR /setlevel xhci98=<n> xhcicm=<n>
                               sets either driver's logging level (0 to
                               4; one or both) and exits; needs
                               administrator rights on 2000 to 10
  XHCITR /set Name=Value [Name=Value ...] [/on <id>] [/yes]
                               sets the named settings (as the Settings
                               window offers them; Name=default for the
                               value the install writes) for every
                               controller, or with /on for the one with
                               that Device Manager instance ID, says what
                               it takes to apply them and exits; needs
                               administrator rights on 2000 to 10. A
                               change that can stop Windows from
                               starting (XhciIntelNoRouteIrq 0 on the
                               listed computers under Windows 2000) is
                               refused unless /yes is given
  XHCITR /set /show            lists the settings and exits
  XHCITR /snapshot [NAME] [/c N] [/nosysinfo]
                               writes the snapshot's four files in the
                               current directory (/c N: controller N,
                               default 0; /nosysinfo: without the
                               Windows, processor and BIOS details)
  XHCITR /dump [/c N] [/o NAME] [/nosysinfo]
                               writes the dump alone (NAME.TXT, .BIN and
                               .PSC; NAME is XHCITR without /o),
                               replacing an earlier set of NAME
  XHCITR /probe                checks the route to the driver and
                               writes nothing
  XHCITR /snapshot /?          the snapshot's long help

The snapshot commands show their text in a message box, or write it to
where their output is redirected.

Exit codes: 0 done, 1 refused (nothing changed), 2 failed, 3 done and
Windows must restart (/set), 4 Windows refused or a step remains for you
(/set: a drive to plug in again). /snapshot: 0 both files written in
full, 2 a name or switch refused, 3 to 8 as XHCITR /snapshot /? says.

Licence
-------

Free software under the GNU General Public License, version 2 or later. See
the package's LICENSE and NOTICE files.

------------------------------------------------------------------------------
Part of the xhci98 3.4.3.0 release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 or later - see the LICENSE
file two directories up. XHCITR.EXE and XHCITR64.EXE are not only this
project's code: a C runtime is statically linked into each, which NOTICE.TXT
beside this file records.
