Stor98, bundled with xhci98
===========================

Stor98 is a storage controller driver for NVMe SSDs on old versions
of Windows, for use with xhci98. It runs a PCI NVMe controller, such as
the drive in a Thunderbolt or USB4 enclosure, as a SCSI adapter on:

  - Windows 98 SE and Windows ME                 98-me\stor98\
  - Windows 2000 SP4, XP, Vista, 7, 8, 8.1      nt-x86\stor98\
    and 10 (32-bit; 10 below build 22000)
  - Windows XP x64, Vista x64, 7 x64, 8 x64,    nt-x64\stor98\
    8.1 x64 and 10 x64 (10 below build 22000)

Each folder holds its own stor98.inf: in 98-me\stor98\ the Windows 98
SE and ME driver, stor98.mpd; in nt-x86\stor98\ the 32-bit stor98.sys
for Windows 2000 to 10; in nt-x64\stor98\ the 64-bit stor98.sys.

Stor98 is built with xhci98 and carries the xhci98 package's version
and date: its files and its INF say the same version as xhci98.sys.
Until xhci98 3.0.0.0 it was released on its own, last as Stor98
0.4.0.0; 3.1.0.0 is the next version after 0.4.0.0, and the installer
updates an earlier Stor98 to it.

It derives from nvme2k by Dominik Behr and its Windows 9x port nvme9x by
SweetLow, and is distributed under the BSD-3-Clause licence in
stor98-LICENSE.txt. stor98-NOTICE.txt says where each part came from.
Stor98 is not endorsed by either upstream author.

NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is
independent and not endorsed by NVM Express or Microsoft.

STATUS

Do not use Stor98 with data you cannot afford to lose, and do not
install it on a system that boots from an NVMe disk through another
driver.

Stor98 is for data disks only: Windows cannot start from a disk on a
Stor98 controller. On Windows 2000 and later it is installed as a
demand-start driver, which Plug and Play loads when it finds the NVMe
controller; if Windows refuses the driver, only that device stops and
Windows still starts.

WINDOWS ME: EJECT BEFORE UNPLUGGING

On Windows ME, eject the disk cleanly (with the eject of the USB4 /
Thunderbolt host driver) before you unplug it: the clean eject makes
the drive flush its cache first. A 'Disk Write Error' after a disk is
unplugged without an eject is data that ME's file system (VFAT) still
held in its write-behind cache: Stor98 never received it, so it could
not write it. If disks may be unplugged without an eject, you can
turn write-behind caching off, at a cost in speed: Control Panel,
System, Performance, File System, Troubleshooting, 'Disable
write-behind caching for all drives'. (The Removable Disk tab's
option covers only drives Windows treats as removable, which a Stor98
disk is not.)

DRIVES WITH 4 KiB SECTORS

A drive set to 4 KiB (4096-byte) logical sectors works on every
system. Windows 98 SE, ME, 2000, XP, Vista and 7 see it with
512-byte sectors; Windows 8, 8.1 and 10 see its own 4096-byte
sectors, as Windows' own NVMe driver does. A drive partitioned on
one group of systems shows as empty or damaged on the other; back
up before moving it between them.

INSTALLING

The xhci98 installer (SETUP.EXE) installs Stor98 with the Thunderbolt 3
/ USB4 support: its box sits under the Thunderbolt 3 / USB4 one on the
Components page and is ticked with it. It installs Stor98 only on NVMe
drives behind a Thunderbolt or USB4 tunnel, and updates an earlier
Stor98 the same way. Stor98 is not for internal NVMe drives: do not
install it on one, and decline it if Windows offers it for one. On
Windows 2000 to 8 stor98.inf matches only the id xhci98 gives a drive
behind a tunnel (PCI\XHCI98_TUNNEL&CC_010802), so Windows does not offer
it for an internal drive; on 98 SE and ME its stor98.inf matches every
NVMe controller. By hand, for a drive behind a tunnel:

Windows 98 SE / ME
  Device Manager lists the NVMe controller as a PCI device without a
  driver. Open it, choose Update Driver (or Reinstall Driver), pick
  'Display a list of all the drivers in a specific location', then Have
  Disk, and point it at the 98-me\stor98 folder.
  stor98.mpd is copied to WINDOWS\SYSTEM\IOSUBSYS. Restart when asked.
  The Found New Hardware wizard works the same way: search a specific
  location, that folder.

