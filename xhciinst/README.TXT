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

It uses only routines Windows documents for this. It is not devcon, and
this package carries no Microsoft file.

The connection manager alone: you can leave the USB drivers (xhci98.sys,
xhciuas.sys) out and install only the Thunderbolt 3 / USB4 connection
manager, for a computer whose USB 3 controllers already run another
maker's driver - untick the USB drivers on the Components page, or
INSTALL /console /cmonly. On Windows 98 SE and ME the installer warns
first: those systems have no USB 3 driver of their own, and this has not
been tested there. On Windows 8, 8.1 and 10 the USB drivers and Stor98
are unticked by default, Windows' own drivers running the controllers and
NVMe drives; ticking them (or /usb, /stor98) replaces Windows' own, with
a warning that they are untested there. Windows 11 (build 22000 and
later) is refused: it has a USB4 connection manager of its own. Every
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
                   the xHCI98 USB Tree started at login, and the tools
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

Stor98, the bundled NVMe driver (its guide is stor98-readme.txt):
ticked with the Thunderbolt 3 / USB4 box, and you
can untick it. Ticked, it is installed on every NVMe drive behind a
Thunderbolt or USB4 tunnel that has no driver - never on one another
driver runs, nor on an internal drive - and an older Stor98 is
updated. Its tab in the Settings dialog shows
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

USB storage drivers (Windows 98 SE only). Windows 98 SE has no USB
mass-storage driver: every USB disk, Bulk-Only or UAS under xhciuas.sys,
needs NUSB 3.3's seven storage files, which a download built with them
carries in its NUSB33\ folder. The row for them is ticked when none of
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

Other USB drivers (the page after Components). A release has up to
three downloads: xhci98-<version>.zip, -free.zip, which adds the free
USB class drivers of usb-wdm-free in a WDMFREE folder (beta: report a
problem with one of them at https://github.com/yeokm1/usb-wdm-free/issues,
not to xHCI98), and -nonfree.zip, which adds the non-free driver pack of
usb-wdm-nonfree in a NONFREE folder: other makers' drivers, not this
project's and not tested by it. The page shows a row for each set this
download carries and, for each it does not, which download and which
address have it (https://github.com/yeokm1/usb-wdm-free and
https://github.com/yeokm1/usb-wdm-nonfree, where the up-to-date drivers
are). Every row is unticked unless you tick it. Ticked, the set is
copied to WDMFREE or NONFREE in the tools folder and nothing is
installed; the box under it, also unticked, lets Windows use the copied
drivers when a device is plugged in (on 98 SE and ME by adding each
driver's folder to DevicePath, on 2000 and later by staging its INF in
the driver store), only for the INFs the set's STAGE.TXT names for this
Windows. The Finish page lists the folders to point the hardware wizard
at. The uninstall removes the copies, the DevicePath entries and the
staged INFs it made; a driver a device installed from them stays
installed, and so does a folder whose INF a device still uses.

Switches the window takes: /uninstall (the uninstall, below), /to path
(the tools folder, preselected), /thunderbolt, /stor98 (only with
/thunderbolt or /cmonly), /nostor98 (Stor98 unticked), /s98vsd and
/nos98vsd (98 SE and ME: S98VSD.VXD, Stor98's large transfers, ticked or
unticked; /s98vsd needs Stor98 in the run), /faster and /nofaster
(the USB Tree's "can run faster" notice on or off for this computer),
/notray,
/nouas, /storage, /nostorage, /cmonly, /usb, /free, /freestage,
/nonfree and /nonfreestage (the components, preselected),
/set Name=Value
(an option, preselected), /defaults,
/force (reinstall this release, or go back to it from a newer one),
/restage (Vista and 7: replace a stored development build with the same
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
                                drivers too, over Windows' own (untested
                                there)
  INSTALL /console /thunderbolt /stor98
                                installs the bundled Stor98 without asking;
                                it is part of the Thunderbolt 3 / USB4
                                support, so /stor98 needs /thunderbolt (or
                                /cmonly); without /stor98 the console asks,
                                Enter for Y, and /yes installs it: Stor98
                                comes with the Thunderbolt support (on 8,
                                8.1 and 10 Enter and /yes give N)
  INSTALL /console /thunderbolt /nostor98
                                the Thunderbolt support without Stor98
  INSTALL /console /nouas       installs without the UAS disk driver
                                (xhciuas.sys); one already installed
                                stays as it is
  INSTALL /console /storage     Windows 98 SE: installs the USB storage
                                drivers without asking; on any other
                                system it only copies their files to
                                NUSB33 in the tools folder, with a
                                warning; /nostorage leaves them out on any
                                system; without either the console asks
                                on 98 SE, and /yes takes the row's default
  INSTALL /console /free        copies the free USB class drivers this
                                download carries without asking;
                                /freestage also lets Windows use them at
                                a device's first plug; /nonfree and
                                /nonfreestage do the same for the
                                non-free driver pack; without them the
                                console asks, Enter for N, and /yes
                                takes N
  INSTALL /console /notray      installs without starting the xHCI98 USB
                                Tree at login
  INSTALL /console /to path     installs the xHCI98 tools in that
                                directory without asking

Its other switches are the window's, and /nopause. On Windows 98 SE and
ME, INSTALL.BAT takes eight arguments at most, seven after /console, and
/set Name=Value counts as three; it refuses more rather than run a
shortened command. Run TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86 with
the same arguments instead for a longer one. On 98 SE and ME the console
tool installs each device through the helper too; a Windows dialog may
open (the Windows CD for usbd.sys): answer it as below.

The connection manager alone
----------------------------

What it gives on a computer whose USB 3 driver is another one (Windows'
own on 8, 8.1 and 10): the Thunderbolt 3 and USB4 controllers are driven by
this package's connection manager - the devices are approved (see
XhciCmApprove), connected and disconnected - and what a device brings is
driven by whatever driver Windows has for it: an NVMe drive by an NVMe
driver (Windows 8.1 and 10 have their own; Windows 8 has none; the
bundled Stor98 is unticked there by default and untested), the USB
controller inside a dock by
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

