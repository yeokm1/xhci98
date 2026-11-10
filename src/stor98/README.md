# Stor98

Stor98 is a storage controller driver for NVMe SSDs on legacy Windows, built to sit behind hot-pluggable USB4 and Thunderbolt PCIe tunnels.

Stor98 is meant to be used with [xhci98](https://github.com/yeokm1/xhci98) only, as the
storage driver for NVMe SSDs that xhci98's connection manager brings up behind a USB4 or Thunderbolt tunnel. It is not meant to be used standalone. It targets the same systems as xhci98:

| Family | Targets | Binary |
|---|---|---|
| Windows 9x | Windows 98 SE, ME | SCSI miniport (`.mpd`); no helper VxD needed for basic operation ([PLAN.md](docs/PLAN.md) 0.12, MD-5; xhci98's optional `S98VSD.VXD` lets larger requests through, below) |
| NT, 32-bit | Windows 2000 SP4, XP, Vista, 7, 8, 8.1, 10 (below build 22000) | SCSIport miniport (`.sys`) |
| NT, 64-bit | XP x64 / Server 2003 x64, Vista x64, 7 x64, 8 x64, 8.1 x64, 10 x64 (below build 22000) | SCSIport miniport (`.sys`, amd64) |

Windows 8, 8.1 and 10 since xhci98 3.2.0.0 (xhci98's roadmap 43.23; 10 below build 22000,
which xhci98's installer admits beside 8 and 8.1 from its roadmap 43.34): see
[Windows 8, 8.1 and 10](#windows-8-81-and-10) for how Stor98 installs there and what the
64-bit package needs.

Windows NT4 is outside the product scope. The NT4 build path inherited from nvme9x/nvment
is kept only while it costs nothing to maintain; it is not shipped.

## How Stor98 differs from nvme2k and nvme9x

Stor98 is a fork of **nvme9x / nvment** by SweetLow, which ports **nvme2k** 1.0.0.2 by
Dominik Behr to Windows 9x and NT4 (see [Lineage and licensing](#lineage-and-licensing)).
Both work well on a drive that is always present. Stor98 exists for the case they were not
built for: a drive in a USB4 or Thunderbolt enclosure that can be unplugged at any time.
Before the fork, surprise removal during I/O hung Windows 2000 with nvme2k and wedged the
volume on XP x64 with nvme2k 1.2.0.0 ([docs/TESTING.md](docs/TESTING.md)).

What Stor98 adds:

- **Surprise removal is a normal event.** A disk that disappears no longer hangs Windows:
  new requests fail at once, and requests already sent complete only once the driver knows
  the controller can no longer write into their memory (or at a 5-second deadline).
- **No DMA into freed memory.** Memory the controller can write to is never handed back to
  Windows without proof that the controller has stopped; on Windows 98 SE / ME the driver
  keeps its own pool for this.
- **xhci98 integration.** Through the link-state channel of xhci98's connection manager,
  the driver learns when the PCIe link to the drive went down, so the requests it holds
  complete at once instead of at its own 5-second deadline, and the connection manager
  removes the device without waiting. What works depends on both versions:
  - Windows 2000 to 7 with Stor98 0.4.0.0 or later and xhci98 3.0.0.0 or later: the
    driver reads the channel. xhci98 states a link-down only where it can prove the
    drive was reset; elsewhere it says nothing and the driver's 5-second deadline
    applies. There is
    no clean-eject handshake on these systems: an eject is Windows' own (flush and stop).
  - Windows 98 SE / ME: Stor98 reads the channel (and offers the clean-eject handshake
    that flushes and shuts the drive down before the connection manager removes it).
    xhci98 3.0.0.0 publishes the channel there. A drive plugged in after Windows started
    is announced by the connection manager only after Windows has already started the
    driver for it, so Stor98 looks for it again for up to 10 seconds and binds it then.
    A drive present at startup is started before the
    connection manager publishes the channel, so it runs without it. xhci98 3.0.0.0 and
    3.1.0.0 make no link-down statement and no clean-eject request on these systems
    (xhci98's roadmap 42-A.12 and 42-A.13), so the 5-second deadline applies and an eject
    is Windows' own. They also do not yet hold a returning drive's announcement back
    until Windows has removed the previous one, when that removal was vetoed or is slow
    (rule R8c of [`include/stor98cm.h`](include/stor98cm.h); xhci98's roadmap 42-A.12).
- **A new request engine:** requests are checked, split to the controller's limits and
  completed exactly once with the right status; a controller that stops answering is reset
  within bounded time; shutdown, stop and resume flush, shut down and re-initialise the
  drive.
- **Requests of up to 2 MiB on Windows 98 SE / ME** (256 KiB on Windows 2000 to 10;
  a setting can lower either, see [Settings](#settings)). Above 512 KiB this needs
  xHCI98's `S98VSD.VXD`, which the xHCI98 installer offers on 98 SE and ME; without it
  Windows still cuts every request over 512 KiB into 63 KiB pieces. Without it, and
  up to 512 KiB:
  Windows 98 SE / ME passes a request to the driver whole only if it is at most 512 KiB
  and its buffer is page-aligned and physically contiguous enough. Only such requests
  larger than 256 KiB gain from the higher limit; requests of 256 KiB or less are
  unchanged. Larger requests, misaligned buffers and fragmented
  buffers are still cut by Windows into 63 KiB pieces, one at a time, so most other I/O
  is not faster.
- **Every target from one source tree:** Windows 98 SE / ME, Windows 2000 to 10 32-bit, and
  XP x64 to 10 x64, 10 below build 22000 (the 64-bit build follows upstream nvme2k
  1.2.0.0, which nvme9x predates).
- **Removed:** the magic-pattern TRIM of the base, which could overwrite the caller's
  buffer.

The work and the evidence behind each change are in [docs/PLAN.md](docs/PLAN.md).

## Windows 8, 8.1 and 10

Since xhci98 3.2.0.0 (its roadmap 43.23, owner 2026-10-20) xhci98's installer offers
Stor98 on Windows 8, 8.1 and 10 (below build 22000) as on 2000 to 7: on NVMe drives behind
a Thunderbolt or USB4 tunnel only, its box ticked with the Thunderbolt box on 8 and 8.1,
and unticked on 10, where Windows' own `stornvme` stays the tunnelled drive's driver
(xhci98's roadmap 43.59; xhci98's design record 26 section 5.0; `/nostor98` leaves it out).
Windows 8 has no NVMe driver of its own; Windows 8.1 and 10 bind their own `stornvme` to
every NVMe controller, and the installer installs Stor98 over it, forced, on a tunnel drive
it finds at install time.

- **32-bit:** the x86 package installs through the `.NTx86` sections, and SCSIport loads
  and starts Stor98 as on 2000 to 7. Windows 8's disk class driver retries a failed write
  whichever terminal status Stor98 uses (PLAN 2.2).
- **64-bit:** from xhci98 3.2.0.0 `stor98.inf` names a catalog file, `stor98.cat`, which
  Windows 8 x64, 8.1 x64 and 10 x64 require in test-signing mode: without it their driver
  store refuses the package ("The third-party INF does not contain digital signature
  information", `0xE000022F`) before the driver is loaded. Keep `stor98.cat` beside
  `stor98.inf`. As on Vista x64 and 7 x64, the test-signed 64-bit `stor98.sys` loads there
  in test-signing mode, or on a start with driver signature enforcement disabled, as
  xhci98's own 64-bit drivers do; xhci98's installer has its own requirements there (its
  readme). The INF writes no `DmaRemappingCompatible`: with it, Stor98 does not start on
  10 x64 (xhci98's roadmap 43.59s).
- **Windows 11** is not a target (xhci98's installer refuses it; it has its own USB4
  support).

The settings below apply on 8, 8.1 and 10 as on 2000 to 7: the same key, the same defaults.
What was run on these systems is in [docs/TESTING.md](docs/TESTING.md) ("Windows 8 and
8.1", "Windows 10").

## Known limits

- **Drives with 4 KiB logical sectors (4Kn).** Most NVMe drives are formatted with
  512-byte sectors. A drive formatted with 4096-byte sectors works on every system,
  but not every system sees the same sector size: Windows 98 SE, ME, 2000, XP, Vista
  and 7 see 512-byte sectors (Stor98 translates); Windows 8, 8.1 and 10 see the
  drive's own 4096-byte sectors, as Windows' own NVMe driver shows them. Drives with
  1 KiB or 2 KiB sectors are seen with 512-byte sectors everywhere.
  A 4Kn drive partitioned with 512-byte sectors (on 98 SE to 7, or on 8 to 10 with an
  earlier Stor98 test build that showed 512-byte sectors) and one partitioned with
  4096-byte sectors (on 8 to 10, with Stor98 or Windows' own driver) are not
  interchangeable: the other view sees its partitions and files as damaged. Back up
  first. To move a drive freely between them, reformat the drive itself to 512-byte
  sectors with the drive maker's tool or Linux `nvme format`, or partition and format
  it again on the system that will use it.
- **VERIFY reads the drive** (xhci98 3.4.2.0): VERIFY(10) and VERIFY(16) read the blocks
  asked for, by the drive's own Verify command where it has one, otherwise by reads that
  the driver discards, so a scan that uses VERIFY finds an unreadable block: format and
  `chkdsk /r` on Windows 2000 to 10, and MS-DOS FORMAT. ScanDisk on Windows 98 SE and ME
  does not use VERIFY: it reads the drive, and writes to it with write-testing on. A range
  past the end of the drive is refused (LOGICAL BLOCK ADDRESS OUT OF RANGE), and so is a
  compare with the caller's data, a protection check or more than 32 MiB in one VERIFY
  (INVALID FIELD IN CDB). On Windows 98 SE and ME a VERIFY that starts at or past the end
  succeeds without reading, whatever its length (MS-DOS FORMAT asks for such blocks on a
  volume made on another system). VERIFY(6) is refused as an unsupported command
  ([docs/PLAN.md](docs/PLAN.md), "VERIFY").

## Updating the driver

- **Windows 98 SE / ME.** Update Stor98 (or switch to it from another NVMe driver) with
  the drive unplugged, or with nothing using its volume, then restart Windows. If the
  drive then reads "not ready", unplug it, restart, and plug it in again (xhci98
  3.4.3.0; up to 3.4.2.0 this readme asked for a shutdown and a fresh start).
- **With xhci98's own USB4 connection manager.** On a USB4 host run
  by xhci98's connection manager, changing the driver of a tunnelled NVMe drive stops the
  device, which the connection manager takes as an eject: it tears the tunnel down.
  Unplug the drive and plug it in again after the update. Under a Thunderbolt 3 host's
  firmware connection manager the tunnel stays up and the drive comes back with the new
  driver.

## Settings

Stor98 needs no settings: every value below is at its default as installed. Since xhci98
3.4.0.0 the INFs write each one at the default for that system, so that every setting is
already there, ready to change (xhci98 roadmap-usb4 45.50, owner 2026-10-30): on Windows 98
SE / ME each value, on Windows 2000 to 10 one `DriverParameter` string holding every key
below. An install or update writes a value only where it is missing, never over one
already there: a value you set, or a `DriverParameter` you wrote (whole, whatever keys it
holds), stays as it is, and a key missing from your `DriverParameter` still takes its
default. So a value the install wrote keeps that number: a later Stor98 with a better
default does not apply it by itself. To go back to the default, delete the value (or the
key from `DriverParameter`, or `DriverParameter` itself, which the next update writes
again) or set it to the default in the table. An absent value means the built-in
default, as before. The INFs delete none of these values.

One value changed its name: Stor98 0.3.0.0 and 0.3.0.1 read the Windows 98 SE / ME
write-through value as `WriteThrough`, and 0.4.0.0 and later read only
`StorWriteThrough`. A `WriteThrough` value set for an earlier release is ignored after
the update, and the default applies. Rename it to `StorWriteThrough`, with the same string.

Where they are set:

- **Windows 98 SE / ME:** values with the names below in
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98`, as a string (REG_SZ;
  numbers in decimal) or, for the numeric ones, a DWORD. They are read whenever the
  controller starts: at boot, or when it is plugged in.
- **Windows 2000 to 10:** one string value, `DriverParameter` (REG_SZ), in
  `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\stor98\Parameters\Device`, holding
  lower-case `key=value` pairs separated by semicolons, for example
  `stormaxtransferkb=128;storpollmode=1;storpollbusyus=20000`. As installed it reads
  `stormaxtransferkb=256;storwritethrough=auto;storpollmode=0;storpollbusyus=1000;storpollidlems=1000;storpollspinus=500;storintwatchdog=1;storgonems=5000;storwriteidlems=1000`.
  It is read when the controller starts:
  at boot, or after disabling and enabling it in Device Manager (not at a resume from
  standby or hibernation, which keeps what was read at the start).

| Setting | Windows 98 SE / ME value | Windows 2000 to 10 key | Unit | Default | Range | Outside the range |
|---|---|---|---|---|---|---|
| Maximum transfer size | `StorMaxTransferKB` (DWORD or string) | `stormaxtransferkb=` | KiB | 2048 on 98 SE / ME, 256 on 2000 to 10 | 64 to the default, in multiples of 4 | refused: the default stands |
| Write-through | `StorWriteThrough` (string only) | `storwritethrough=` | - | `auto`: `cacheoff` on ME, `off` on 98 SE and on 2000 to 10 | `auto`, `off`, `cacheoff`, `fua` | an unknown word means `auto` |
| Polling mode | `StorPollMode` (DWORD or string) | `storpollmode=` | - | 0 | 0-1 | clamped to the nearer end |
| Polling period while busy | `StorPollBusyUs` (DWORD or string) | `storpollbusyus=` | microseconds | 1000 (1 ms); on 2000 to 10 in effect at least 10 ms | 1000-100000; 0 = the default | clamped to the nearer end |
| Polling period when idle | `StorPollIdleMs` (DWORD or string) | `storpollidlems=` | milliseconds | 1000 (1 s) | 100-60000; 0 = no polling when idle | clamped to the nearer end |
| Completion spin after a request | `StorPollSpinUs` (DWORD or string) | `storpollspinus=` | microseconds | 500 on 2000 to 10, 0 on 98 SE / ME | 0-500; 0 = no spin | clamped to the nearer end |
| Missed-interrupt watchdog (xhci98 3.4.1.0) | `StorIntWatchdog` (DWORD or string) | `storintwatchdog=` | - | 1 | 0-1; 0 = off | clamped to the nearer end |
| Gone deadline (advanced: leave at the default) | `StorGoneMs` (DWORD or string) | `storgonems=` | milliseconds | 5000 (5 s) | 1-600000 | 0 or above the range: the default |
| Write-idle time before a flush | `StorWriteIdleMs` (DWORD or string) | `storwriteidlems=` | milliseconds | 1000 (1 s) | 10 and up | below 10: 10 |
| Hold before busy, 98 SE / ME (advanced: leave at the default) | `StorHold9xMs` (DWORD or string) | none | milliseconds | 4000 (4 s) | 1000-10000 | refused: the default stands |
| Take a controller the BIOS set up, 98 SE only, ignored on ME (xhci98 3.4.3.0; advanced) | `StorTakeFirmware` (DWORD or string), and per controller `stortakefirmware=` in its `AdapterSettings` | none | - | 0 | 0-1 | anything but an exact 1 or 0: no setting (left) |

Each refusal or clamp is recorded in the driver's debug log (the debug-port build), not
shown to the user.

**Renamed in xhci98 3.3.0.0**, so that every name starts with Stor and the two families
match: the Windows 2000 to 10 keys `maxtransferkb=`, `writethrough=`, `pollmode=`,
`pollbusyus=`, `pollidlems=`, `pollspinus=`, `gone=` and `idle=` are now
`stormaxtransferkb=`, `storwritethrough=`, `storpollmode=`, `storpollbusyus=`,
`storpollidlems=`, `storpollspinus=`, `storgonems=` and `storwriteidlems=`, and Windows 98
SE / ME gain `StorGoneMs` and `StorWriteIdleMs`. A key with an old name is ignored after
the update and the default applies: rename it in `DriverParameter`, with the same value.

- **Maximum transfer size** is the largest request Stor98 tells Windows to send it
  (MaximumTransferLength, with as many physical breaks as pages plus one). It can only
  lower the built-in maximum, never raise it: a larger value, a value below 64, one that
  is not a multiple of 4, or a string that is not a plain decimal number is refused and
  the default stands. On Windows 98 SE / ME without `S98VSD.VXD` only aligned,
  physically contiguous requests above 256 KiB gain from more than 256 KiB, and nothing
  gains above 512 KiB (see above); a lower value is for testing or for hardware that
  misbehaves with large requests.
- **Write-through** turns the drive's volatile write cache off (`cacheoff`, checked
  after setting it, with `fua` used if the drive refuses), or marks every write
  Force Unit Access (`fua`), or leaves the drive as it is (`off`). `auto` is `cacheoff`
  only on Windows ME, where a removal that bypasses the connection manager gives the
  driver no chance to flush; elsewhere it is `off`.
  Windows sees the drive's write cache as enabled when the drive has a volatile write
  cache, except under `cacheoff` once the cache is verified off. The setting cannot be
  changed from Windows (the disk's "Enable write caching" box).
- **Gone deadline** is how long requests the driver holds wait, after the drive
  disappeared, for proof that the controller can no longer write into their memory
  before they are failed anyway (see "How Stor98 differs"). It is advanced: leave it
  at the default. A lower value raises the risk the maintainer accepted in decision
  MD-1 (late DMA into memory Windows has reused); a higher value makes a pulled drive
  take longer to fail. **Write-idle time** is how
  long the drive must have had no writes before Stor98 flushes its write cache from
  its timer. Both can be set on Windows 98 SE / ME too since xhci98 3.3.0.0
  (`StorGoneMs`, `StorWriteIdleMs`).
- **Hold before busy** (Windows 98 SE / ME only, new in xhci98 3.3.0.0) is how long a
  request waits for room in the driver, when its limits on requests in flight are
  reached, before it is completed busy and Windows sends it again. On Windows 2000 to 10
  the driver holds each request for most of its own timeout instead, so there is no key
  there. It is advanced: leave it at the default. The range stays below the shortest
  time after which the 98 SE port was seen to reset the bus over a request held too
  long (13.2 s, PLAN "Gone state and its deadline", 9x); a lower value means
  more busy completions while the driver is full.
- **Take a controller the BIOS set up** (Windows 98 SE only, new in xhci98 3.4.3.0;
  PLAN "Taking a controller the BIOS set up") is off by default: Stor98 leaves an NVMe
  controller it finds already enabled or ready (PLAN 3.1), because the BIOS may still be
  serving its drive through INT 13h, and that drive runs in MS-DOS compatibility mode
  with Stor98's controller at Code 10. On Windows ME Stor98 always leaves it and ignores
  both settings below, because taking such a controller can hang ME's start; `StorFind`
  then says "left: already set up, StorTakeFirmware ignored on Windows ME" (or "...
  stortakefirmware=1 ignored ..."). `StorTakeFirmware` = 1 takes every such
  controller at its next start. For one controller alone, add the string value
  `AdapterSettings` = `stortakefirmware=1` to that controller's driver key
  (`HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\SCSIAdapter\NNNN`,
  the one whose `DriverDesc` is Stor98's; other entries separated by semicolons):
  Windows 98 SE hands that string to the driver; `stortakefirmware=0` there leaves
  that controller whatever `StorTakeFirmware` says. Only an exact `1` or `0` counts.
  Taking it disables the controller and sets it up again, so the BIOS's own requests to
  it end: it can hang Windows' start or corrupt data if the BIOS still uses the drive (a
  DOS box or MS-DOS mode, or the drive the computer started from), and a drive the BIOS
  also gave a letter may show twice. Leave it off unless it has been tried on that
  computer. Each controller's last start is recorded as `StorFind
  <bus>.<device>.<function>` in `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98`
  ("taken: already set up, overridden by StorTakeFirmware" or "... by
  stortakefirmware=1"; "left: already set up" when it was not taken), beside `StorFind`,
  the last start of any.

### Polling mode

Stor98 normally completes requests from the controller's interrupt. When Windows gives
the controller no usable interrupt, Stor98 runs without one: it polls the controller for
completed requests, often while requests are outstanding and seldom when idle. This
happens when the controller has no interrupt pin (an MSI-only controller: a SCSI miniport
gets no MSI on these systems), when Windows assigned it no interrupt, or when its
interrupt line is not set. Polling can also be forced, for testing. Five values control
it (table above; [docs/PLAN.md](docs/PLAN.md), "Polling mode"):

- **StorPollMode** (`storpollmode=`): 0 polls only when there is no usable interrupt; 1
  always polls.
- **StorPollBusyUs** (`storpollbusyus=`) is the polling period while requests are
  outstanding. It only takes full effect on Windows 98 SE and ME. On Windows 2000 to 8.1
  the port rounds every timer period up to the OS clock tick (10-15.6 ms), so Stor98
  uses at least 10 ms there and a value below that changes nothing; on Windows 10 Stor98
  asks for the same 10 ms minimum. A shorter period
  completes requests sooner, at a higher CPU cost while busy.
- **StorPollIdleMs** (`storpollidlems=`) is the period with nothing outstanding and nothing
  left to flush. 0 stops polling when idle, as with an interrupt; the timer still runs
  for unflushed writes and for the connection manager's link check.
- **StorIntWatchdog** (`storintwatchdog=`, xhci98 3.4.1.0): with 1, the default, an adapter
  that started on its interrupt and then finds completions at its timer three times within
  2 s with no interrupt taken between switches to the polling mode until its next start
  (docs/PLAN.md, "Missed-interrupt watchdog"); the performance page (XHCIPERF) shows it.
  0 keeps waiting for interrupts.
- **StorPollSpinUs** (`storpollspinus=`) lets the driver keep checking for a completion for
  that long right after sending a request, which helps short requests at a CPU cost (a
  busy-wait of at most that long per request while one is outstanding, only in the
  polling mode). The defaults differ by family: on Windows 2000 to 8.1 the timer cannot run
  faster than the OS clock tick (10 takes 2000's defaults), so the spin is what keeps polling close to interrupt
  throughput; on 98 SE and ME the 1 ms timer works and the CPU is scarcer.

[docs/TESTING.md](docs/TESTING.md), "Polling-mode measurements", compares these settings
with each other.

### Interrupt report

At every start of a controller Stor98 records what interrupt it found, so that a user can
report it. The record is a string value named `StorInterrupt <bus>.<device>.<function>` in
`HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98`, on every system, for
example:

```
StorInterrupt 1.0.0 = pin 1 line 11 msi no msix yes port 11/11 interrupt
StorInterrupt 1.0.0 = pin 0 line 255 msi yes msix yes port 0/0 polling (no interrupt pin)
```

It gives the controller's Interrupt Pin (0 = none, 1-4 = INTA-INTD) and Interrupt Line
from its PCI configuration space, and whether it has the MSI and MSI-X capabilities
(reported only; Stor98 uses neither). `port` is the interrupt level and vector Windows
handed the driver. The last part is the outcome: `interrupt`, or `polling` with its
reason (`forced by StorPollMode` on Windows 98 SE / ME, `forced by storpollmode` on
Windows 2000 to 10, when polling was forced; `INTx would not enable` when the
controller's INTx enable did not read back as set). Open it with Regedit, or export it with
`regedit /e report.reg "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98"`.
On Windows 2000 to 10 it is not rewritten when the system resumes from hibernation.

## Why the name has no "NVMe"

NVM Express, Inc.'s trademark guidelines do not allow "NVMe" in a product name, and they
reserve the marks to members. So the driver is named Stor98 (files and service `stor98`,
after its companion xhci98), and "NVMe" appears only where it says which devices the
driver is for, as in "Stor98 Storage Controller for NVMe SSDs". See also
[Trademarks](#trademarks).

## Building

`scripts\build.cmd` builds all three targets (`stor98.mpd` for Windows 98 SE/ME,
`stor98.sys` for x86 and amd64) from `src/` with the kits in the git-ignored `tools/` folder.
[docs/BUILD.md](docs/BUILD.md) lists what goes there and where each kit comes from.

Since xhci98 3.1.0.0 Stor98 lives in the xhci98 repository as `src/stor98/` and is built,
versioned and released with it: xhci98's gated build builds and checks these images, they
carry the xhci98 package's version and date (`src\stor98ver.h` takes them from xhci98's
`src\xhci_version.h`; `0.4.0.0` was the last release made on its own, so the next version
is `3.1.0.0`), and the 64-bit `stor98.sys` is test-signed with the same certificate as
xhci98's own 64-bit drivers, in the same step. Stor98's interface versions in
[`include/stor98cm.h`](include/stor98cm.h) are protocol versions and stay its own. See
[docs/BUILD.md](docs/BUILD.md), "Releases" and "Test signing", and
[releases/README.md](releases/README.md) for the releases up to `0.4.0.0`.

## Lineage and licensing

Stor98 is a fork of **nvme9x / nvment** by SweetLow
([LordOfMice/Tools](https://github.com/LordOfMice/Tools), `nvme2k-src.zip`), which ports
**nvme2k** 1.0.0.2 by Dominik Behr ([techomancer/nvme2k](https://github.com/techomancer/nvme2k),
BSD-3-Clause) to Windows 9x and NT4, with 64-bit LBA support and fixes.

- The whole project is BSD-3-Clause (see [LICENSE](LICENSE)), new files included.
- Code inherited from nvme2k keeps its BSD-3-Clause copyright notice, conditions and
  disclaimer in each file.
- Code inherited from nvme9x/nvment (SweetLow's `nvme2k.mpd` port) is BSD-3-Clause, the
  licence of the original nvme2k, as SweetLow confirmed on 2026-10-14. Each file in `src/`
  carries both copyright lines.
- The two NT INFs derive from nvme2k's `NVMe2K.inf` (copyright Dominik Behr and Ray
  Hinchliffe), and the Windows 9x INF (`inf/stor98-9x.inf`) from SweetLow's `nvme9x.inf`;
  `LICENSE` names these holders too.
- Microsoft DDK headers (for example `SCSI.H`, `SRB.H`) are never committed; builds take
  them from the git-ignored `tools/` folder.
- The binaries ship as `stor98.mpd` / `stor98.sys`, not under the upstream names: the
  licence's third clause bars using the upstream authors' names to promote this fork.

[NOTICE](NOTICE) lists every incorporated work and its licence status.

## For redistributors

Stor98 ships inside xhci98's release download, built from this source with xhci98's drivers
and at their version. Since xhci98 3.4.0.0 the release is laid out by Windows family, and
each family folder has a `stor98\` folder of its own. xhci98 is GPL-2.0-or-later; Stor98
stays BSD-3-Clause, and its files keep their own names and licence there:

| Folder in the xhci98 release | Stor98 files | Systems |
|---|---|---|
| `98-me\stor98\` | `stor98.inf` (`$CHICAGO$`, from `inf/stor98-9x.inf`) and `stor98.mpd` | Windows 98 SE and ME |
| `nt-x86\stor98\` | `stor98.inf` (NT only, from `inf/stor98-x86.inf`) and the 32-bit `stor98.sys` | Windows 2000 SP4, XP, Vista, 7, 8, 8.1 and 10 (32-bit, 10 below build 22000) |
| `nt-x64\stor98\` | `stor98.inf` (from `inf/stor98-x64.inf`), `stor98.cat` and the 64-bit `stor98.sys` | XP x64 / Server 2003 x64, Vista, 7, 8, 8.1 and 10 (64-bit, 10 below build 22000) |
| each of the three | `stor98-LICENSE.txt`, `stor98-NOTICE.txt` | **Required.** BSD-3-Clause clause 2 says the copyright notices, conditions and disclaimer must go with the binaries. |
| each of the three | `stor98-readme.txt` | Recommended: install notes and settings. |

- Each folder's `stor98.inf` names only the image beside it, so the INF and its image go
  together; take a whole folder, not single files.
- In `nt-x64\stor98\`, `stor98.sys` and `stor98.cat` are test-signed with xhci98's test
  certificate, like xhci98's own drivers; the certificate's public half is
  `nt-x64\xhci98-test.cer`. Windows 8 to 10 x64 refuse the INF without its catalog, so the
  three files go together. The 32-bit images carry no signature and no catalog.
- `98-me\stor98\S98VSD.VXD` beside `stor98.mpd` is xhci98's own file (GPL-2.0-or-later),
  not part of Stor98: it lets Windows 98 SE and ME pass requests above 512 KiB to Stor98
  whole. Stor98 works without it.
- xhci98's flat staging packages (`make-package.ps1`), which its own tools and tests read,
  are not the release layout: there the 9x INF is `stor98w9.inf` beside `stor98.inf`
  ([docs/BUILD.md](docs/BUILD.md)). Take Stor98 from the published family folders.
- Another project that bundles Stor98 takes these files from an xhci98 release, unchanged
  and complete. The releases made on their own up to `0.4.0.0` are in `releases/`.
- Keep the Stor98 file names. Clause 3 also applies: do not use the names of the Stor98,
  nvme2k or nvme9x authors to promote the bundling product without permission.
- The connection-manager link-state interface is the header
  [`include/stor98cm.h`](include/stor98cm.h), which is BSD-3-Clause. A CM may copy it
  unchanged, with its notice. Check that the interface version in the header matches the
  bundled build; xhci98's build holds its copy to this file byte for byte.
- Code from GPL projects must not be copied into Stor98, so that Stor98 stays BSD-3-Clause.

## Trademarks

NVM Express and NVMe are trademarks of NVM Express, Inc.; Stor98 is independent and not
endorsed by NVM Express or Microsoft. "NVMe" appears in this project only to say which
devices the driver is for. Windows is a trademark of Microsoft Corporation.

## Testing

How Stor98 is tested, and the record of each run, are in [docs/TESTING.md](docs/TESTING.md).
