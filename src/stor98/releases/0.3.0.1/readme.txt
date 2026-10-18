Stor98 0.3.0.1 (2026-10-16)
===========================

Stor98 is a storage controller driver for NVMe SSDs on old versions
of Windows. It runs any PCI NVMe controller as a SCSI adapter on:

  - Windows 98 SE and Windows ME                 x86\
  - Windows 2000 SP4, XP, Vista and 7 (32-bit)   x86\
  - Windows XP x64, Vista x64 and 7 x64          x64\

x86\ holds one INF for all the 32-bit systems, stor98.mpd for Windows
98 SE and ME, and stor98.sys for Windows 2000 to 7; each system copies
only its own file. x64\ holds the 64-bit stor98.sys and its INF.

It derives from nvme2k by Dominik Behr and its Windows 9x port nvme9x by
SweetLow, and is distributed under the BSD-3-Clause licence in LICENSE.
NOTICE.TXT says where each part came from. Stor98 is not endorsed by
either upstream author.

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

Windows 98 SE / ME
  Device Manager lists the NVMe controller as a PCI device without a
  driver. Open it, choose Update Driver (or Reinstall Driver), pick
  'Display a list of all the drivers in a specific location', then Have
  Disk, and point it at the x86 folder. stor98.mpd is copied to
  WINDOWS\SYSTEM\IOSUBSYS. Restart when asked. The Found New Hardware
  wizard works the same way: search a specific location, the x86
  folder.

