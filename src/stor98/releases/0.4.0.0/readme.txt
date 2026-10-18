Stor98 0.4.0.0 (2026-10-16)
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

CHECKING THE FILES

SHA256SUMS.txt lists the SHA-256 of every file in this directory. On a
modern system: certutil -hashfile <file> SHA256, or sha256sum -c
SHA256SUMS.txt.

RELEASE HISTORY

## 0.4.0.0 - 2026-10-16

- Windows 2000 to 7 with xhci98 3.0.0.0 or later: the driver now reads
  the connection manager's link-state channel there too (0.3.0.1 read it
  on Windows 98 SE / ME only). When the connection manager states that
  the drive's PCIe link went down, the requests the driver holds
  complete at once and the connection manager removes the device
  without waiting for the driver's 5-second deadline (measured in the
  emulator on Windows 2000 and XP x64: the driver acknowledged within
  about 0.1 s of the statement, 130 to 210 ms after the drive's reset).
  xhci98 makes that statement only where it can prove the drive was
  reset (today: its emulator, on Windows 2000 and XP x64); elsewhere,
  and without xhci98, the 5-second deadline applies as before. There is
  no clean-eject handshake on Windows 2000 to 7: an eject there is
  Windows' own (flush and stop). A connection manager of another
  interface version is ignored.
- Windows 98 SE / ME with xhci98 3.0.0.0: a drive plugged in after
  Windows started is now bound to the connection manager's link-state
  channel. The connection manager can announce such a drive only after
  Windows has started the driver for it, so the driver looks for the
  announcement again for up to 10 seconds (about 0.2 s in the
  emulator); before, that drive ran without the channel. A drive
  unplugged and plugged in again in the same session is bound to its
  new connection, never to the previous one. xhci98 3.0.0.0 makes no
  link-down statement on these systems yet, so the 5-second deadline
  still applies there.
- A polling mode: when Windows gives the controller no usable interrupt
  (no interrupt pin, as on an MSI-only controller, none assigned, or no
  interrupt line set), the driver polls the controller for completed
  requests instead of failing to work. It can also be forced for
  testing. Four values control it (readme.txt, SETTINGS); the defaults
  differ between Windows 98 SE / ME and Windows 2000 to 7. In the
  emulator, polling on Windows 2000 with the
  default 0.5 ms spin ran at about the interrupt mode's speed.
- An interrupt report: at every start the driver records the
  controller's interrupt pin and line, whether it has MSI / MSI-X, and
  whether it uses the interrupt or polls, in the registry value
  "StorInterrupt <bus>.<device>.<function>" under
  HKLM\System\CurrentControlSet\Services\stor98, for problem reports.
- Windows 98 SE / ME: the driver's timer could not be made to fire
  sooner once a longer wait was set (the 9x port does not replace a
  pending timer request), so a request could wait for a slow tick. The
  driver now cancels the pending timer first. Together with the larger
  transfers of this release, sequential throughput on Windows 98 SE in
  the emulator rose from 15-50 MB/s (0.3.0.1) to 26-76 MB/s (emulator
  figures, relative only; ME unchanged within the emulator's noise).
- Settings (README.md, Settings; readme.txt, SETTINGS): the Windows 98
  SE / ME values now start with Stor (write-through is
  StorWriteThrough). New:
  StorMaxTransferKB (Windows 98 SE / ME) and maxtransferkb= (Windows
  2000 to 7, in DriverParameter) lower the largest request Windows
  sends the driver, from 64 KiB up to the default (512 KiB on 98 SE /
  ME, 256 KiB on 2000 to 7) in steps of 4 KiB; any other value is
  ignored. The INF sets none of these values, and an update keeps
  those you set.

Updating (known issue on Windows 98 SE / ME): update the driver with the
drive unplugged, or with nothing using its volume, then shut down and
start again rather than restart with the drive attached. If the drive
then reads "not ready", unplug it, restart, and plug it in again.
Observed in the emulator after an in-place update with the volume in
use and a warm restart: ME stopped at restart with a VCACHE error and
then hung in ScanDisk, or the volume came back "not ready". Whether the
update or the warm restart causes it was not separated.

With xhci98's connection manager (any Windows), changing the driver of a
tunnelled drive is taken as an eject and the tunnel is torn down: unplug
the drive and plug it in again after the update.

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

The first release of Stor98.

NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is
independent and not endorsed by NVM Express or Microsoft.

- Files: stor98.mpd (Windows 98 SE / ME), stor98.sys (Windows 2000
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
