XHCIWIZ and XHCIINST - install, update or uninstall the xhci98 drivers
======================================================================

What it is for
--------------

To install or update, double-click SETUP.EXE, at the top of the package:
it opens the installer's window. To uninstall, use Remove in Add/Remove
Programs (or SETUP /uninstall). INSTALL.BAT, beside it, is for the
command line and scripts (INSTALL /console ...); run with no arguments it
opens the same window.

On Windows 2000, XP, XP x64, Vista and 7 it installs or updates every driver
of the package on the devices it finds - the xHCI controller first, then
its root hub, hubs and composite entries, then UAS disks and a
Thunderbolt or USB4 controller - and puts the package's INFs (all but one
whose driver has a newer release installed) where Windows finds them for
a device plugged in later; the release notes say when Windows may still
ask for the medium an earlier install came from. It does what the release
notes tell you to do in Device Manager, including the traps of an update
(Windows 2000 keeping the old driver unless Have Disk is used; Vista and 7
keeping an old build of the same version; the root hub's own update).

On Windows 98 SE and ME it copies the package's drivers into the install
directory (C:\XHCI98 unless you choose another), adds that folder to the
places the hardware wizards search, and installs each device's driver
through Windows' own device installer, with a small helper, XHCIDI.EXE.
A device the helper cannot install gets its Device Manager steps on a page
of their own, which you can save to a text file; the rest of the install
completes.

On Windows 98 SE and ME the Components page also offers "Close PCI
windows above 4 GB (XHCIPFW.VXD)", under the Thunderbolt 3 / USB4 box and
above Stor98's: a small part Windows loads at each start, before it reads
the PCI bridges, which closes every bridge's prefetchable memory window
that lies wholly above 4 GB. Those systems cannot use such a window, and
a board that leaves one there (a USB4 controller's ports with "Above 4G
Decoding" off, for one) otherwise leaves the devices behind it without
resources. It is part of the Thunderbolt 3 / USB4 support: ticked with
that box, unticked and greyed without it. It writes nothing on a computer
without such windows, and records what it did under
HKLM\System\CurrentControlSet\Services\VxD\XHCIPFW, in values whose
names begin with XhciPfw. Untick it, or uninstall, to remove it.

On Windows 2000, XP and the 32-bit Vista and 7 the same box, "Close
PCI windows above 4 GB (XHCIPFW.SYS)", offers XHCIPFW.SYS. It is a small
driver Windows loads at each start before its PCI driver, and it does
the same. As on 98 SE and ME it is ticked whenever the Thunderbolt 3 /
USB4 box is. Without that box it is unticked and greyed. You may untick
it. A USB4 or Thunderbolt controller behind a bridge with such a window
is not found until the window is closed. Device Manager then shows the
bridge with "This device cannot find enough free resources" (code 12).
On such a computer tick the Thunderbolt 3 / USB4 box yourself (or use
/thunderbolt). That also ticks this one. The console says so when it
sees such a bridge. On Windows 2000 and XP it acts only when one
processor runs. On a computer with several, start them with /NUMPROC=1
in BOOT.INI if they need this. It records what it did under
HKLM\SYSTEM\CurrentControlSet\Services\xhcipfw\Parameters. No 64-bit
Windows is offered it.

The 32-bit Windows 8, 8.1 and 10 are not offered XHCIPFW.SYS. There it
may start after Windows has read the PCI bridges. On a board that leaves
the USB4 ports' windows above 4 GB, these systems cannot use the devices
behind those ports. Windows 8 may not start at all on such a board. An
XHCIPFW.SYS this installer placed there before is removed. /xhcipfw and
/noxhcipfw are refused there.

On the 32-bit Vista, 7, 8, 8.1 and 10, set "Above 4G Decoding" off in
the computer's firmware setup where it offers that. With it on, those
systems can stop at start on a board that offers much memory space above
4 GB. Nothing here can help then. With it off, XHCIPFW.SYS closes the
windows the firmware still leaves above 4 GB on Vista and 7.

It uses only routines Windows documents for this. It is not devcon, and
this package's own files include no Microsoft file (the downloads'
USBDRV\NUSB33, and USBDRV\NONFREE in the -nonfree download, carry
some: see their
readme.txt).

The names in Device Manager: each USB 3 controller the installer knows is
named with its maker and product, as in "xHCI98 USB 3 Controller (Renesas
uPD720202)". Every install or update run writes that name (the device's
FriendlyName), even one that had nothing to reinstall, and the uninstall
removes it; a name you gave the controller yourself is kept. Installed
from the INF alone, a controller the table knows takes the same name on
Windows 98 SE, ME, 2000 and XP, and any other controller, or any on Vista
and later, the plain "xHCI98 USB 3 Controller".
Thunderbolt and USB4 controllers take theirs from the INF, as in "xHCI98
TB3 Controller (Intel Titan Ridge)" and "xHCI98 USB4 Router (Intel Meteor
Lake)" - TB2 and TB3 are Thunderbolt 2 and Thunderbolt 3.

