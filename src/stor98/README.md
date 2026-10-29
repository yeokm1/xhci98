# Stor98

Stor98 is a storage controller driver for NVMe SSDs on legacy Windows, built to sit behind hot-pluggable USB4 and Thunderbolt PCIe tunnels.

Stor98 is meant to be used with [xhci98](https://github.com/yeokm1/xhci98) only, as the
storage driver for NVMe SSDs that xhci98's connection manager brings up behind a USB4 or Thunderbolt tunnel. It is not meant to be used standalone. It targets the same systems as xhci98:

| Family | Targets | Binary |
|---|---|---|
| Windows 9x | Windows 98 SE, ME | SCSI miniport (`.mpd`); no helper VxD ([PLAN.md](docs/PLAN.md) 0.12, MD-5) |
| NT, 32-bit | Windows 2000 SP4, XP, Vista, 7, 8, 8.1, 10 (below build 22000) | SCSIport miniport (`.sys`) |
| NT, 64-bit | XP x64 / Server 2003 x64, Vista x64, 7 x64; 8.1 x64 and 10 x64 read in virtual machines only (xhci98's roadmap 43.59: tunnelled drives run, a surprise unplug fails at once; on 10 x64 without `DmaRemappingCompatible`, 43.59s); 8 x64 untested (from xhci98 3.2.0.0 the package installs there with its catalog) | SCSIport miniport (`.sys`, amd64) |

Windows 8, 8.1 and 10 since xhci98 3.2.0.0 (xhci98's roadmap 43.23; 10 below build 22000,
which xhci98's installer admits beside 8 and 8.1 from its roadmap 43.34): see
[Windows 8, 8.1 and 10](#windows-8-81-and-10) for what was read there and what was not, the
64-bit package's install among them.

Windows NT4 is outside the product scope. The NT4 build path inherited from nvme9x/nvment
is kept only while it costs nothing to maintain; it is not tested or shipped.

## How Stor98 differs from nvme2k and nvme9x

Stor98 is a fork of **nvme9x / nvment** by SweetLow, which ports **nvme2k** 1.0.0.2 by
Dominik Behr to Windows 9x and NT4 (see [Lineage and licensing](#lineage-and-licensing)).
Both work well on a drive that is always present. Stor98 exists for the case they were not
built for: a drive in a USB4 or Thunderbolt enclosure that can be unplugged at any time.
Tested in the emulator before the fork, surprise removal during I/O hung Windows 2000 with
nvme2k and wedged the volume on XP x64 with nvme2k 1.2.0.0.

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
    drive was reset (today in the emulator, on Windows 2000 and XP x64); elsewhere it
    says nothing and the driver's 5-second deadline applies. There is
    no clean-eject handshake on these systems: an eject is Windows' own (flush and stop).
  - Windows 98 SE / ME: Stor98 reads the channel (and offers the clean-eject handshake
    that flushes and shuts the drive down before the connection manager removes it).
    xhci98 3.0.0.0 publishes the channel there. A drive plugged in after Windows started
    is announced by the connection manager only after Windows has already started the
    driver for it, so Stor98 looks for it again for up to 10 seconds and binds it then
    (about 0.2 s later in the emulator). A drive present at startup is started before the
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
  larger than 256 KiB gain from the higher limit: in the emulator, aligned 512 KiB reads
  ran about three times as fast as with the earlier 256 KiB limit, and requests of
  256 KiB or less were unchanged. Larger requests, misaligned buffers and fragmented
  buffers are still cut by Windows into 63 KiB pieces, one at a time, so most other I/O
  is not faster.
- **Every target from one source tree:** Windows 98 SE / ME, Windows 2000 to 7 32-bit, and
  XP x64 to 7 x64 (the 64-bit build follows upstream nvme2k 1.2.0.0, which nvme9x
  predates).
- **Removed:** the magic-pattern TRIM of the base, which could overwrite the caller's
  buffer.

The work and the evidence behind each change are in [docs/PLAN.md](docs/PLAN.md).

## Windows 8, 8.1 and 10

Since xhci98 3.2.0.0 (its roadmap 43.23, owner 2026-10-20) xhci98's installer offers
Stor98 on Windows 8 and 8.1 as on 2000 to 7: on NVMe drives behind a Thunderbolt or USB4
tunnel only, its box ticked with the Thunderbolt box there, and on 10 unticked (xhci98's roadmap 43.59; xhci98's
design record 26 section 5.0; `/nostor98` leaves it out), untested until xhci98's
Windows 8 legs (its roadmap 43.24) pass. Windows 8 has no NVMe driver of its own;
Windows 8.1 binds its own `stornvme` to every NVMe controller, and the installer installs
Stor98 over it, forced, on a tunnel drive it finds at install time. Read in the emulator
on 2026-10-22 ([docs/TESTING.md](docs/TESTING.md), "Windows 8 and 8.1" and "Windows 10"):

- **Windows 8, 32-bit:** the x86 package installs through the `.NTx86` sections, SCSIport
  loads and starts Stor98 as on 2000 to 7, and a disk behind the tunnel reads and writes.
  Unplugged during writes with no notice from Windows, the requests failed at once, the
  disk was removed at the 5-second deadline and the copy failed at 5.4 s. Windows 8's disk
  class driver retries a failed write whichever terminal status Stor98 uses (PLAN 2.2).
- **Windows 8 x64 and 8.1 x64: the 64-bit package installs; Stor98 x64 runs on 8.1 x64.**
  Without a catalog file (xhci98 3.1.0.0's package) these systems' driver store refused
  it in test-signing mode ("The third-party INF does not contain digital signature
  information", `0xE000022F`) before the driver was ever loaded. From xhci98 3.2.0.0
  `stor98.inf` names `stor98.cat`, and on 8.1 x64 in test-signing mode the package with
  it was taken into the driver store after the publisher prompt (xhci98's roadmap
  43.36). Signed and trusted, it ran tunnelled drives on 8.1 x64 in xhci98's roadmap
  43.59, and a surprise unplug during a copy failed at once with no hang; 8 x64 with the
  catalog was not run.
- **Windows 8.1, 32-bit:** not run (no install medium); its SCSIport and `stornvme` are
  taken to be 8.1 x64's and 8 x86's, untested.
- **Windows 10, 32-bit** (22H2, build 19045; xhci98's roadmap 43.34 admits 10 below build
  22000 as it admits 8 and 8.1): Windows 10 binds its own `stornvme` to every NVMe
  controller, as 8.1 does. The x86 package installed over it, forced, with the unsigned-driver
  prompt, and SCSIport started Stor98 on both controllers as on 8. Unplugged during writes
  with no notice from Windows, the copy failed at about 6 s, the disk was removed ("Disk 2
  has been surprise removed") at the deadline, and Windows showed "Delayed Write Failed"
  for the lazy writes in flight. Earlier and later 10 builds were not run.
- **Windows 10 x64:** without a catalog the 64-bit package was refused at the driver
  store as on 8 x64 and 8.1 x64 (`0xE000022F`, the same `setupapi` lines), the drive
  staying with `stornvme`; with `stor98.cat` (xhci98 3.2.0.0) it was taken into the
  driver store as on 8.1 x64 (xhci98's roadmap 43.36). Signed and trusted, it did not
  start there while its INF wrote `DmaRemappingCompatible`; without it (xhci98's roadmap
  43.59s, the INF as it ships) it ran tunnelled drives, and a surprise unplug during a
  read failed at once (xhci98's roadmap 43.59).
- **Windows 11** is not a target (xhci98's installer refuses it; it has its own USB4
  support).
- **Under 8.1's own `stornvme`** (no Stor98), a drive unplugged during a write with no
  notice from Windows held the write for minutes, `stornvme` resetting the controller every
  35 s, and finished it when the drive came back.

The settings below apply on 8, 8.1 and 10 as on 2000 to 7: the same key, the same defaults.

## Updating the driver

- **Windows 98 SE / ME (known issue).** Update Stor98 (or switch to it from another NVMe
  driver) with the drive unplugged, or with nothing using its volume, then shut down and
  start the computer again rather than restart it with the drive attached. If the drive
  then reads "not ready", unplug it, restart, and plug it in again. Observed in the
  emulator after an in-place update with the volume in use followed by a warm restart: ME
  stopped at restart with a VCACHE error and then hung in ScanDisk (xhci98's tests, 0.3.0.0
  to 0.3.0.1), or the volume came back "not ready" (Stor98's test). Whether the update
  itself or the warm restart of Windows 98 SE / ME causes it was not separated.
- **With xhci98's own USB4 connection manager (Windows 2000 to 7; on 8, 8.1 and 10 not yet read).** On a USB4 host run
  by xhci98's connection manager, changing the driver of a tunnelled NVMe drive stops the
  device, which the connection manager takes as an eject: it tears the tunnel down.
  Unplug the drive and plug it in again after the update. Under a Thunderbolt 3 host's
  firmware connection manager the tunnel stays up and the drive comes back with the new
  driver.

## Settings

Stor98 needs no settings: every value below is optional. The INFs write none of them,
so an absent value means the built-in default on every system, and a later Stor98 with a
better default applies it without the user doing anything. Updating the driver keeps any
value the user set: the INFs neither write nor delete these values.

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
  `stormaxtransferkb=128;storpollmode=1;storpollbusyus=20000`. It is read when the controller starts:
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
| Gone deadline (advanced: leave at the default) | `StorGoneMs` (DWORD or string) | `storgonems=` | milliseconds | 5000 (5 s) | 1-600000 | 0 or above the range: the default |
| Write-idle time before a flush | `StorWriteIdleMs` (DWORD or string) | `storwriteidlems=` | milliseconds | 1000 (1 s) | 10 and up | below 10: 10 |
| Hold before busy, 98 SE / ME (advanced: leave at the default) | `StorHold9xMs` (DWORD or string) | none | milliseconds | 4000 (4 s) | 1000-10000 | refused: the default stands |

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

### Polling mode

Stor98 normally completes requests from the controller's interrupt. When Windows gives
the controller no usable interrupt, Stor98 runs without one: it polls the controller for
completed requests, often while requests are outstanding and seldom when idle. This
happens when the controller has no interrupt pin (an MSI-only controller: a SCSI miniport
gets no MSI on these systems), when Windows assigned it no interrupt, or when its
interrupt line is not set. Polling can also be forced, for testing. Four values control
it (table above; [docs/PLAN.md](docs/PLAN.md), "Polling mode"):

- **StorPollMode** (`storpollmode=`): 0 polls only when there is no usable interrupt; 1
  always polls.
- **StorPollBusyUs** (`storpollbusyus=`) is the polling period while requests are
  outstanding. It only takes full effect on Windows 98 SE and ME. On Windows 2000 to 8.1
  the port rounds every timer period up to the OS clock tick (10-15.6 ms), so Stor98
  uses at least 10 ms there and a value below that changes nothing; Windows 10's
  rounding is not read yet (Stor98 asks for the same 10 ms minimum there). A shorter period
  completes requests sooner, at a higher CPU cost while busy.
- **StorPollIdleMs** (`storpollidlems=`) is the period with nothing outstanding and nothing
  left to flush. 0 stops polling when idle, as with an interrupt; the timer still runs
  for unflushed writes and for the connection manager's link check.
- **StorPollSpinUs** (`storpollspinus=`) lets the driver keep checking for a completion for
  that long right after sending a request, which helps short requests at a CPU cost (a
  busy-wait of at most that long per request while one is outstanding, only in the
  polling mode). The defaults differ by family: on Windows 2000 to 8.1 the timer cannot run
  faster than the OS clock tick (10's is not read yet, and it takes 2000's defaults), so the spin is what keeps polling close to interrupt
  throughput; on 98 SE and ME the 1 ms timer works and the CPU is scarcer.

Measured on Windows 98 SE in the emulator (QEMU TCG, polling forced, before the 512 KiB
transfer of 0.4.0.0, 8 MiB written and read
back with a data compare at queue depth 1). The figures show the settings relative to each
other, not what real hardware does:

| Mode | MB/s, 64 KiB requests | MB/s, 128 KiB requests | Time per 64 KiB request |
|---|---|---|---|
| Interrupt | 13-16 | 12-24 | - |
| Polling, StorPollBusyUs 1000 (default) | 12-13 | 16-21 | about 5 ms |
| Polling, StorPollBusyUs 10000 | 8 | 5.4-5.5 | about 8 ms |
| Polling, StorPollBusyUs 10000, StorPollSpinUs 500 | 15-16.6 | 14.5-16.3 | - |
| Polling, StorPollBusyUs 50000 | 1.3-1.4 | 1.8-2.7 | about 47 ms |

The same on Windows 2000 (emulator, polling forced, raw disk, 16 MiB written and read back
with a compare, 64 and 256 KiB requests):

| Mode | MB/s, queue depth 1 | MB/s, queue depth 8 |
|---|---|---|
| Interrupt | 69-229 | 73-267 |
| Polling, no spin (each request waits for a 10 ms tick) | 5.9-21 | 39-132 |
| Polling, pollspinus=500 (default) | 76-178 | 84-229 |

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
reason (`forced by StorPollMode` on Windows 98 SE / ME, `forced by pollmode` on Windows
2000 to 10, when polling was forced). Open it with Regedit, or export it with
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
- The INFs derive from nvme2k's `NVMe2K.inf` (copyright Dominik Behr and Ray Hinchliffe)
  and, for their Windows 9x sections, SweetLow's `nvme9x.inf`; `LICENSE` names these
  holders too.
- Microsoft DDK headers (for example `SCSI.H`, `SRB.H`) are never committed; builds take
  them from the git-ignored `tools/` folder.
- The binaries ship as `stor98.mpd` / `stor98.sys`, not under the upstream names: the
  licence's third clause bars using the upstream authors' names to promote this fork.

[NOTICE](NOTICE) lists every incorporated work and its licence status.

## For redistributors

Stor98 ships inside xhci98's release download (its `release-x86` and `release-x64`
directories), built from this source with xhci98's drivers and at their version. xhci98 is
GPL-2.0-or-later; Stor98 stays BSD-3-Clause, and its files keep their own names and licence
there:

| File in the xhci98 package | Needed for |
|---|---|
| `stor98.inf`, `stor98.mpd`, `stor98.sys` (32-bit package) | Windows 98 SE and ME (`stor98.mpd`) and Windows 2000, XP, Vista, 7 32-bit (`stor98.sys`). One `$CHICAGO$` INF installs the right image on each system, so the three files go together. |
| `stor98.inf`, `stor98.sys` (64-bit package) | XP x64, Vista and 7 (64-bit). The `.sys` is test-signed with xhci98's test certificate (`xhci98-test.cer`), like xhci98's own drivers. |
| `stor98-LICENSE.txt`, `stor98-NOTICE.txt` | **Required.** BSD-3-Clause clause 2 says the copyright notices, conditions and disclaimer must go with the binaries. |
| `stor98-readme.txt` | Recommended: install notes and settings. |

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

## Testing without hardware

The driver is developed against an emulated USB4 / Thunderbolt host for QEMU,
[usb4-emu](https://github.com/yeokm1/usb4-emu), which puts QEMU's `nvme` device behind a
PCIe tunnel that can be brought up, torn down and surprise-removed. See
[docs/TESTING.md](docs/TESTING.md).