Windows 2000, XP, Vista, 7 (32-bit)
  In Device Manager open the controller (often shown as 'Mass Storage
  Controller' or 'PCI Device' with a yellow mark), choose Update Driver,
  install from a specific location, Have Disk, and point it at the x86
  folder. Restart when asked.

Windows XP x64, Vista x64, 7 x64
  As above, using the x64 folder. On Vista x64 and 7 x64 turn on
  test-signing mode first; see the next section. With test-signing
  already on, the driver starts as soon as it is installed and no
  restart is asked for (also with: pnputil -i -a stor98.inf, from an
  elevated Command Prompt in the x64 folder).

TEST SIGNING (64-bit Vista and 7)

x64\stor98.sys is signed with a test certificate of this project,
whose public half is x64\stor98-test.cer. It is not signed by
Microsoft, and no Windows trusts the certificate by itself.

Windows XP x64 and Server 2003 x64 do not check driver signatures, and
neither does any 32-bit Windows: nothing below is needed there. Vista
x64 and 7 x64 load a test-signed driver only in test-signing mode.
Turn it on once:

  1. Open an elevated Command Prompt: Start, type cmd, right-click it,
     Run as administrator.
  2. Type:  bcdedit /set testsigning on
  3. Restart. 'Test Mode' then shows on the desktop, and the driver
     loads at every start with nothing to press.

To check: bcdedit /enum {current} shows 'testsigning Yes'. With
BitLocker on the system drive, suspend BitLocker before changing the
boot setting, or the next start asks for the recovery key.

Without test-signing mode Windows refuses the driver: the controller
shows Code 52 in Device Manager and its disk is not available.

For one start only, without test-signing mode: press F8 at start-up
and choose 'Disable Driver Signature Enforcement'.

TURNING TEST-SIGNING OFF AGAIN

Stop using the driver first, then turn the mode off:

  1. Uninstall stor98 (Device Manager, the controller, Uninstall), or
     in an elevated Command Prompt: sc config stor98 start= disabled
     (the space after 'start=' is required).
  2. bcdedit /set testsigning off, and restart.

Leaving Stor98 installed is not harmful: Windows starts, and the
controller shows Code 52.

Setup still warns that it cannot verify the publisher, because the
package has no catalog; choose 'Install this driver software anyway'.

Importing stor98-test.cer is optional and not needed: test-signing
mode loads the driver without it, and it does not remove that warning.
Imported into 'Trusted Root Certification Authorities' and 'Trusted
Publishers' (certmgr.msc) it makes Windows show the signer as trusted,
and makes the machine trust anything signed with this certificate.
That is your decision; it is not recommended.

CHECKING THE FILES

SHA256SUMS.txt lists the SHA-256 of every file in this directory. On a
modern system: certutil -hashfile <file> SHA256, or sha256sum -c
SHA256SUMS.txt.

RELEASE HISTORY

## 0.3.0.1 - 2026-10-16

A maintenance release, mostly for Windows 98 SE / ME with a connection
manager; on Windows 2000 to 7 only the disk name of models with an
underscore, the version number and the copyright text change.

- Windows 98 SE / ME with a connection manager: the driver now answers
  the connection manager's "link down" statement in every state,
  including after Windows has shut the drive down, after the
  controller failed or stopped answering, and while or after the
  connection manager ejects the disk. The connection manager then
  removes the device at once instead of after its timeout (a few
  seconds). The 0.3.0.0 known limit about this is gone.
- Windows 98 SE / ME: the driver no longer waits for its shared
  memory-accounting lock. It tries once and finishes the bookkeeping
  on its next timer tick, so no driver callback busy-waits longer than
  Windows allows (0.3.0.0 could, by about a millisecond, when two
  adapters were busy at once). Nothing changes in how memory is kept
  or released.
- The disk's name now also splits the model number at an underscore
  when one comes before the first space: an SK hynix drive reported as
  "SKHynix_HFS256GD4..." shows as SKHynix HFS256GD4... instead of
  NVMe SKHynix_HFS256GD. Such disks (also WD_BLACK models), and disks
  whose model has an underscore among the spaces after its first word,
  appear once more as a new device after the update; the data is not
  affected.
- The copyright line of the project in LICENSE and in the driver's
  version information (Driver File Details) now names Stor98; the
  upstream copyright lines are unchanged.

Known limit: drives formatted with 4096-byte sectors (4Kn) are not yet
supported. The driver reports the drive's own sector size instead of
presenting 512-byte sectors, so Windows may fail to use such a drive.
Most NVMe drives use 512-byte sectors; check with the drive's tools if
unsure. Support is planned for a later release.

## 0.3.0.0 - 2026-10-16

The first release of Stor98. The project was formerly named nvme98; it
was renamed before its first public release, because NVM Express, Inc.
does not allow "NVMe" in product names. Builds 0.1.0.0 and 0.1.1.0 below
were made under the former name and never published; a 0.2.0.0 was
never made. There is nothing to migrate from them.

NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is
independent and not endorsed by NVM Express or Microsoft.

- New names: stor98.mpd (Windows 98 SE / ME), stor98.sys (Windows 2000
  to 7), stor98.inf; the Windows 2000 to 7 service is stor98.
- A new request engine and adapter life cycle: requests are checked,
  split and completed with the right status; a controller that stops
  answering is reset within bounded time; a disk that disappears (for
  example an unplugged Thunderbolt enclosure) no longer hangs Windows:
  new requests fail at once and requests already sent are completed
  once the driver knows the disk can no longer write into their memory,
  or after a 5-second deadline.
- Windows 98 SE / ME: the memory the controller uses is kept by the
  driver itself, so a removal cannot hand it back to Windows while the
  controller may still write to it. With xhci98's connection manager
  the driver learns when the PCIe link went down, and an eject from the
  connection manager first flushes the drive and shuts it down cleanly.
- Windows ME: eject the disk before unplugging it (readme.txt, WINDOWS
  ME: EJECT BEFORE UNPLUGGING). A 'Disk Write Error' after a removal
  without an eject is data ME's file system still held in its own
  write-behind cache, which the driver never received. Turning off
  ME's write-behind caching avoids it, at a cost in speed. On ME the
  driver also turns the drive's own write cache off by default; the
  REG_SZ value WriteThrough (auto, off, cacheoff, fua) in
  HKLM\System\CurrentControlSet\Services\stor98 changes that.
- The disk's name in Device Manager now splits the model number at its
  first word ("QEMU NVMe Ctrl" was "QEMU NVM e Ctrl"). Windows sees an
  existing disk once as a new device after the update; the data on it
  is not affected.
- Shutdown and stop flush the drive and shut it down cleanly; resume
  from standby and hibernation re-initialises the controller.

Known limits:

- Use only drives formatted with 512-byte sectors. A drive formatted with
  4 KiB sectors is reported with 4 KiB sectors, which most of these
  Windows versions cannot use.
- Only the first namespace (NSID 1) is used.
- Data disks only: Windows cannot start from a disk on a Stor98
  controller.
- Windows 98 SE / ME with a connection manager: if the link goes down
  after Windows has already shut the drive down, or after the
  controller failed to start, the connection manager waits its timeout
  (a few seconds) before it removes the device.

## 0.1.1.0 - 2026-10-14

A packaging and documentation fix; the driver code is the same as in
0.1.0.0 apart from its version number.

- Windows 2000 to 7: nvme98 is now installed as a demand-start driver
  (Plug and Play loads it when it finds the NVMe controller) instead of
  boot-start. It is for data disks only; Windows cannot start from an
  NVMe disk with nvme98.
- Vista x64 and 7 x64: with 0.1.0.0 installed, starting Windows without
  test-signing mode made Windows 7 x64 fail to start: Startup Repair ran
  and System Restore removed the driver. With 0.1.1.0 Windows starts and
  the controller shows Code 52. Installing 0.1.1.0 over 0.1.0.0 changes
  the service to demand-start.
- readme.txt: the way to turn test-signing off was wrong. Uninstall or
  disable nvme98 first (sc config nvme98 start= disabled), then
  bcdedit /set testsigning off. It also says how to recover a 0.1.0.0
  system, and that in test-signing mode the driver starts as soon as it
  is installed, without a restart.
- Windows 98 SE / ME: no change apart from the version number.

Known limits, unchanged from 0.1.0.0:

- Use only drives formatted with 512-byte sectors. A drive formatted with
  4 KiB sectors does not work and can hang or crash Windows.
- Removing the drive while Windows is using it (for example unplugging a
  Thunderbolt enclosure) is not handled and can hang Windows. Stop the
  device first (Windows 2000 and later: disable it in Device Manager).
- Only the first namespace (NSID 1) is used.

## 0.1.0.0 - 2026-10-14

The first build of nvme98: the nvme9x/nvment driver by SweetLow (itself a
port of nvme2k 1.0.0.2 by Dominik Behr) built for all three targets from
one source tree, renamed nvme98. It behaves as the code it was forked
from; none of the planned fixes are in it yet.

- Windows 98 SE / ME: nvme98.mpd, as SweetLow's nvme2k.mpd, without the
  NVMEVSD.VXD helper.
- Windows 2000 to 7, 32-bit: nvme98.sys.
- Windows XP x64, Vista x64, 7 x64: nvme98.sys, the first 64-bit build.

Known limits, all planned for later versions:

- Use only drives formatted with 512-byte sectors. A drive formatted with
  4 KiB sectors does not work and can hang or crash Windows.
- Removing the drive while Windows is using it (for example unplugging a
  Thunderbolt enclosure) is not handled and can hang Windows. Stop the
  device first (Windows 2000 and later: disable it in Device Manager).
- Only the first namespace (NSID 1) is used.