The connection manager alone: you can leave the USB drivers (xhci98.sys,
xhciuas.sys) out and install only the Thunderbolt 3 / USB4 connection
manager, for a computer whose USB 3 controllers already run another
maker's driver - untick the USB drivers on the Components page, or
INSTALL /console /cmonly. On Windows 98 SE and ME the installer warns
first: those systems have no USB 3 driver of their own. On Windows 8,
8.1 and 10 the USB drivers are unticked by default, Windows' own driver
running the controllers; ticking them (or /usb) replaces Windows' own on
every xHCI controller, with a warning that says so. Stor98 is ticked
with the Thunderbolt support on every system but 10, where Windows' own
NVMe driver (stornvme) stays the
tunnelled drive's driver and Stor98 is yours to tick (task 43.59); on
8.1 it replaces stornvme on a drive behind a tunnel. Windows 11 (build
22000 and later) is refused: it has a USB4 connection manager of its
own. Every
box of the Components page is shown on every system; one that does not
apply there gets a warning when you click Next, and Cancel
on it takes you back to the page. See "The connection manager alone"
below.

Which file
----------

  XHCIWIZ.EXE    the window: Windows 98 SE, ME, 2000, XP, 32-bit Vista
                 to 10 (on 64-bit Windows it starts XHCIWZ64.EXE itself)
  XHCIWZ64.EXE   the window: Windows XP x64, and Vista x64 to 10 x64
  XHCIINST.EXE   the console tool, for scripts: 32-bit Windows
  XHCIIN64.EXE   the console tool: 64-bit Windows
  XHCIDI.EXE     Windows 98 SE and ME only: the helper that installs a
                 driver on a device; run by the other two, never by hand

SETUP.EXE, at the top of the package, starts the right window and ends at
once; INSTALL.BAT picks the right one and the right directory (release-x86
or release-x64) and waits for it. Either passes its arguments on (SETUP
/uninstall, INSTALL /console /list).

The installer's window
----------------------

Unzip the WHOLE package first: the installer refuses to run from anywhere
but its own tools\xhciinst directory of an unzipped package, and checks
that every file of the driver directory belongs to this release, this
architecture and this build.

  Windows 98 SE, ME: double-click SETUP.EXE.
  Windows 2000, XP:  sign in as an administrator, then double-click
                     SETUP.EXE.
  Windows Vista to 10:
                     double-click SETUP.EXE and answer Yes when Windows
                     asks for an administrator's approval (once, for the
                     window; SETUP.EXE itself asks for none).

SETUP.EXE leaves no console window behind; INSTALL.BAT, double-clicked,
keeps one open until the window closes. If the window cannot be started
(a file missing, or the approval refused) SETUP.EXE says so in a message.

The window walks you through:

  Welcome          the system found, the log's place, and what was
                   found: each device of the package, what runs it now
                   and what will be done, and any warning
  Components       the USB drivers with the UAS disk driver under them,
                   Thunderbolt 3 / USB4 support with Stor98 under it
                   (where the package carries it), the USB storage
                   drivers (98 SE only, where the download carries them),
                   the xHCI98 USB Tree started at login, and the install
                   folder (Change folder...); a warning on Next for a box
                   that does not apply there. Settings... opens one
                   dialog with a tab each for USB, Thunderbolt and
                   Stor98, the components ticked; Advanced... on a tab
                   holds the rest of its options
  Summary          everything that will happen, the warnings again;
                   nothing is changed before you click Install
  Progress         each step as it runs; Cancel stops at the next step
  Steps            (98 SE and ME) a device the installer could not install
  Finish           the result, and Restart now or Restart later

Help on every page opens this file.

