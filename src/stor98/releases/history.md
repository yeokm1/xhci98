# Release history

One entry per version, newest first, written for the person installing
the driver; plain ASCII within 78 columns. Since 3.1.0.0 Stor98 is built
and released as part of xhci98, with xhci98's version.

## 3.2.0.0 - not yet released

- Windows 8, 8.1 and 10 (below build 22000): xhci98's installer
  offers Stor98 there (unticked) for NVMe drives behind a Thunderbolt
  or USB4 tunnel; on 8.1 and 10 it replaces Windows' own NVMe driver on
  such a drive. The 32-bit driver ran on Windows 8 and Windows 10 22H2
  (32-bit) in the emulator unchanged. From this release the 64-bit
  package, with its catalog (below), installs into the driver store
  on 8.1 x64 and 10 x64; it has not yet run a drive there, nor on 8
  x64. 32-bit Windows 8.1 is untested.
- The INFs' comments name Windows 8, 8.1 and 10; nothing they install
  changed.
- 64-bit Windows 8, 8.1 and 10: stor98.inf names a catalog,
  stor98.cat, signed with the same test certificate as the driver.
  Those systems install no driver package without one in
  test-signing mode.
- Kernel DMA Protection (64-bit Windows 10 1803 and later): stor98.inf
  declares the driver compatible with DMA remapping, so on a machine
  with the protection on, an NVMe drive behind a Thunderbolt or USB4
  tunnel need not wait for a user to sign in. Not yet tried on such
  a machine.
- A read-only counter answer for xhci98's disk benchmark (XHCIPERF):
  Stor98 answers READ BUFFER (buffer ID 53h) from memory with the sizes
  of the requests it received and of the commands it sent to the drive,
  the splits, the physical layout of each request's buffer, the queue
  depth reached, the errors, its limits and, on Windows 98 SE and ME,
  what the system's disk layer hands it. INQUIRY lists a vendor page,
  C9h, that identifies the adapter. No NVMe command is sent for either,
  nothing is written, and nothing else changes (docs/PLAN.md MD-8).

## 3.1.0.0 - 2026-10-19

The first Stor98 built with xhci98 and released in its package, at the
package's version. 3.1.0.0 is the next version after 0.4.0.0: there is
no Stor98 1.x or 2.x, and an installed 0.4.0.0 (or 0.3.0.x) is older,
so xhci98's installer updates it. The connection manager's link-state
interface (interface 4) is unchanged.

- Version and date: stor98.mpd, both stor98.sys and stor98.inf carry
  xhci98's version and date, like every other file of the package.
- Signing: the 64-bit stor98.sys is signed with the same test
  certificate as xhci98's own 64-bit drivers, in the same step;
  xhci98-test.cer covers it. Stor98's own certificate is retired and
  stor98-test.cer is no longer shipped. Test-signing mode on Vista x64
  and 7 x64 loads it as before.
- Windows 98 SE / ME: when a request's data must go through the
  driver's own bounce buffer, the copy now follows the request's
  scatter/gather list as the port driver does, instead of assuming one
  contiguous buffer, so requests the system splits into several pieces
  are copied correctly; a buffer that cannot be resolved fails the
  request instead of copying a guessed address.
- The driver file now names which build it is (free, checked or a
  test-only build) in a marker string, which xhci98's build checks.
- Write requests with Force Unit Access (FUA) set now reach the drive
  as FUA writes, and a read with FUA set is passed on too; MODE SENSE
  reports that FUA is supported, the drive's write cache state (from
  the controller and the write-through setting) and the values that
  can really be changed.
- INQUIRY: requests for vital product data are now decoded correctly,
  with the supported-pages, serial number and device identification
  pages; refused commands return sense data saying why.
- A namespace in a format the driver cannot present is refused instead
  of being reported with 512-byte sectors. Drives formatted with 4096-
  byte sectors (4Kn) are still not supported.
- Finding the controller: a 64-bit BAR0 is sized correctly (with decode
  off while sizing), configuration reads are checked, Windows 2000 and
  later keep the interrupt Plug and Play assigned, and a slot that is
  not Stor98's is reported as not found instead of starting a bus scan.
- Recovery: a flush while the controller has failed succeeds only when
  nothing was left unwritten; re-enabling after a shutdown replays the
  held requests instead of reporting a bus reset; nothing of the drive's
  identity is carried into a new start; tunable numbers that are too
  large are capped instead of wrapping; a busy drive's retries are
  counted per request.
- Windows 98 SE / ME with xhci98's connection manager: a newer link
  generation without the "link down, quiesced" statement counts as
  proof only after the drive stopped answering; while it answers, it is
  treated as a different drive. A quarantined memory block is no longer
  released by a later generation alone.
- Windows 2000 to 7: the connection manager's table is accepted only at
  a system-space address.
- Device Manager names the product "Stor98" in the disk and file
  descriptions, and the file description names the SSDs, not the
  protocol.

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
