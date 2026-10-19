Stor98, bundled with xhci98
===========================

Stor98 is a storage controller driver for NVMe SSDs on old versions
of Windows, for use with xhci98. It runs a PCI NVMe controller, such as
the drive in a Thunderbolt or USB4 enclosure, as a SCSI adapter on:

  - Windows 98 SE and Windows ME                 the 32-bit package
  - Windows 2000 SP4, XP, Vista and 7 (32-bit)   the 32-bit package
  - Windows XP x64, Vista x64 and 7 x64          the 64-bit package

In the 32-bit package stor98.inf serves every 32-bit system, with
stor98.mpd for Windows 98 SE and ME and stor98.sys for Windows 2000 to
7; each system copies only its own file. In the 64-bit package
stor98.inf and stor98.sys are the 64-bit driver.

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

This is an early build for testing. Do not use it with data you cannot
afford to lose, and do not install it on a system that boots from an
NVMe disk through another driver.

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

INSTALLING

The xhci98 installer (SETUP.EXE) installs Stor98 with the Thunderbolt 3
/ USB4 support: its box sits under the Thunderbolt 3 / USB4 one on the
Components page and is ticked with it. It installs Stor98 only on NVMe
drives behind a Thunderbolt or USB4 tunnel, and updates an earlier
Stor98 the same way. Stor98 is not for internal NVMe drives: do not
install it on one, and decline it if Windows offers it for one. By
hand, for a drive behind a tunnel:

Windows 98 SE / ME
  Device Manager lists the NVMe controller as a PCI device without a
  driver. Open it, choose Update Driver (or Reinstall Driver), pick
  'Display a list of all the drivers in a specific location', then Have
  Disk, and point it at the 32-bit package's folder. stor98.mpd is
  copied to WINDOWS\SYSTEM\IOSUBSYS. Restart when asked. The Found New
  Hardware wizard works the same way: search a specific location, that
  folder.

Windows 2000, XP, Vista, 7 (32-bit)
  In Device Manager open the controller (often shown as 'Mass Storage
  Controller' or 'PCI Device' with a yellow mark), choose Update Driver,
  install from a specific location, Have Disk, and point it at the
  32-bit package's folder. Restart when asked.

Windows XP x64, Vista x64, 7 x64
  As above, using the 64-bit package's folder. On Vista x64 and 7 x64
  turn on test-signing mode first, as for xhci98's own drivers; see
  the next section.

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

Stor98 needs no settings; every value below is optional. The INF writes
none of them, so an absent value means the built-in default on every
system, and a later Stor98 with a better default applies it without you
doing anything. Updating the driver keeps any value you set.

Windows 98 SE / ME: values with the 9x names below in
HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98, as a string
(REG_SZ, numbers in decimal) or, for the numeric ones, a DWORD. Read
whenever the controller starts (at boot, or when it is plugged in).

Windows 2000 to 7: one REG_SZ value DriverParameter in
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\stor98\
Parameters\Device, holding lower-case key=value pairs separated by
semicolons, for example maxtransferkb=128;pollmode=1. Read when the
controller starts (at boot, or after disabling and enabling it; a resume
keeps what was read at the start).

  9x name / NT key        Unit  Default            Range
  ----------------------  ----  -----------------  ------------------------
  StorMaxTransferKB       KiB   512 on 98 SE / ME  64 to the default, in
  maxtransferkb=                256 on 2000 to 7   multiples of 4; anything
                                                   else is refused and the
                                                   default stands
  StorWriteThrough        -     auto               auto, off, cacheoff, fua
  writethrough=                 (cacheoff on ME,   (string only on 9x); an
                                off elsewhere)     unknown word is auto
  StorPollMode            -     0                  0 = poll only without an
  pollmode=                                        interrupt, 1 = always
  StorPollBusyUs          us    1000               1000-100000, 0 = default;
  pollbusyus=                                      2000 to 7 use at least
                                                   10 ms (their clock tick)
  StorPollIdleMs          ms    1000               100-60000, 0 = no polling
  pollidlems=                                      when idle
  StorPollSpinUs          us    500 on 2000 to 7,  0-500, 0 = none (check
  pollspinus=                   0 on 98 SE / ME    for a completion right
                                                   after a request)
  (NT only) gone=         ms    5000               1-600000: how long held
                                                   requests wait for proof
                                                   after the drive is gone;
                                                   advanced: leave at the
                                                   default
  (NT only) idle=         ms    1000               10 and up: write-idle
                                                   time before a flush

Polling values outside their range are clamped to the nearer end;
refusals and clamps are recorded only in the driver's debug log.

Write-through: cacheoff turns the drive's volatile write cache off (fua
if the drive refuses), fua marks every write Force Unit Access, off
leaves the drive as it is.

Polling: when Windows gives the controller no usable interrupt (no
interrupt pin, as on an MSI-only controller, none assigned, or no
interrupt line set), Stor98 polls the controller for completed requests
instead.

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