Windows 8 and 8.1 were read in virtual machines, the install alone, on 8
(32-bit and x64) and 8.1 x64; 8.1 32-bit has not been run. The connection
manager installs there as on Windows 7. On 64-bit Windows 8 and 8.1 this
package's x64 drivers, being test-signed, load in test-signing mode, which
those systems let you turn on only with Secure Boot off in the computer's
firmware setup, or on a start with driver signature enforcement disabled.
The connection manager needs test-signing mode itself: Windows makes it a
boot driver there (it filters a PCI bridge), and a start without
test-signing mode then stops in Automatic Repair (read on 64-bit Windows
8). So on 64-bit 8 and 8.1 the installer installs it only in test-signing
mode; a start with enforcement disabled is not enough. Keep test-signing
mode on while it is installed. From Automatic Repair the way back is a
start with enforcement disabled (Advanced startup, Troubleshoot, Advanced
options, Startup Settings, 7), then test-signing mode on again, or the
uninstall. It is not Microsoft-signed. When the installer asks for a
restart, use Restart, not Shut down: Windows 8's Shut down does not start
the drivers afresh (nor does Windows 10's).

Windows 10 is installable. Its drivers were read in virtual machines of
22H2, each installed by a script; the installer itself has not yet run
there. It admits every build below 22000 - the releases 1507 to 22H2 and
the LTSC releases - and Windows Server 2016, 2019 and 2022, which it
treats as Windows 10 and which are untested; only 22H2 is to be tested.
It installs there as on Windows 8.1: the connection manager, and the USB
drivers and Stor98 only when you tick them. Windows 10 has no Thunderbolt
or USB4 driver of its own (USB4 support came with Windows 11), but many
computers come with Intel's Thunderbolt driver: where it runs the
Thunderbolt controller, the installer leaves it as it is. On 64-bit
Windows 10 the drivers load as on 64-bit Windows 8: in test-signing mode,
which needs Secure Boot off in the computer's firmware setup, or on a
start with enforcement disabled; and the installer installs the
connection manager there only in test-signing mode, as on 64-bit 8
(whether 10 too makes it a boot driver has not been read). With Secure
Boot on, Windows 10 from 1607 loads only drivers Microsoft has signed (and
some older cross-signed ones), and this package's are neither. The 64-bit
package carries a catalog for each driver (xhci98.cat and the others,
from 3.2.0.0), signed with the same test certificate: Windows 8, 8.1 and
10 x64 refuse a 64-bit driver package without one in test-signing mode
(error 0xE000022F; design record 23 section 5.12), so keep the .cat files
beside the INFs. Whether 32-bit Windows 10 with Secure Boot on refuses
this package's 32-bit drivers, which are not signed, has not been read.

Neither ever runs XHCICLN. After an update from 2.3.x or earlier they say,
in one line, when old XHCI98\ entries are still recorded; nothing needs
them removed, and tools\xhcicln\ lists and removes them if you want.

Before you start
----------------

The USB keyboard and mouse on the controller stop for a moment while it
is updated. From 2.3.x or earlier, on Windows XP, XP x64 and 98 SE, they
do not work until Windows' questions for the new root hub and hubs are
answered: have a keyboard and mouse that do not go through this driver
(PS/2, a laptop's built-in ones, or a USB controller another driver runs).

On Windows 98 SE and ME, Windows' own hardware wizards open for the
devices the controller shows after it starts: answer them (Next, Next,
Finish, and type the tools folder where they ask for a folder or for the
"xhci98 xHCI Host Controller Disk"). Finish every such wizard, Next
through Finish, even when it finds no driver; never cancel one: on 98 SE
a cancelled wizard hides its device until Windows restarts, and the
installer can then only list the device for after the restart. Before
each device the installer asks you to finish any such wizard.

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
changes it.

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
manager's waits and its log, XhciCmNoIrqStart on 98 SE and ME, and the
mark service's Restart on 2000 to 10); the console sets them only with
/set Name=Value.

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

Add/Remove Programs (Control Panel) lists the package as "xHCI98 USB 3.x
drivers" with its version; Remove there runs the installer's uninstall
(INSTALL /uninstall does the same, and INSTALL /console /uninstall in the
console). It first warns that on a machine whose only USB controller is
xHCI the USB keyboard and mouse stop working until another driver is
installed, and goes on only when you tick that you have another keyboard
and mouse or accept it (the console: when you type REMOVE). It removes, in
this order and stopping at the first step that fails or waits for a
restart: on Windows 2000 to 7 this package's filter from a Thunderbolt
port, every device entry of the package, its INFs, its services and its
files; on Windows 98 SE and ME every device entry through the helper,
then the package copy and its search path, the INF copies Windows kept
and the driver files. Then the xHCI98 tools and the installer, the
options it wrote machine-wide, its records in the registry, the
Add/Remove Programs entry and the install directory if nothing else is in
it.

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