Thunderbolt 3 / USB4 support (experimental): never ticked unless you
tick it, whether or not a controller is found; the line under the box
says whether one was found - some laptops show their controller only
while a device is plugged in, or only with a BIOS setting, so tick it if
yours has the port. With it, a device plugged into
that port is approved automatically and gets a PCI Express connection,
which lets it read and write the computer's memory: these systems have no
Thunderbolt control panel to ask first. On a computer whose port is within
reach of people you do not trust, set XhciCmApprove to 0 on the
Settings dialog's Thunderbolt tab; devices then get no connection unless
the BIOS
already gave them one. Unticked, a Thunderbolt driver already installed
stays as it is, and this package's is kept out of Windows' reach.

Stor98 (hot-pluggable NVMe driver; its guide is stor98-readme.txt):
ticked with the Thunderbolt 3 / USB4 box (on Windows 10 unticked), and you
can untick it. Ticked, it is installed on every NVMe drive behind a
Thunderbolt or USB4 tunnel that has no driver - never on one another
driver runs (but on 8.1, and on 10 when you tick it, on a tunnel drive
that Windows' own stornvme runs, which it replaces), nor on an internal
drive - and an
older Stor98 is
updated. (Updating from 3.2.0.0 on an ASMedia ASM4242 USB4 controller,
the first run leaves Stor98 and says so: 3.2.0.0's Thunderbolt driver
does not list that controller's tunnel ports. Run the installer again
after the restart; it updates Stor98 then, unless another Stor98
binding keeps the shared file - an internal drive on it, say - which the
run then says.)
Its tab in the Settings dialog shows
each of Stor98's options as it is now ("not set" means Stor98's built-in
default); a row left alone writes nothing. On Windows 98 SE and ME an
older Stor98 is updated as a file, replaced when Windows next starts
(no drive is reinstalled), once you answer OK to "Please disconnect all
external NVMe drives before proceeding" at Install (Cancel stays on the
Summary page), and the window then offers "Shut down now"
rather than a restart: Stor98's readme asks for a fresh start after its
update there. On 2000 to 7 a
drive behind a Thunderbolt or USB4 port is unplugged and plugged in again
after its update; the installer asks for that and waits up to two
minutes. The uninstall removes Stor98 only where this installer put it
there: a Stor98 installed before, by hand or by an earlier tool, stays.
Stor98 is not for internal NVMe drives. The package gives a drive
behind a Thunderbolt or USB4 port an id of its own,
PCI\XHCI98_TUNNEL&CC_010802, and on Windows 2000 to 8 Stor98's INF
matches that id alone, so Windows never offers Stor98 for an internal
drive there; on 8.1 and 10 it keeps 3.2.0.0's model, which stornvme
outranks; on 98 SE and ME the installer stages stor98w9.inf, which
matches every NVMe controller. An update removes the earlier release's
Stor98 INF from the driver store (on Windows 2000 only once no drive
names it: the installer says which drive keeps it). A tunnel drive that
Windows found before it had the id gets Stor98 from the safe-removal
mark service within seconds on Vista to 8.1; on 2000 and XP the service
restarts the enclosure so that Windows' own wizard offers Stor98.

USB storage drivers (Windows 98 SE only). Windows 98 SE has no USB
mass-storage driver: every USB disk, Bulk-Only or UAS under xhciuas.sys,
needs NUSB 3.3's seven storage files, which a download built with them
carries in USBDRV\NUSB33. The row for them is ticked when none of
the seven is found, and unticked when any of them is there or NUSB is
installed (its Uninstall\USB20U entry; whatever an earlier run chose;
/storage still ticks it). With NUSB not installed, a USBSTOR.INF found
in Windows' INF folders without USBNTMAP.INF ticks it, and the row says
that such a USBSTOR.INF lets Windows' own NTMAP.INF take a new USB disk
and stop Windows; a run that ends with it still alone says so and leaves
a step. The installer never copies over a file
already present, and with only some
of the seven found, one of them not this download's, it copies nothing
and says the storage install is not complete. It installs a stick at
"Unknown Device" with the two INFs in place. It also puts the two INFs,
USBSTOR.INF and USBNTMAP.INF, in Windows' INF folder where NUSB's
installer puts them, USBNTMAP.INF first and never one without the other
(USBSTOR.INF alone lets Windows' own NTMAP.INF take a disk, which stops a
stock Windows 98 SE), never over a file of that name,
so that a USB disk plugged in later is meant to install without the New
Hardware wizard. The uninstall removes the files it copied, the INFs it
put in the INF folder, the copies Windows made of their INFs for it and
the devices installed from them (a stick first plugged in after the
install included), never while NUSB is installed.

