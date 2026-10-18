XHCIINST - install, update or uninstall the xhci98 drivers
==========================================================

What it is for
--------------

INSTALL.BAT, at the top of the package, runs this tool. On Windows 2000,
XP, XP x64, Vista and 7 it installs or updates every driver of the package
on the devices it finds - the xHCI controller first, then its root hub,
hubs and composite entries, then UAS disks and a Thunderbolt or USB4
controller - and puts the package's INFs (all but one whose driver has a
newer release installed) where Windows finds them for a device plugged in
later; the release notes say when Windows may still ask for the medium an
earlier install came from. It does what the release notes tell you to do
in Device Manager, including the traps of an update (Windows 2000 keeping
the old driver unless Have Disk is used; Vista and 7 keeping an old build
of the same version; the root hub's own update).

On Windows 98 SE and ME the tool stages the files and you finish the
installation in Device Manager: it copies the package's drivers into
the install directory (C:\XHCI98 unless you choose another), adds that
folder to the places the hardware wizards search, and prints the steps
that remain, one action a line, with the folder to type if a wizard does
not find the driver by itself. It then offers to open Device Manager.

It uses only routines Windows documents for this. It is not devcon, and
this package carries no Microsoft file.

Which file
----------

  XHCIINST.EXE   Windows 98 SE, ME, 2000, XP, 32-bit Vista and 7
  XHCIIN64.EXE   Windows XP x64, Vista x64 and 7 x64

INSTALL.BAT picks the right one and the right directory (release-x86 or
release-x64). The 32-bit tool refuses to run on 64-bit Windows, and each
refuses the other architecture's directory.

How to run it
-------------

Unzip the WHOLE package first: the tool refuses to run from anywhere but
its own tools\xhciinst directory of an unzipped package, and checks that
every file of the driver directory belongs to this release, this
architecture and this build.

  Windows 98 SE, ME: double-click INSTALL.BAT, or run it from an MS-DOS
                     Prompt in the package's top directory.
  Windows 2000, XP:  run it as an administrator.
  Windows Vista, 7:  right-click INSTALL.BAT, Run as administrator.

  INSTALL              shows what it will do, asks the questions below,
                       then asks once before it changes anything: Y or
                       Enter goes on, N cancels
  INSTALL /list        shows what it would do; changes nothing, asks
                       nothing, writes no log
  INSTALL /yes         asks nothing: goes on, and keeps every option
                       /set does not name
  INSTALL /uninstall   removes the drivers (see below)
  INSTALL /thunderbolt installs the Thunderbolt 3 / USB4 support without
                       asking (for unattended installs, with /yes)
  INSTALL /notray      installs without starting the xHCI98 USB Tree at
                       login (and stops an earlier install's start)
  INSTALL /to path     installs the xHCI98 tools in that directory
                       without asking (see "The install directory")

On Windows 98 SE and ME, INSTALL.BAT takes eight arguments at most, and
/set Name=Value counts as three; it refuses more rather than run a
shortened command. Run TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86
with the same arguments instead for a longer one.

Thunderbolt 3 / USB4 support: the tool asks "Install Thunderbolt 3 / USB4
support?". With a Thunderbolt or USB4 controller present Enter (and /yes)
answers Y; with none found it is still asked, and Enter (and /yes)
answers N - some laptops show their controller only while a device is
plugged in, or only with a BIOS setting, so answer Y if yours has the
port. With it, a device plugged into that port is approved automatically
and gets a PCI Express connection, which lets it read and write the
computer's memory: these systems have no Thunderbolt control panel to ask
first. On a computer whose port is within reach of people you do not
trust, set XhciCmApprove to 0 (INSTALL /set XhciCmApprove=0); devices then
get no connection unless the BIOS already gave them one. N leaves a
Thunderbolt driver already installed as it is, and keeps this package's
out of Windows' reach.

Other switches: /defaults (ask no option question), /set Name=Value (set
an option, repeatable), /force (reinstall this release, or go back to it
from a newer one), /restage (Vista and 7: replace a stored development
build with the same DriverVer), /debug (the debug- directory, only if a
maintainer asks), /nopause, /log path.

It never runs XHCICLN. After an update from 2.3.x or earlier it says, in
one line, when old XHCI98\ entries are still recorded; nothing needs them
removed, and tools\xhcicln\ lists and removes them if you want.

Before you start
----------------

The USB keyboard and mouse on the controller stop for a moment while it
is updated. From 2.3.x or earlier, on Windows XP, XP x64 and 98 SE, they
do not work until Windows' questions for the new root hub and hubs are
answered: have a keyboard and mouse that do not go through this driver
(PS/2, a laptop's built-in ones, or a USB controller another driver runs).

On Vista x64 and 7 x64 the drivers are test-signed: they load in
test-signing mode (bcdedit /set testsigning on, then restart) or on a
start where F8 chose "Disable Driver Signature Enforcement". The tool says
whether it could confirm test-signing mode; it never changes it.

The options it asks about
-------------------------

Before it changes anything it asks about a few of the driver's options,
each only where the hardware it affects is present, showing the value now,
each value allowed on its own line with what it means, and the default.
Enter keeps the value set now, and the question says which it is: "Enter
for [1]". On a new install that is the default. They are
XhciIntelPortSwitch (only with an Intel 7-, 8- or 9-series controller),
XhciInterruptMode, XhciForceBulkOnly, XhciSilentInstall (Windows 98 SE and
ME only, off by default: on does what tools\silent\silent-on.bat does,
and it says first when to turn it on), and XhciCmMode, XhciCmApprove and
XhciCmSafeEject (only with a Thunderbolt or USB4 controller). The release
notes, "Registry settings", say what each does. An answer applies from the
driver's next start. XhciPollFineTimer, XhciSelectiveSuspend,
XhciCmReenum, XhciCmUsb3Tunnel, XhciCmLaneBonding and XhciCmPreOsCleanup
are never asked; /set Name=Value still sets them.

When Windows starts installing a device by itself while the tool runs - a
Found New Hardware window, perhaps behind the tool's - the tool waits for
it (up to 10 minutes) and goes on afterwards; answer that window. It never
installs a device while Windows' own install of it is under way.

At the end it says "Changes were made. Please restart Windows now." (exit
code 3) when it changed anything and finished, "Changes were made, but not
everything is done" when it stopped with steps left for you (do those
first; the next run asks for the restart), and "No changes were made."
when it changed nothing.

The install directory
---------------------

The tool asks where to install the xHCI98 tools, and lists what goes
there: the xHCI98 USB Tree (XHCITR.EXE, or XHCITR64.EXE on 64-bit
Windows), which starts at each login, and on Windows 98 SE and ME the
package's drivers (the INF and SYS files), which the hardware wizards
search. An update replaces them all at once; a file of the same name
that the tool did not put there stops it, and the uninstall removes only
what it put there. Enter takes C:\XHCI98 on
the drive Windows is on, or, on an update, the directory an earlier
install used. /to path gives it without asking; /yes and /defaults take
the default without asking.

The directory must be a full path on a local hard disk, not the drive's
top level, not inside the Windows directory and not inside the package.
On Windows 98 SE and ME each folder name must be a short (8.3) name with
no spaces, because it is what you type into the hardware wizards. The
tool makes the directory if it is missing, and records it in the registry
(HKLM\Software\xhci98, InstallDir) so that an update and the uninstall
find it.

An earlier test build put the tools in C:\WINDOWS\XHCI98 (your Windows
directory). An update offers that directory as the default; choose
another and the tool moves them: it closes the xHCI98 USB Tree, copies it
to the new directory, points its start at login there, then deletes the
old copy and, if it is empty, the old folder. If the xHCI98 USB Tree does
not close, nothing moves and the tool says to close it and run INSTALL
again. On Windows 2000 to 7 the package copies the drivers are installed
from stay in C:\WINDOWS\XHCI98, because Windows remembers that place.

Thunderbolt and USB4 devices
----------------------------

  INSTALL /devices     shows the Thunderbolt and USB4 devices connected
                       now, and the Windows devices behind each, with
                       their drivers; changes nothing
  INSTALL /eject       lists them, asks which, and removes it safely
  INSTALL /eject 2     the same for device 2 of the list (one question)
  INSTALL /eject id=<instance id> /yes
                       for scripts: the device with that instance id, as
                       /devices prints it, with no question

Windows 98 SE and ME: to remove a Thunderbolt or USB4 device safely, run
INSTALL /eject; these systems have no other way. If a program has a file
open on the device, Windows refuses and nothing changes: close it and try
again. When the tool says "You can now unplug the device.", unplug it.
Until then, Device Manager's Refresh can start it again.

Windows 2000 to 7: Safely Remove Hardware works as usual; INSTALL /eject
also asks Windows to stop a device it does not list. Run it as an
administrator on 2000 and XP.

Exit codes for these: 0 listed, or ejected; 1 refused or cancelled (or
Windows answered that the device is gone or cannot be removed), nothing
changed; 2 Windows began to stop the device and could not finish - shut
Windows down before unplugging it; 4 Windows refused because the device
is in use, nothing changed.

The text can be saved to a file as well (INSTALL /eject > EJECT.TXT): the
questions still show on the screen, and the file gets the same text.

Uninstall
---------

INSTALL /uninstall first warns that on a machine whose only USB controller
is xHCI the USB keyboard and mouse stop working until another driver is
installed, and goes on only when you type REMOVE. On Windows 2000 to 7 it
removes, in this order and stopping at the first step that fails or waits
for a restart: this package's filter from a Thunderbolt port, every device
entry of the package, its INFs, its services and its files. It never
removes Windows' own usbd.sys and usbui.dll, which other USB drivers use.
Run it again after a restart it asks for. On Windows 98 SE and ME it
removes what it placed itself and prints the Device Manager steps. On
every system it removes the xHCI98 tools from the install directory, the
directory itself if nothing else is in it, and its record in the registry.

Exit codes
----------

  0  done, nothing needed a change (or /list)
  1  refused or cancelled; nothing was changed
  2  something failed; the screen and the log say what was done
  3  done, something changed; restart Windows (and run INSTALL.BAT again
     after it if it says so)
  4  done as far as the tool can; it printed what is left for you

The log
-------

Every run but /list adds to XHCIINST.LOG at the root of the system drive
(C:\XHCIINST.LOG on most machines). Attach it to a bug report about an
install.