Windows 2000, XP, Vista, 7, 8, 8.1, 10 (32-bit)
  In Device Manager open the controller (often shown as 'Mass Storage
  Controller' or 'PCI Device' with a yellow mark), choose Update Driver,
  install from a specific location, Have Disk, and point it at the
  nt-x86\stor98 folder. Restart when asked.

Windows XP x64, Vista x64, 7 x64
  As above, using the nt-x64\stor98 folder. On Vista x64 and 7 x64
  turn on test-signing mode first, as for xhci98's own drivers; see
  the next section.

WINDOWS 8, 8.1 AND 10

The installer offers Stor98 on Windows 8, 8.1 and 10 (below build
22000) too: ticked with the Thunderbolt support on 8 and 8.1
(/nostor98 leaves it out), unticked on 10, where Windows' own stornvme
stays the tunnelled drive's driver (xhci98's roadmap 43.59). Windows
8 has no NVMe driver of its own; Windows
8.1 and 10 run their own (stornvme), and Stor98 takes its place on a
drive behind a tunnel. The 64-bit package carries a catalog file,
stor98.cat, which Windows 8 x64, 8.1 x64 and 10 x64 require in
test-signing mode (without it: 'The third-party INF does not contain
digital signature information'): keep it beside stor98.inf. As on
Vista x64 and 7 x64, the 64-bit stor98.sys loads there in
test-signing mode (next section), or on a start with driver
signature enforcement disabled, as xhci98's own 64-bit drivers do;
xhci98's readme says what its installer needs there.

TEST SIGNING (64-bit Vista and 7)

The 64-bit stor98.sys is signed with the same test certificate as
xhci98's own 64-bit drivers, in the same step: one signature covers
every driver of the 64-bit package. xhci98-test.cer beside them is that
certificate's public half. It is not signed by Microsoft, and no
Windows trusts the certificate by itself. Stor98 no longer has a
certificate of its own (Stor98 0.4.0.0 and earlier shipped
stor98-test.cer).

Windows XP x64 and Server 2003 x64 do not check driver signatures, and
neither does any 32-bit Windows. Vista x64 and 7 x64 load a
test-signed driver only in test-signing mode: xhci98's readme says how
to turn it on (bcdedit /set testsigning on, then restart), and the
setting that loads xhci98's drivers loads Stor98 too. Without it
Windows refuses the driver: the controller shows Code 52 in Device
Manager and its disk is not available.

SETTINGS

Stor98 needs no settings; every value below is at its default as
installed. Since xhci98 3.4.0.0 the INF writes each one at its default
for the system, so that it is already there, ready to change: on 98 SE /
ME each value, on 2000 to 10 one DriverParameter holding every key. An
install or update writes a value only where it is missing: a value you
set, or a DriverParameter you wrote (whole), stays, and a key missing
from it still takes its default. So a value the install wrote keeps that
number, and a later Stor98 with a better default does not apply it by
itself; to go back to the default, delete the value (or the key). An
absent value means the built-in default.

Windows 98 SE / ME: values with the 9x names below in
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98, as a string
(REG_SZ, numbers in decimal) or, for the numeric ones, a DWORD. Read
whenever the controller starts (at boot, or when it is plugged in).

Windows 2000 to 10: one REG_SZ value DriverParameter in
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\stor98\
Parameters\Device, holding lower-case key=value pairs separated by
semicolons, for example stormaxtransferkb=128;storpollmode=1. Read when the
controller starts (at boot, or after disabling and enabling it; a resume
keeps what was read at the start). In the table '2000 to 7' covers
Windows 8, 8.1 and 10 as well.

  9x name / NT key        Unit  Default            Range
  ----------------------  ----  -----------------  ------------------------
  StorMaxTransferKB       KiB   2048 on 98 SE / ME 64 to the default, in
  stormaxtransferkb=            256 on 2000 to 7   multiples of 4; anything
                                                   else is refused and the
                                                   default stands
  StorWriteThrough        -     auto               auto, off, cacheoff, fua
  storwritethrough=             (cacheoff on ME,   (string only on 9x); an
                                off elsewhere)     unknown word is auto
  StorPollMode            -     0                  0 = poll only without an
  storpollmode=                                    interrupt, 1 = always
  StorPollBusyUs          us    1000               1000-100000, 0 = default;
  storpollbusyus=                                  2000 to 7 use at least
                                                   10 ms (their clock tick)
  StorPollIdleMs          ms    1000               100-60000, 0 = no polling
  storpollidlems=                                  when idle
  StorPollSpinUs          us    500 on 2000 to 7,  0-500, 0 = none (check
  storpollspinus=               0 on 98 SE / ME    for a completion right
                                                   after a request)
  StorIntWatchdog         -     1                  1 = poll for the rest of
  storintwatchdog=                                 the start once interrupts
                                                   stop arriving, 0 = never
  StorGoneMs              ms    5000               1-600000: how long held
  storgonems=                                      requests wait for proof
                                                   after the drive is gone;
                                                   advanced: leave at the
                                                   default
  StorWriteIdleMs         ms    1000               10 and up: write-idle
  storwriteidlems=                                 time before a flush
  StorHold9xMs            ms    4000               1000-10000, anything
  98 SE / ME only                                  else is refused and the
                                                   default stands: how long
                                                   a request waits for room
                                                   in the driver before it
                                                   is answered busy (2000
                                                   to 7 use each request's
                                                   own timeout); advanced
  StorTakeFirmware        -     0                  0 = leave a controller
  98 SE only, not ME                               the BIOS set up, 1 =
                                                   take it anyway;
                                                   advanced: can hang
                                                   Windows' start or
                                                   corrupt data (below)

Polling values outside their range are clamped to the nearer end;
refusals and clamps are recorded only in the driver's debug log.

A controller the BIOS set up (98 SE only): Stor98 leaves an NVMe
controller it finds already enabled or ready, because the BIOS may
still be serving its drive through INT 13h; the drive then runs in
MS-DOS compatibility mode and Stor98's controller shows Code 10.
On Windows ME it always leaves it: StorTakeFirmware and
stortakefirmware=1 are ignored there, because taking such a controller
can hang ME's start, and StorFind says "... ignored on Windows ME".
StorTakeFirmware = 1 takes every such controller anyway, at its next
start; to take one controller only, add the string value
AdapterSettings = stortakefirmware=1 to that controller's key under
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\
SCSIAdapter (the one whose DriverDesc is Stor98's; separate other
entries with semicolons), and stortakefirmware=0 there leaves that
controller whatever StorTakeFirmware says. Only an exact 1 or 0
counts. Taking it can hang Windows' start or corrupt data if the BIOS
still uses the drive (a DOS box or MS-DOS mode, or the drive the
computer started from): leave it off unless it has been tried on this
computer. The value StorFind <bus>.<device>.<function> beside the
settings records each controller's last start ("taken: already set
up, overridden by ...").

Renamed in xhci98 3.3.0.0, so that every name starts with Stor and the
two families match: the NT keys maxtransferkb=, writethrough=,
pollmode=, pollbusyus=, pollidlems=, pollspinus=, gone= and idle= are
now stormaxtransferkb=, storwritethrough=, storpollmode=,
storpollbusyus=, storpollidlems=, storpollspinus=, storgonems= and
storwriteidlems=, and 98 SE and ME gain StorGoneMs and StorWriteIdleMs.
98 SE and ME also gain StorHold9xMs, new in xhci98 3.3.0.0.
A key of the old names is ignored: rename it in DriverParameter.

Write-through: cacheoff turns the drive's volatile write cache off (fua
if the drive refuses), fua marks every write Force Unit Access, off
leaves the drive as it is.

Polling: when Windows gives the controller no usable interrupt (no
interrupt pin, as on an MSI-only controller, none assigned, or no
interrupt line set), Stor98 polls the controller for completed requests
instead. With StorIntWatchdog / storintwatchdog= 1 it also polls, until
the controller next starts, once its interrupts stop arriving while
requests complete.

INTERRUPT REPORT

At every start (on Windows 2000 to 7 not at a resume from hibernation)
Stor98 records what interrupt it found, in a REG_SZ value named
'StorInterrupt <bus>.<device>.<function>' in
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98, for
example 'pin 1 line 11 msi no msix yes port 11/11 interrupt': the
controller's Interrupt Pin and Line, whether it has MSI and MSI-X
(not used by Stor98), the interrupt Windows gave the driver, and
'interrupt' or 'polling (reason)'. When reporting a problem, include
it: open it with Regedit, or in a Command Prompt run (one line)
  regedit /e report.reg
  "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98"
and send report.reg.

RELEASE HISTORY

Stor98's release history is src\stor98\releases\history.md in the
xhci98 source; what changed in each xhci98 release, Stor98 included, is
in xhci98's release notes.