Free USB drivers and Non-free USB drivers (the pages after Components).
A release has up to three downloads: xhci98-<version>.zip, -free.zip,
which adds the free USB class drivers of usb-wdm-free in USBDRV\FREE
("These drivers are experimental. They may not work with every device
and can make Windows unstable. Report problems at
https://github.com/yeokm1/usb-wdm-free."), and -nonfree.zip, which adds
the non-free driver pack of usb-wdm-nonfree in USBDRV\NONFREE: other
manufacturers' Ethernet, HID and serial drivers, not this project's
work. Wi-Fi drivers are not in this download: they are in
usb-wdm-nonfree's own release. Each set this download carries has a
page of its own; a set it does not carry has no page, and
the Finish page says which download and which address have it
(https://github.com/yeokm1/usb-wdm-free and
https://github.com/yeokm1/usb-wdm-nonfree, where the up-to-date drivers
are). The set's row on its page is unticked unless you tick it. Ticked,
the set is copied to USBDRV\FREE or USBDRV\NONFREE in the install folder
and nothing is installed. Below the row, under "Let Windows choose these
drivers automatically for matching devices", is a box for each driver
package of the set that has an INF for this Windows, grouped by kind
(Audio, Video, HID, Network, Storage, Serial, Other), each unticked and
open only while its set's row is ticked; a ticked package's INFs for
this Windows are staged in the driver store, and Windows may still ask
for the folder of the files the first time. On Windows 98 SE and ME two
free packages are offered, uac2 (98 SE and ME) and usbmidi (98 SE),
because Windows' own USB audio INF would otherwise take their devices
first: ticked, its folder is added to DevicePath. Every other package
there is copied only, and none of its folders is added to DevicePath (a
long DevicePath stops 98 SE's hardware wizard finding any driver; the
installer keeps it within 280 characters and offers no box that would
pass that). There, when the wizard asks for a driver, choose "Specify a
location" and point it at the package's folder,
USBDRV\FREE\<class>\<package>\<folder> (or USBDRV\NONFREE\...) in the
install directory; an install over a run that added another package's
folders to DevicePath takes them out again. Two packages that would
claim the same device are not offered together, and the box below the
list says why. What an earlier run staged is listed there too: leaving a
box unticked does not undo it. On 64-bit Vista and later, with a free
package ticked, one more box offers to trust usb-wdm-free's own test
certificate (its 64-bit drivers are signed with it and load only in
test-signing mode; the console's switch is /wdmfree:trust); the
uninstall removes the trust it added. No trust is added for the non-free
drivers. Trusting the certificate lets Windows load the set's
test-signed 64-bit drivers, but Windows still says "Windows can't verify
the publisher of this driver software" when it stages one of these
packages, because they carry no catalog: answer "Install this driver
software anyway". The Finish page lists the folders to point the
hardware wizard at. The uninstall removes the copies, the DevicePath
entries and the staged INFs it made; a driver a device installed from
them stays installed, and so does a folder whose INF a device still
uses.

Switches the window takes: /uninstall (the uninstall, below), /to path
(the install folder, preselected), /thunderbolt, /stor98 (only with
/thunderbolt or /cmonly), /nostor98 (Stor98 unticked), /s98vsd and
/nos98vsd (98 SE and ME: S98VSD.VXD, Stor98's large transfers, ticked or
unticked; /s98vsd needs Stor98 in the run), /xhcipfw and /noxhcipfw
(98 SE, ME, 2000, XP and the 32-bit Vista and 7: XHCIPFW.VXD or
XHCIPFW.SYS, PCI windows above 4 GB closed at each start, ticked or
unticked; /xhcipfw only with /thunderbolt or /cmonly),
/faster and /nofaster
(the USB Tree's "can run faster" notice on or off for this computer),
/notray,
/nouas, /storage, /nostorage, /cmonly, /usb, /wdmfree:copy,
/wdmfree:stage=<ids>, /wdmnonfree:copy and /wdmnonfree:stage=<ids>
(the components, preselected; /free and /nonfree are the copies),
/set Name=Value
(an option, preselected), /defaults,
/force (reinstall this release, or go back to it from a newer one),
/restage (Vista to 10: replace a stored development build with the same
DriverVer), /debug (the debug- directory, only if a maintainer asks),
/log path.

The console tool
----------------

  INSTALL /console              shows what it will do, asks the questions
                                below, then asks once before it changes
                                anything: Y or Enter goes on, N cancels
  INSTALL /console /list        shows what it would do; changes nothing,
                                asks nothing, writes no log
  INSTALL /console /yes         asks nothing: goes on, and keeps every
                                option /set does not name
  INSTALL /console /uninstall   removes the drivers (see below)
  INSTALL /console /thunderbolt installs the Thunderbolt 3 / USB4 support
                                (experimental) without asking; without
                                it the console asks, Enter for N, and
                                /yes leaves it out
  INSTALL /console /cmonly      installs the Thunderbolt 3 / USB4
                                connection manager alone, without the
                                USB drivers (with a warning on 98 SE and
                                ME; on 8, 8.1 and 10 the default)
  INSTALL /console /usb         Windows 8, 8.1 and 10: installs the USB
                                drivers too, over Windows' own
  INSTALL /console /thunderbolt /stor98
                                installs the bundled Stor98 without asking;
                                it is part of the Thunderbolt 3 / USB4
                                support, so /stor98 needs /thunderbolt (or
                                /cmonly); without /stor98 the console asks,
                                Enter for Y, and /yes installs it: Stor98
                                comes with the Thunderbolt support, on
                                every system
  INSTALL /console /thunderbolt /nostor98
                                the Thunderbolt support without Stor98
  INSTALL /console /nouas       installs without the UAS disk driver
                                (xhciuas.sys); one already installed
                                stays as it is
  INSTALL /console /storage     Windows 98 SE: installs the USB storage
                                drivers without asking; on any other
                                system it only copies their files to
                                USBDRV\NUSB33 in the install folder, with a
                                warning; /nostorage leaves them out on any
                                system; without either the console asks
                                on 98 SE, and /yes takes the row's default
  INSTALL /console /wdmfree:copy
                                copies the free USB class drivers this
                                download carries without asking;
                                /wdmfree:stage=audio/uac2,video/uvc98
                                also lets Windows choose those packages
                                automatically for matching devices
                                (INSTALL /list lists the packages for
                                this Windows); /wdmnonfree: does the same
                                for the non-free driver pack. /freestage
                                and /nonfreestage are refused: name the
                                packages. Without them the console asks,
                                Enter for N, and /yes takes N. On 98 SE
                                and ME only audio/uac2 (and on 98 SE
                                audio/usbmidi) can be staged: any other
                                package in /wdmfree:stage=, and
                                /wdmnonfree:stage=, are refused, with
                                nothing changed
  INSTALL /console /notray      installs without starting the xHCI98 USB
                                Tree at login
  INSTALL /console /faster      turns the xHCI98 USB Tree's "can run
                                faster" notice on for this computer;
                                /nofaster turns it off (each user's own
                                choice in the USB Tree overrides it)
  INSTALL /console /to path     installs the xHCI98 tools in that
                                directory without asking

Its other switches are the window's, and /nopause. On Windows 98 SE and
ME, INSTALL.BAT takes eight arguments at most, seven after /console, and
COMMAND.COM splits them at = and at commas: /set Name=Value counts as
three. It refuses more rather than run a shortened command, and ends
with exit code 1 (a refusal).
Run TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86 with the same arguments
instead for a longer one. On 98 SE and ME the console tool installs each
device through the helper too; a Windows dialog may open (the Windows CD
for usbd.sys): answer it as below.

The connection manager alone
----------------------------

What it gives on a computer whose USB 3 driver is another one (Windows'
own on 8, 8.1 and 10): the Thunderbolt 3 and USB4 controllers are driven by
this package's connection manager - the devices are approved (see
XhciCmApprove), connected and disconnected - and what a device brings is
driven by whatever driver Windows has for it: an NVMe drive by an NVMe
driver (Windows 8.1 and 10 have their own; Windows 8 has none; the
bundled Stor98 is ticked with the Thunderbolt support on 8 and 8.1 and
unticked on 10), the USB controller inside a dock by
Windows' own USB 3 driver on 8, 8.1 and 10 (on 2000 to 7 by another maker's
driver only where it supports that controller), and the USB devices of a
USB4 port's USB 3 connection on the computer's own USB controller.
Safely Remove (the xHCI98 USB Tree, INSTALL /console /eject) works as
with the whole package. The connection manager needs nothing of
xhci98.sys.

A run with the USB drivers left out never updates or removes an xhci98
already installed: it stays as it is. To go from the whole package to
the connection manager alone, uninstall (Add/Remove Programs, or INSTALL
/console /uninstall) and install again with the USB drivers unticked. To
go the other way, run the installer again with them ticked. Add/Remove
Programs lists one entry either way, named for what is installed.

Windows 8, 8.1 and 10. The installer installs the connection manager,
with Stor98 ticked on 8 and 8.1 and unticked on 10 (Windows' own
stornvme stays there; /stor98 asks for Stor98), and the USB drivers only
when you tick them: Windows' own USB 3 driver keeps the controllers
otherwise. It admits every release of Windows 10, 1507 to 22H2 and the
LTSC releases, and Windows Server 2016, 2019 and 2022, which it treats
as Windows 10; Windows 11 is refused. Should an uninstall with the USB
drivers stop part-way, it says so: restart and run it again. When the
installer asks for a restart, use Restart, not Shut down: Windows 8's
and 10's Shut down does not start the drivers afresh. Windows 10 has no
Thunderbolt or USB4 driver of its own (USB4 support came with Windows
11), but many computers come with Intel's Thunderbolt driver. Where it
runs the Thunderbolt controller, the installer says so and leaves the
Thunderbolt support unticked (the console asks, N the default); ticked
(/thunderbolt), it replaces Intel's driver on that controller after a
warning. The uninstall gives the controller back to Intel's driver, and
Windows Update may also put Intel's driver back.

On 64-bit Windows 8, 8.1 and 10 this package's drivers, being
test-signed, load in test-signing mode, which those systems let you turn
on only with Secure Boot off in the computer's firmware setup, or on a
start with driver signature enforcement disabled (Advanced startup,
Troubleshoot, Advanced options, Startup Settings, 7). They are not
Microsoft-signed. The 64-bit package carries a catalog for each driver
(xhci98.cat and the others), signed with the same test certificate:
in test-signing mode these systems refuse a 64-bit driver package
without one, so keep the .cat files beside the INFs. With Secure Boot
on, Windows 10 from 1607
loads only drivers Microsoft has signed (and some older cross-signed
ones), and Windows 8, 8.1 and 10 refuse unsigned drivers on 32-bit too.

The connection manager on 64-bit Vista, 7, 8, 8.1 and 10. Windows makes
it a boot driver when it installs its filter on the Thunderbolt
controller's PCI ports, and while it is one, a start that cannot load it
(test-signing mode off) does not finish: 64-bit Windows 8 stopped in
Automatic Repair, and 7 undid the whole install. So the installer
installs it there only in test-signing mode it can confirm (a start with
enforcement disabled is not enough), and at the end of each run sets it
back to start on demand; the mark service does the same after an install
the installer did not run. A start without test-signing mode then
reaches the desktop, with the Thunderbolt devices and this package's USB
controllers at Code 52 (Code 39 on Vista) until test-signing mode is on
again. Where the disk Windows starts from is below a device the filter
is on, it stays a boot driver, and the installer says so. Keep
test-signing mode on while it is installed. Should a start stop in
Automatic Repair or undo the install, the way back is a start with
enforcement disabled (F8 on Vista and 7; on 8 to 10 Advanced startup,
Troubleshoot, Advanced options, Startup Settings, 7), test-signing mode
turned on again there, a restart, and the installer run again.

Where the installer reads Secure Boot as on (Windows 8, 8.1 and 10), it
refuses before changing anything a run with the USB drivers ticked (Windows
would load none of them, and every USB 3 controller, the keyboard's and
mouse's included, would stop working) and the connection manager on 32-bit
Windows (Windows would not load it, and Windows makes it a boot driver
while it installs its filter: a start then would stop in Automatic
Repair), as it already refused the connection manager on 64-bit Windows.
Turn Secure Boot off in the computer's firmware setup first. On 64-bit
Windows 10 it also reads whether memory integrity (Windows Security,
Device security, Core isolation) is turned on, and if it is, says that
Windows refuses a driver it finds incompatible with it.

After an update from 2.3.x or earlier they say, in one line, when old
XHCI98\ entries are still recorded. They do no harm, and nothing needs
them removed.

Before you start
----------------

The USB keyboard and mouse on the controller stop for a moment while it
is updated. From 2.3.x or earlier, on Windows XP, XP x64 and 98 SE, they
do not work until Windows' questions for the new root hub and hubs are
answered: have a keyboard and mouse that do not go through this driver
(PS/2, a laptop's built-in ones, or a USB controller another driver runs).

On Windows 98 SE and ME, Windows' own hardware wizards open for the
devices the controller shows after it starts: answer them (Next, Next,
Finish, and type the install folder where they ask for a folder or for the
"xhci98 xHCI Host Controller Disk"). Finish every such wizard, Next
through Finish, even when it finds no driver; never cancel one: on 98 SE
a cancelled wizard hides its device until Windows restarts, and the
installer can then only list the device for after the restart. Before
the first device the installer asks you to finish any such wizard; later
it asks again only when it sees one of those wizards open, or when it
cannot look.

Asked for the Windows CD (Windows' own files, such as usbd.sys), insert
it and type its WIN98 folder (WIN9X on ME) with your CD drive's letter,
for example E:\WIN98; the prompt offers D:\WIN98 whatever your drive is.
A PC set up without the CD often keeps those files in
C:\WINDOWS\OPTIONS\CABS: type that instead.

On Vista x64 and 7 x64 the drivers are test-signed: they load in
test-signing mode (bcdedit /set testsigning on, then restart) or on a
start where F8 chose "Disable Driver Signature Enforcement"; on 8, 8.1
and 10 x64 test-signing mode needs Secure Boot off first (above). The
installer says whether it could confirm test-signing mode; it never
changes it. It installs the Thunderbolt 3 / USB4 support on 64-bit Vista
to 10 only when it can confirm test-signing mode (above).

The options
-----------

The Settings dialog's tabs (and the console's questions) show a few of the
driver's options, each only where the hardware it affects is present,
with the value now, each value allowed with what it means, and the
default. A value left as it is is not written; one you change is written
machine-wide (HKLM\System\CurrentControlSet\Services\USB), which every
controller's driver reads first, and both places are shown where they
differ. They are XhciIntelPortSwitch (only with an Intel 7-, 8- or
9-series controller), XhciSilentInstall (Windows 98 SE and ME only, off
by default: on does what
tools\silent\silent-on.bat does), and XhciCmMode, XhciCmApprove and
XhciCmSafeEject (with the Thunderbolt support). The release notes,
"Registry settings", say what each does. An answer applies from the
driver's next start. Advanced... on each tab holds the values for the
release notes' advice and a tester's use (XhciInterruptMode,
XhciForceBulkOnly, XhciPollFineTimer, XhciSelectiveSuspend, XhciCmReenum,
XhciCmUsb3Tunnel, XhciCmLaneBonding, XhciCmPreOsCleanup, the connection
manager's waits and its log, XhciCmBridgeNumbering and XhciCmNoIrqStart
on 98 SE and ME, and the mark service's Restart on 2000 to 10); the
console sets them only with /set Name=Value. The drivers' own install
writes each value of the release notes' "Registry settings" and "The
connection manager's registry values" at its default where it is
missing, so it is already in the controller's key (XhciCmBridgeNumbering
and XhciCmNoIrqStart on 98 SE and ME alone, the systems that read them);
XhciCmTunnelBridge, whose absence means no override, is never written.

When Windows starts installing a device by itself while the installer
runs - a Found New Hardware window, perhaps behind the installer's - it
waits for it (up to 10 minutes) and goes on afterwards; answer that
window. It never installs a device while Windows' own install of it is
under way.

At the end it says "Changes were made. Please restart Windows now." (exit
code 3) when it changed anything and finished, "Changes were made, but not
everything is done" when it stopped with steps left for you (do those
first), and "No changes were made." when it changed nothing. After a
Stor98 update on 98 SE or ME the first two say "shut down and start
Windows again" instead. The window then offers to restart (or shut down)
Windows; the console tool never does.

The install directory
---------------------

The installer asks where to install the xHCI98 tools: the xHCI98 USB Tree
(XHCITR.EXE, or XHCITR64.EXE on 64-bit Windows), which starts at each
login, the installer itself, which Add/Remove Programs runs to remove the
package, and on Windows 98 SE and ME the package's drivers (the INF and
SYS files), which the hardware wizards search. An update replaces them
all at once; a file of the same name that the installer did not put there
stops it, and the uninstall removes only what it put there. The default
is C:\XHCI98 on the drive Windows is on, or, on an update, the directory
an earlier install used.

The directory must be a full path on a local hard disk, not the drive's
top level, not inside the Windows directory and not inside the package.
On Windows 98 SE and ME each folder name must be a short (8.3) name with
no spaces, because it is what you type into the hardware wizards. The
installer makes the directory if it is missing, and records it in the
registry (HKLM\Software\xhci98, InstallDir) so that an update and the
uninstall find it.

An earlier test build put the tools in C:\WINDOWS\XHCI98 (your Windows
directory). An update offers that directory as the default; choose
another and the installer moves them: it closes the xHCI98 USB Tree,
copies it to the new directory, points its start at login there, then
deletes the old copy and, if it is empty, the old folder. If the xHCI98
USB Tree does not close, nothing moves and the installer says to close it
and run INSTALL again. On Windows 2000 to 7 the package copies the drivers
are installed from stay in C:\WINDOWS\XHCI98, because Windows remembers
that place.

Thunderbolt and USB4 devices
----------------------------

  INSTALL /console /devices  shows the Thunderbolt and USB4 devices
                             connected now, and the Windows devices behind
                             each, with their drivers; changes nothing
  INSTALL /console /eject    lists them, asks which, and removes it safely
  INSTALL /console /eject 2  the same for device 2 of the list
  INSTALL /console /eject id=<instance id> /yes
                             for scripts: the device with that instance
                             id, as /devices prints it, with no question

Windows 98 SE and ME: to remove a Thunderbolt or USB4 device safely, use
the xHCI98 USB Tree or INSTALL /console /eject; these systems have no
other way. If a program has a file open on the device, Windows refuses and
nothing changes: close it and try again. When the tool says "You can now
unplug the device.", unplug it. Until then, Device Manager's Refresh can
start it again.

Windows 2000 to 7: Safely Remove Hardware works as usual; /eject also asks
Windows to stop a device it does not list. Run it as an administrator on
2000 and XP.

Exit codes for these: 0 listed, or ejected; 1 refused or cancelled (or
Windows answered that the device is gone or cannot be removed), nothing
changed; 2 Windows began to stop the device and could not finish - shut
Windows down before unplugging it; 4 Windows refused because the device
is in use, nothing changed.

The text can be saved to a file as well (INSTALL /console /eject >
EJECT.TXT): the questions still show on the screen, and the file gets the
same text.

Uninstall
---------

Add/Remove Programs (Control Panel) lists the package as "xHCI98 USB 3
drivers" with its version ("xHCI98 Thunderbolt 3 / USB4 connection
manager" when that is all it installed); Remove there runs the installer's
uninstall (INSTALL /uninstall does the same, and INSTALL /console
/uninstall in the console). It first warns that on a machine whose only
USB controller is xHCI the USB keyboard and mouse stop working until
another driver is installed, and goes on only when you tick that you have
another keyboard and mouse or accept it (the console: when you type
REMOVE). It removes, in this order and stopping at the first step that
fails or waits for a restart: on Windows 2000 to 7 this package's filter
from a Thunderbolt port, every device entry of the package, its INFs, its
services and its files; on Windows 98 SE and ME every device entry through
the helper, then the package copy and its search path, the INF copies
Windows kept and the driver files. Then the xHCI98 tools and the
installer, the options it wrote machine-wide, its records in the registry,
the Add/Remove Programs entry and the install directory if nothing else is
in it.

An uninstall that stops part-way keeps the installer, its records and the
Add/Remove Programs entry, named "(incomplete: run again)": restart
Windows if it asked, then run it again; it goes on from what is left. It
never removes Windows' own usbd.sys and usbui.dll, which other USB drivers
use, and it puts back nothing of another USB stack that was there before:
Windows finds the controller again at the next start, and on a machine
whose only USB controller is xHCI has no driver for it.

Removed from Add/Remove Programs, the installer runs from a copy of itself
in a temporary folder, so the install directory can go. On Windows 2000
to 7 that copy is deleted at the restart; on 98 SE and ME it stays (a few
hundred kilobytes, under TEMP\XHCI98UN) for you or a disk cleanup to
delete.

Exit codes
----------

  0  done, nothing needed a change (or /list)
  1  refused or cancelled; nothing was changed
  2  something failed; the screen and the log say what was done
  3  done, something changed; restart Windows (and run SETUP.EXE again
     after it if it says so)
  4  done as far as the installer can; it said what is left for you

INSTALL /console, and INSTALL without it (which waits for the window),
return the code to a script. SETUP.EXE does not wait: it returns 0 once
the window is started and 1 when it could not start it; the window's
result is on its Finish page and in the log.

The log
-------

Every run but /list adds to XHCIINST.LOG at the root of the system drive
(C:\XHCIINST.LOG on most machines). Attach it to a bug report about an
install.

------------------------------------------------------------------------------
Part of the xhci98 3.3.0.0 release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 or later - see the LICENSE
file two directories up. XHCIWIZ.EXE, XHCIWZ64.EXE, XHCIINST.EXE and
XHCIIN64.EXE, and SETUP.EXE two directories up, are not only this project's
code: a C runtime is statically linked into each, which NOTICE.TXT beside this
file records.
