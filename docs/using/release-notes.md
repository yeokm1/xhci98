# xhci98 - Release Notes

This file describes package version `2.0.0.0`, the eighth release and the
first of the host controller driver (`DriverVer` `10/04/2026,2.0.0.0`).
Where this file and
`docs/contributing/roadmap-hcd.md`, `docs/contributing/build-and-test.md` or
`xhciqual/README.md` disagree, the other document wins and this one is the
copy to fix.

The notes for `1.2.0.0`, the last release of the USB 2.0-only miniport, stay
as they were published: on the `1.2.0.0` branch,
[docs/using/release-notes.md](https://github.com/yeokm1/xhci98/blob/1.2.0.0/docs/using/release-notes.md),
and in plain text in that release's own
[readme.txt](../../releases/1.2.0.0/readme.txt). Read them if you run
`1.2.0.0`; nothing below describes it except where it says so.

---

## What this is

`xhci98.sys` is a USB host controller driver for xHCI (USB 3.x) controllers,
for Windows 98 SE, Windows ME, Windows 2000 SP4, 32-bit Windows XP, Windows XP
x64, and Windows Vista and Windows 7 in both architectures, on machines whose
only USB controller is xHCI.

Since `2.0.0.0` it is a whole USB host stack below the class drivers rather
than a miniport. It owns the controller, creates the root hub, drives every
hub behind it and splits composite devices into their functions itself, with
no `usbport.sys`, `usbhub.sys`, `usbhub20.sys` or `usbccgp.sys` involved.
Windows' own class drivers sit on top of it unchanged: HID, `usbstor.sys`,
USB audio, and vendor drivers such as ASIX's Ethernet one. Because nothing of
the old USB 2.0-era stack is left underneath it, it is no longer limited to
USB 2.0:

- SuperSpeed (USB 3.x, 5 Gbit/s) devices on the root ports;
- SuperSpeed hubs, whose USB 2.0 half is driven as a USB 2.0 hub;
- High-, Full- and Low-Speed devices and USB 2.0 and 1.1 hubs, every device
  reported to Windows at its true speed;
- UAS (USB Attached SCSI) storage, through a second driver in the same
  package, `xhciuas.sys`, since none of these systems has a UAS driver of its
  own.

A link that trains above 5 Gbit/s (SuperSpeedPlus: USB 3.1 Gen 2, USB 3.2
Gen 1x2 and Gen 2x2) is accepted at its trained rate. That ground is
untested; see "Untested ground".

In Device Manager the controller is "xHCI98 USB 3.x eXtensible Host
Controller", with "xHCI98 USB 3.x Root Hub" beneath it and every USB device
beneath that, hubs and devices behind hubs included: an external hub is
driven inside the driver and gets no entry of its own. A UAS device appears
as "xHCI98 USB Attached SCSI Storage".

The package carries two architectures. The 32-bit drivers serve Windows 98
SE, ME, 2000, 32-bit XP and 32-bit Vista and 7; the 64-bit ones are a
separate build from a separate toolchain, in their own directories with their
own INFs, for Windows XP x64, Vista x64 and Windows 7 x64. Picking the wrong
directory is harmless: Windows finds no driver in it and says so.

**On Windows Vista x64 and Windows 7 x64 driver signature enforcement must be
disabled for the drivers to load, at every start.** Those systems refuse an
unsigned kernel-mode driver, and these are not signed. On a start where
enforcement is in force the controller shows Code 39 and nothing plugged into
it works inside Windows, a USB keyboard or mouse included. The install itself
can be done on an ordinary start. 32-bit Vista and Windows 7, and Windows XP
x64, do not refuse unsigned drivers.

## What this is not

- It is not tested evenly. Every Windows 2000 result comes from virtual
  machines: Windows 2000 Setup bugchecked on both physical machines this
  project tried, so the driver has never run on Windows 2000 on real silicon.
  Windows ME, both Windows XP builds, Windows Vista and Windows 7 are
  supported in virtual machines only for `2.0.0.0`. Windows 98 SE is the one
  target read on real hardware, at a bench session before the release, on a
  ThinkPad E460 and a ThinkPad P14s Gen 1. "Targets and their standing" says
  what each one rests on.
- It is not a mass-storage driver for Windows 98 SE. Storage on Windows 98
  SE, Bulk-Only and UAS alike, needs NUSB's mass-storage component; see
  "Windows 98 SE and ME: the stock install, and storage".
- It is not signed. Windows 98 SE and ME do not check; Windows 2000 and XP
  warn and install; 32-bit Vista and 7 warn and install; Vista x64 and
  Windows 7 x64 load it only while driver signature enforcement is disabled
  (above). No 64-bit Windows after Windows 7 is supported.
- It does not put idle devices to sleep. The driver never initiates
  selective suspend, of a device or of a hub port; see "Known limitations".
- It does not write to your disk. The driver creates no file. Its log is read
  out of the running driver by `XHCISNAP.EXE` when you ask for a report; see
  "The log, and how to send one".
- It is not the miniport. `1.2.0.0` is frozen and stays available; nothing
  of it is in this package. A `2.0.0.0` install replaces it, because both are
  called `xhci98.sys` (see "Upgrading from 1.2.0.0").

## Targets and their standing

Each row says what the `2.0.0.0` drivers were observed doing on that system,
and where. "The integration build" is the merged build of the release's code
before the cut, in the never-published `qemu` flavour. The ten install legs
were also read on the `release` flavour of that build, and are read again
from the release asset itself after the cut.

| Target | Standing | What it rests on |
|---|---|---|
| Windows 98 SE | Primary | Virtual machines, under NUSB 3.3 and under SweetLow's stack: the install, mouse, keyboard, storage with a verified file compare, a composite audio device split into its functions and played to its end (unheard: the guest has no audio out), the ASIX Ethernet adapter passed through, the controller's Advanced tab and the root hub's Power tab, and disable, enable, remove and rescan. Hubs behind QEMU's Full-Speed hub to five tiers, 25 plug cycles per device class, and a 120-hub churn with the guest responsive. A stock install with no USB 2.0 stack: the controller, hubs, a HID mouse and a composite audio device work (next section). SuperSpeed storage and UAS at SuperSpeed (16 streams) and at High Speed, each with a verified round trip, on the integration build. UAS and forced Bulk-Only on a passed-through dual-transport bridge, each with a verified round trip. Real hardware, on a ThinkPad E460 and a ThinkPad P14s Gen 1: the install, HID, storage, the ASIX Ethernet adapter, and a Full-Speed audio device played and heard at a root port and behind a hub; High-Speed hubs, single- and multi-TT, with Low- and Full-Speed devices behind them, and a Full-Speed hub behind a High-Speed hub (a USB 2.0 hub held at Full Speed by a full/low-speed isolator); a Low-Speed mouse polled every 8 ms at a root port and behind a hub; two SuperSpeed drives on a root port, the link read as SuperSpeed in `XHCISNAP`'s slot table, with round trips and throughput against the same drive behind a USB 2.0 hub; a SuperSpeed hub with a SuperSpeed drive behind its SuperSpeed half and a High-Speed device behind its USB 2.0 half, plugged, unplugged and plugged in again; and UAS on a dual-transport bridge at SuperSpeed and, behind a USB 2.0 hub, at High Speed, UAS on a UAS flash drive at SuperSpeed with Bulk-Only behind a USB 2.0 hub, and the forced-Bulk-Only value on both at SuperSpeed, each with a round trip and throughput against Bulk-Only |
| Windows 2000 SP4 | Primary, virtual machines only | The same device, hub and Device Manager rows as Windows 98 SE, under Driver Verifier, plus a multiprocessor guest. SuperSpeed storage, and UAS at SuperSpeed and at High Speed, on the integration build. UAS, forced Bulk-Only and the switch between them on a passed-through dual-transport bridge, each with a verified round trip. SuperSpeed hubs: untested, as no virtual machine models one; built from the specification. Never run on real hardware |
| Windows ME | Virtual machines only | Under SweetLow's stack, on the integration build: the install, HID, storage with a verified file compare, unplug and replug, a hub with a mouse and a stick behind it, a composite audio device bound at a root port and behind a hub, the root hub's disable and enable, a 10-cycle soak per device class, SuperSpeed storage, and UAS at SuperSpeed (streams) and at High Speed. Bulk-Only storage works on Windows ME's own mass-storage files, and UAS once those are present (next section). Re-enabling the controller with a USB mouse attached hung Windows ME under `2.0.0.0` (a keyboard was never tried); `2.1.0.0` fixes it: the devices are kept across the controller's stop and revived on the re-enable, as Microsoft's hub driver does (`TODO(33.1 legs)`: read again on the `2.1.0.0` package) |
| 32-bit Windows XP SP3 | Virtual machines only | On the integration build: install, HID, storage with a verified file compare, unplug and replug, a hub with devices behind it, composite audio bound, the root hub's and the controller's disable and enable, shutdown; SuperSpeed storage, and UAS at SuperSpeed and at High Speed. A 10-cycle soak per device class (HID, a hub with a mouse, storage) |
| Windows XP x64 SP2 | Virtual machines only; the 64-bit drivers | The same clauses as 32-bit XP, with the 64-bit `xhciuas.sys` at SuperSpeed and at High Speed, and the same soak |
| Windows Vista SP2, 32-bit and x64 | Virtual machines only | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled. The same soak |
| Windows 7 SP1, 32-bit and x64 | Virtual machines only | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled; five controller disable and enable cycles on each. The same soak. Windows 7 is not read on real hardware for `2.0.0.0` |
| Every target, from the release package | | The `release` flavour of the integration build, on the ten install legs (Windows 98 SE under NUSB and under SweetLow's stack, ME, 2000, XP, XP x64, and Vista and 7 in both architectures): installed, controller and root hub started, HID, storage with a verified file compare, composite audio bound, the controller's disable and enable, and shutdown, with SuperSpeed storage and UAS at SuperSpeed and at High Speed on most legs. The one defect was Windows ME's controller re-enable, which `2.1.0.0` fixes (`TODO(33.1 legs)`). The legs are read again from the release asset after the cut |

The virtual machines are QEMU's `qemu-xhci`, which models no SuperSpeed hub,
no link faster than 5 Gbit/s, no Low-Speed device and no High-Speed hub; what
those need is the bench's, or is untested ground.

## Requirements

| | |
|---|---|
| Operating system | Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows ME (4.90.3000), 32-bit Windows XP (SP3), Windows XP x64 (SP2), and Windows Vista (SP2) and Windows 7 (SP1) in both architectures, in virtual machines only. Vista x64 and Windows 7 x64 load the drivers only while driver signature enforcement is disabled. Nothing after Windows 7 |
| USB stack | None. The driver replaces the port and hub drivers on every target. Windows 98 SE needs no USB 2.0 stack for the controller, hubs, HID or audio; **storage needs NUSB's mass-storage component** (next section). Windows ME: every Windows ME reading was taken under SweetLow's USB 2.0 stack; Windows ME without it has not been tested under `2.0.0.0`. Do not install NUSB on Windows ME, 2000, XP, Vista or 7 |
| Controller | An xHCI controller presenting PCI class code `0C0330`, with a BAR0 mapped below 4 GB and a legacy interrupt pin: the driver has no MSI path, so a controller reporting `Interrupt Pin = 0` cannot be driven at all. A controller with no USB 2.0 protocol port at all is accepted, built from the specification and untested: no such controller has been held |
| Install media | Windows 98 SE on an xHCI-only machine: the Windows 98 SE installation CD at hand, or the Windows CABs on the hard disk. Windows ME: the same, from the Windows ME CD or the CABs its Setup leaves on the disk. The NT targets take what they need from their own driver cache and ask for nothing |

Run the qualifier before installing anything; it answers the controller
conditions in a single read-only pass.

## Windows 98 SE and ME: the stock install, and storage

**Windows 98 SE needs no USB 2.0 stack.** On a stock Windows 98 SE install,
with neither NUSB nor SweetLow's stack, the driver installs, starts the
controller and the root hub, and drives hubs, HID devices and composite audio
devices through Windows 98 SE's own class drivers. The install asks for the
Windows 98 SE CD for Windows' own `usbd.sys`, and the first HID device asks
for it again for `hidclass.sys` (see "Installing"). If NUSB or SweetLow's
stack is installed, its port and hub drivers are simply not used.

**Storage on Windows 98 SE needs NUSB's mass-storage component**, for
Bulk-Only and UAS devices alike. Windows 98 SE has no USB mass-storage
driver, and a drive letter there comes from a mapping layer that this
package does not replace. The component is five files, which NUSB 3.3 and
3.6 install:

- `USBSTOR.INF` and `USBSTOR.SYS`, the Bulk-Only driver;
- `USBNTMAP.INF` and `USBNTMAP.SYS`, which map a USB disk to a drive letter;
- `USBMPHLP.PDR`, the mapping port driver.

They are Microsoft's, and this package does not carry them. SweetLow's stack
alone has no storage part.

- **Without them**, a Bulk-Only stick has no driver at all: "Unknown Device",
  Code 28. A UAS device installs "xHCI98 USB Attached SCSI Storage" and then
  stays at Code 2 ("The NTKERN.VXD device loader(s) for this device could not
  load the device driver"), because `xhciuas.inf` names `USBNTMAP.SYS` as a
  filter on the device; a restart does not change that.
- **With NUSB 3.3 or 3.6 installed**, storage works.
- **The five files can also be installed on their own**, without NUSB's USB
  2.0 stack: point the Add New Hardware Wizard, or Update Driver on the
  device, at a folder holding them. **On the first storage device, one step
  is not obvious**: Windows binds the new disk to its own generic "Disk
  drive" before it has seen `USBNTMAP.INF`, and no drive letter appears. In
  Device Manager open Disk drives, "Disk drive", Driver, Update Driver, and
  point it at the same folder, which installs "USB Disk"; then unplug the
  device and plug it back in. After that every storage device, Bulk-Only or
  UAS, works with no extra step, and a UAS device that sat at Code 2 starts
  at the next boot.

**Windows ME ships its own mass-storage files.** Windows ME carries its own
`USBSTOR` and `USBNTMAP` INFs, and the files are copied from Windows ME's
CABs when the first stick installs. Bulk-Only storage works on them, and so
does UAS once they are there. A UAS drive plugged in before any ordinary
stick shows Code 2 until they are: plug in any ordinary USB stick once, then
unplug the UAS drive and plug it back in ("Known limitations").

**The UAS driver on Windows 98 SE installs with no restart.** Its INF
copies no file in use, so the first UAS device installs and works at once.

## Before you install: check the machine

`XHCIQUAL.EXE` is a DOS tool that reads the machine's xHCI controller and says
whether the driver can work on it. Run it with no arguments for a read-only
quick scan. It writes no PCI configuration register and prints one of three
verdicts:

| Verdict | Means |
|---|---|
| `LOOKS QUALIFIED` | nothing a read-only pass can see disqualifies this machine; the active tests still decide |
| `DISQUALIFIED` | something a read-only pass genuinely sees: no `CC_0C0330` function, `Interrupt Pin = 0`, BAR0 unusable or above 4 GB, or no USB 2.0 protocol ports |
| `CANNOT SAY` | a state this pass may not change is in the way: the controller is not in D0, or Memory Space Enable is clear |

The qualifier is the same program the `1.x` releases carried, and its last
criterion, no USB 2.0 protocol ports, is the miniport's; see the Controller
row of "Requirements".

It is in the download's `xhciqual\` directory. **It must be run from real
DOS**, not a DOS box inside Windows, booted without EMM386 or any other V86 or
paging memory manager. `HIMEM.SYS` is allowed, and on some machines it is
needed: if the tool will not run at all on a boot that loads nothing, add
`DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` to `CONFIG.SYS` (adjusting the path)
and try again. `xhciqual/hardware-testing.md` has the staged active tests,
the safety notes, and how to read each result.

## Installing

Each flavour directory of the package holds four files: `xhci98.inf` and
`xhci98.sys`, the host controller driver, and `xhciuas.inf` and
`xhciuas.sys`, the UAS driver. `release-x86\` is the one to install on 32-bit
Windows and `release-x64\` on 64-bit Windows; the `debug-` directories are the
same drivers built for troubleshooting. No Microsoft file is in the package.

1. Install the prerequisite, where there is one: on Windows 98 SE, NUSB 3.3
   or 3.6 if you want USB storage (above). On a fresh Windows 98 SE install
   the order is NUSB, then this driver, then the first storage device.
2. In Device Manager, find the unrecognised xHCI controller, usually under
   "Other devices", and update its driver from the `release-` directory:
   - Windows 98 SE and ME: Properties, Driver, Update Driver, Specify a
     location. If Windows finds the controller first, the Add New Hardware
     Wizard asks the same question; give it the same directory.
   - Windows 2000 and XP: Properties, Driver, Update Driver, Have Disk. XP
     shows its unsigned-driver warning; choose Continue Anyway.
   - Windows Vista and 7: Update Driver Software, Browse my computer for
     driver software. If Windows warns that it cannot verify the publisher,
     install the driver anyway. Use Device Manager rather than right-clicking
     the INF.
3. It installs as "xHCI98 USB 3.x eXtensible Host Controller" with "xHCI98
   USB 3.x Root Hub" beneath it, and neither should carry a warning mark.
   Restart if asked: Windows 98 SE asks once, after the controller's install.
4. The first UAS device brings the Found New Hardware wizard for "xHCI98 USB
   Attached SCSI Storage"; point it at the same directory.

**The files Windows supplies.** Two files the install needs are Windows' own
and are not in the package: `usbd.sys`, whose helper routines the class
drivers above this driver call, and `usbui.dll`, which draws the USB property
pages. On an xHCI-only machine Windows never placed them, so the INF asks
Windows to copy each from its own installation source, only if it is absent.
On Windows 98 SE that is an "Insert Disk" prompt naming the Windows 98 Second
Edition CD-ROM, unless the Windows CABs are on the hard disk; insert the CD
and click OK, and if it then asks where to copy from, give it the CD's
`WIN98` folder. The first HID device can raise the same prompt for
`hidclass.sys`, Windows' own HID class driver. The NT targets take both files
from their driver cache with no prompt, and every Windows Vista and Windows 7
install already has them. The driver needs no `usbport.sys` and no
`usbhub.sys` on any target, and the install copies neither.

Windows 2000 note: the first Update Driver after a cancelled Found New
Hardware Wizard was once seen to leave the controller with no driver; a
second Update Driver filled it.

### Installing Windows 2000 or XP itself: the F6 floppy

From `2.1.0.0` every flavour directory carries `txtsetup.oem`, for a machine
whose keyboard or install medium is on an xHCI controller. Copy the files of
`release-x86\` (`release-x64\` for Windows XP x64) to the root of a floppy,
press F6 when text-mode Setup offers it, press S, and pick "xHCI98 USB 3.x
Host Controller". Setup then drives a USB keyboard, mouse and USB stick
through its own drivers above this one, and GUI-mode Setup installs the
driver from `xhci98.inf` as usual. Limits:

- Pressing F6 needs the firmware's own USB keyboard support.
- The floppy must be drive A: as the firmware sees it.
- A disk the driver runs as UAS (most USB 3 enclosures and SSDs) is not
  usable until GUI-mode Setup; Bulk-Only flash sticks are.
- Installing Windows onto a USB disk is not supported.
- GUI-mode Setup may ask for the floppy or the Windows CD.

**Untested ground** (`TODO(33.3)`): this path has been checked against
Setup's own files only; no install from it has been run on any target, in
a virtual machine or on real hardware.

### Upgrading from 1.2.0.0

`2.0.0.0` replaces the `1.2.0.0` driver: both are called `xhci98.sys`, and
the install overwrites the file. Going back is a reinstall of the `1.2.0.0`
package from its own download.

Update the "USB 2.0 eXtensible Host Controller (xhci98)" entry in Device
Manager and always pick the driver from a list (Have Disk) rather than let
Windows search: a search can reinstall the old driver from Windows' own copy.
Measured in virtual machines on each system below except Windows ME:

- **Windows 98 SE with NUSB: never update in place.** NUSB's `usbport.sys`
  crashes the machine (fatal exception 0E at `0028:C00312EE`) as it stops
  the running `1.2.0.0`, before `2.0.0.0` runs. Instead open an MS-DOS
  Prompt and type `ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV`,
  shut down and switch on again (the controller shows a yellow mark), then
  Update Driver, "Display a list of all the drivers in a specific location",
  Have Disk, the `release-x86\` directory, "xHCI98 USB 3.x eXtensible Host
  Controller"; give it the Windows 98 SE CD for `usbd.sys` and restart. Each
  USB device is then found once more as new hardware. If you already updated
  in place and got the blue screen, restart: `2.0.0.0` comes up on its own.
- **Windows 98 SE with SweetLow's stack**: the same Update Driver route, in
  place. Windows does not ask for a restart, but shut down and switch on
  again straight away: until then USB devices stop working.
- **Windows ME**: not tested; follow the SweetLow route.
- **Windows 2000**: Update Driver, "Display a list of the known drivers",
  Have Disk; pick "xHCI98 USB 3.x eXtensible Host Controller", the first of
  three models. It starts at once; at the next restart Windows may ask for
  one more.
- **Windows XP**: Update Driver, "Install from a list or specific location",
  "Don't search. I will choose the driver to install", Have Disk, Continue
  Anyway; a second wizard follows for the root hub. No restart.
- **Windows Vista and 7**: Update Driver Software, "Browse my computer",
  "Let me pick from a list of device drivers on my computer", Have Disk.
  Typing the folder into the search box keeps the old driver. No restart.

The steps were tested on Windows 98 SE (NUSB and SweetLow), 2000, XP and
32-bit Windows 7; on XP x64, Vista and 7 x64 the same steps apply, pointed
at `release-x64\` on the 64-bit systems.

The three `1.2.0.0` virtual-hub values have no effect under `2.0.0.0`; see
"Registry settings".

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one
installed, Windows 98 SE's "Search for a better driver" keeps the copy it
already has. Instead, Update Driver on "xHCI98 USB Attached SCSI Storage",
choose "Display a list of all the drivers in a specific location", then Have
Disk, and point it at the package directory.

## SuperSpeed and UAS

- **Which driver a storage device gets.** A UAS-capable device gets UAS
  through `xhciuas.sys`, and anything else gets Windows' own `usbstor.sys`
  (Bulk-Only), at whatever speed it connects. A device that offers both can
  be forced to Bulk-Only (`XhciForceBulkOnly`, below).
- **SuperSpeed UAS needs streams.** A UAS-only device at SuperSpeed on a
  controller that cannot stream is sent back to its USB 2.0 port and runs
  UAS at High Speed, or is refused if it has no USB 2.0 port; behind a
  SuperSpeed hub it is refused in place. That branch is built from the
  specification and has run on no such controller.
- **Device Manager does not show SuperSpeed.** Device Manager and the other
  Windows tools on these systems show a SuperSpeed device as High Speed at
  most: the interface they read predates SuperSpeed. That display says
  nothing about the real link. `XHCISNAP`'s report shows the speed the driver
  decoded from the port and the speed it gave the device's slot: in the
  report's slot table, `PSIV` is the Speed field of the controller's own Slot
  Context for the device and `speed` is that value decoded, for example
  `SuperSpeed, 5 Gbit/s, Gen 1x1`.
- **Throughput, for scale.** On a ThinkPad P14s Gen 1 under Windows 98 SE, an
  MSSU10 flash drive under the UAS driver at SuperSpeed read about 198 to
  224 MB/s and wrote about 205 to 217 MB/s from 512 KB transfers up (ATTO
  Disk Benchmark, Direct I/O, queue depth 1), an informal run. Windows 98
  sends one command at a time, so a drive reads slower there than on a
  modern Windows at higher queue depth.

## Registry settings

Four values, each a `DWORD` in the controller's driver (software) key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host
Controller". The number varies from machine to machine, and a machine whose
controller was ever enumerated at another PCI slot can have more than one
such key: the device's own `Driver` value, under `Enum\PCI`, names the one in
use. `XHCISNAP -verbosity` finds it for you.

### XhciImodInterval250ns: the interrupt moderation interval

In **units of 250 ns**: how long the controller waits after one interrupt
before raising the next. A shorter interval makes USB storage faster at the
cost of more interrupts.

| | Value | Interval | Interrupts per second, at most |
|---|---|---|---|
| Written by the install, every path, both INFs | `160` | 40 us | 25,000 |
| Used when the value is missing, unreadable, or outside `10`-`4000` | `4000` | 1 ms | 1,000 |
| Lowest accepted | `10` | 2.5 us | 400,000 |

`160` is written by the INFs only; the driver's own default stays `4000`, the
controller's power-on value, which is what it runs at if the value is absent.
A value outside `10`-`4000` is replaced by `4000`, not rounded, so a mistyped
`0` cannot turn moderation off. The driver reads the value when the
controller starts, so a change takes effect after a restart, and `XHCISNAP`'s report shows the value read, the
interval in force and what the controller took.

`160` is the value Linux's xHCI driver uses. On a ThinkPad P14s Gen 1 under
Windows 98 SE with NUSB, an MSSU10 drive under the UAS driver at SuperSpeed
(ATTO Disk Benchmark, Direct I/O, queue depth 1) read and wrote 181 MB/s at
8 MB transfers at `500`, the value `1.2.0.0` shipped, against 211 MB/s write
and 221 MB/s read at `160`; `40` added only 1 to 3% more for up to four times
the interrupt rate. A Full-Speed audio stream ran at `160` in a Windows 2000
virtual machine with no missed-service or packet error, which shows the value
reaches the controller and the stream runs, not what it costs audio on real
hardware. On real hardware under Windows 98 SE, Full-Speed audio played
without stutter at `160` while a drive was read at full speed. If audio
stutters under storage load on your machine, raise the value towards `4000`
or delete it.

### XhciForceBulkOnly: Bulk-Only instead of UAS

`1` makes every storage device on that controller that offers both
transports use Bulk-Only (Windows' own `usbstor.sys`) instead of UAS. `0` or
absent, the default, means UAS wherever the device offers it. The install
does not write it.

It is read each time a device enumerates, so unplug and replug the device
after changing it. On Windows 2000 and later, a device already installed
keeps its driver until it is uninstalled in Device Manager and replugged. A
UAS-only device stays on UAS whatever the value says. A device whose vendor
id Windows' own `usbstor.inf` lists by hand can still get `usbstor.sys` on
the NT targets.

### XhciLogVerbosity and XhciLogDebugView: the log

See "The log, and how to send one". Both default to `0`.

### The 1.2.0.0 virtual-hub values have no effect

`XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, which a
`1.2.0.0` install wrote, are not read by `2.0.0.0` and its INFs write none of
them. A value left behind by a `1.2.0.0` install has no effect. The driver
reports every device at its true speed with no virtual hub in the way, so
there is nothing for them to switch. Delete them if you want them gone.

## The controller's Advanced tab and the root hub's Power tab

The controller's properties carry an **Advanced** tab and the root hub's a
**Power** tab, on every target, as Windows' own USB controllers do. The pages
are Windows' own (`sysclass.dll` on Windows 98 SE and ME, `usbui.dll` on the
NT targets, with `usbui.dll` drawing the Windows 98 dialogs), and the driver
answers the requests they send. The Power tab reports the power budget the
driver itself keeps, since external hubs are the driver's own. Read on Windows 98 SE under both stacks and on Windows
2000, in virtual machines.

Up to `2.0.0.0` the Power tab showed every device's power as unknown, on
every target: the driver refused the descriptor request the page sends,
whose request code Windows leaves at zero. `2.1.0.0` answers it
(`TODO(33.5)`: read on the `2.1.0.0` package). Two things the pages show
are Windows' own arithmetic, not the driver's:

- **Bandwidth counts isochronous pipes in use, and nothing else.** On
  Windows 98 SE to XP the Advanced tab adds up only the isochronous pipes
  a device has open, as it does over Microsoft's own stack, so a mouse, a
  keyboard or a drive adds nothing, and an audio device adds its share only
  while it plays or records. On Vista and 7 the figure comes from a WMI
  query the driver does not answer, and stays at zero.
- **A SuperSpeed device's power reads a quarter of its draw.** The page
  doubles the configuration descriptor's `bMaxPower`, which is in 2 mA units
  at USB 2.0 and in 8 mA units at SuperSpeed.

## The log, and how to send one

The driver keeps a small log of what happened on the bus, inside itself, and
`XHCISNAP.EXE` (in the download) reads it off the running machine. The log is
off by default and stays off unless you are diagnosing something.

```
  1.  XHCISNAP -verbosity 2
  2.  restart the machine
  3.  make the problem happen again
  4.  XHCISNAP -o C:\MYDUMP
```

Then send `C:\MYDUMP.TXT`, a plain-text report with no internal addresses in
it. `C:\MYDUMP.BIN` is written beside it, the driver's raw internal state,
which only a maintainer holding the exact build can decode; attach it if you
are asked for it. Step 1 finds the right registry key for you, on every xHCI
controller the machine has. Step 2 is not optional: the driver reads these
settings when it starts. **Use the `XHCISNAP.EXE` from this download**, not
a copy kept from a `1.x` release.

| Value | What it does |
|---|---|
| `XhciLogVerbosity` | `0` off (the driver does not answer `XHCISNAP` at all); `1` counters only; `2` adds the log of what happened (use this one); `3` adds the USB port register table; `4` everything, including internal addresses in the plain-text report. A value outside `0`-`4` is treated as `0` |
| `XhciLogDebugView` | `1` hands the log to a debug-output capture tool (DebugView) from the driver's own thread as it runs, and once more at each stop. Use it only when asked |

The log cannot capture a crash: a machine that has bugchecked is not running
for anything to read.

> **Run `XHCISNAP -disable` once you have sent the capture.** While the channel
> is on, anyone using the machine can read the driver's diagnostic state
> through it, including internal addresses in the raw dump.

> **Do not run DebugView on Windows 98 on real hardware.** Under the `1.x`
> miniport, plugging in a device while it captured crashed a ThinkPad E460
> three times over. `2.0.0.0` prints only from its own thread rather than
> from interrupt context, which is what that crash was traced to, but no
> build of it has been run under DebugView on Windows 98 hardware.

## Known limitations

Each was measured, in a virtual machine unless it names a physical machine.

- **The driver never initiates selective suspend.** Idle devices and idle hub
  ports are never suspended to save power, on any target; the driver handles
  a suspend or resume it is asked for, or that a hub reports, and starts
  none. A later release may add selective suspend.
- **No USB storage on a stock Windows 98 SE.** Storage needs NUSB's
  mass-storage component; see "Windows 98 SE and ME: the stock install, and
  storage".
- **Windows Vista x64 and Windows 7 x64: driver signature enforcement must be
  disabled** at every start, or the controller sits at Code 39 and nothing on
  it works. See "What this is".
- **Windows 98 SE: a USB audio device can wedge the machine as it loads.**
  After a cold boot, attaching a USB audio device about 40 seconds after the
  boot wedged the guest in 5 of 6 tries, with the taskbar clock
  stopped; after a 120-second wait it wedged in none of 20. Every request
  the driver was given had been answered: the wait is above it, in Windows
  98's configuration manager, and `1.2.0.0` showed the same intermittent
  wedge. Plug a USB audio device in once the machine has settled.
- **Windows 98 SE in a virtual machine cannot play USB audio** through
  Windows 98's own `USBAUDIO.VXD`; that is QEMU's emulated device and the
  VxD, through this driver and through a UHCI controller alike. On real
  hardware it plays: audio was played and heard at a root port and behind a
  hub.
- **Windows 98 SE: a device at a location the system has not seen before
  raises the Add New Hardware Wizard, and holds that port until it is
  answered.** That is per port and by design of Windows 98.
- **Windows 98 shows no driver version on the Driver tab**, only the file
  date; the four-part version is under Driver File Details, which also lists
  `xhci98.tmp`, a leftover of the install's temporary copy (cosmetic).
- **USB storage on Windows 98 is slower than the drive.** An observation,
  not a defect found: Windows 98 sends one command at a time. On the P14s
  with the MSSU10 at 64 KB, about 208 MB/s on Windows 98 against 277 MB/s
  on Windows 11 at the same queue depth of one. This may be looked into in a
  later release.
- **Windows ME: do not unplug a device while Windows is installing it.**
  ME's own device manager stops responding; it does the same on Microsoft's
  own USB stack.
- **Windows ME: a UAS drive as the first USB storage device shows Code 2.**
  On a fresh Windows ME, ME has not yet copied its own `USBNTMAP.SYS` and
  `USBMPHLP.PDR`, which it installs when its first ordinary USB stick is
  plugged in. Plug in any ordinary USB stick once, then unplug the UAS drive
  and plug it back in; no Remove and no restart are needed.
- **A device moved to a different port is found again as new hardware.**
  This is a known difference from Microsoft's hub driver: this driver names
  a device by its port, and Microsoft's uses the device's serial number. A
  later release may do the same.

The `1.2.0.0` limitations, and what each is under `2.0.0.0`, are in the
next section.

## The 1.2.0.0 known limitations, under 2.0.0.0

`1.2.0.0`'s own notes (linked at the top) list its limitations. Each was
re-measured, or answered by the design, under `2.0.0.0`:

| `1.2.0.0` limitation | Under `2.0.0.0` |
|---|---|
| Windows 98 under NUSB: stopping the controller (disable, uninstall, upgrade) crashed the machine, in NUSB's `usbport.sys` | `2.0.0.0` does not use `usbport.sys`. Disable, enable, remove and rescan of the `2.0.0.0` controller completed under NUSB 3.3 and under SweetLow's stack in Windows 98 SE virtual machines. Carried on one path only: updating in place over a running `1.2.0.0` under NUSB still crashes, because NUSB stops `1.2.0.0` before `2.0.0.0` runs. "Upgrading from 1.2.0.0" has the route around it |
| Windows 98: fast, repeated plug and unplug could freeze the machine | Gone in every reading: the hub churn that froze Windows 98 under `1.2.0.0` at 12 to 18 hubs ran 120 of 120 with the guest responsive, and the device matrix's hub churn passed on Windows 98 SE and 2000 on the final build |
| Windows 7 (32-bit, on a ThinkPad E460): disabling the controller hung | Gone under `2.0.0.0`. It belonged to the miniport under Microsoft's `usbport.sys`; under `2.0.0.0` it did not occur in five disable and enable cycles each on Windows 7 x86 and x64 |
| Windows 2000: a USB audio device unplugged during playback was never fully removed | Gone: the removal arrived within about a second, 7 times out of 7 |
| The controller never went to sleep | The driver idles nothing it is not asked to; see the selective-suspend entry in "Known limitations" |
| Every root-port device reported to Windows as High Speed, and its consequences: root-port polling in 1, 2 and 4 ms bands, a Full-Speed audio device on a root port silent from Windows XP on, a USB 1.1 hub on a root port crashing Vista and 7, and the Advanced tab's bandwidth figures | The cause is gone: there is no `usbport.sys` to report to, and every device is given its true speed, read on every device row of the virtual-machine matrix on both primary targets. A Low-Speed mouse is polled every 8 ms at a root port and behind a hub, on real hardware under Windows 98 SE. A Full-Speed hub on Vista and 7 enumerated with devices behind it, and a Full-Speed audio device on a root port bound on XP, Vista and 7 and played in real time on XP x64, in virtual machines |
| Windows 98 SE: USB audio could stutter while a drive was read at full speed | Not seen under `2.0.0.0`: on real hardware under Windows 98 SE, Full-Speed audio played without stutter at the new interrupt moderation value `160` while a drive was read at full speed |
| Windows 2000: a newer package over an older one was refused; disabling the controller with an audio device attached asked for a restart | The upgrade from `1.2.0.0` installs in place on Windows 2000. The disable with an audio device attached was not re-measured |
| Windows 98: a driver that failed while starting the controller stopped the machine with a protection error | Not re-measured: no failing start was provoked under `2.0.0.0` |
| On a controller without Force Save Context, a wake from standby rebuilt the bus | Untested: the power handlers have not run |
| The virtual High-Speed hub switch and its costs. It also did nothing unless `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` were set beside `XhciVirtualHSHub`, since `1.2.0.0` has no default for either (reported on GitHub issue 4, 2026-10-03) | No switch: all three values have no effect (see "Registry settings") |

## Untested ground

Built, and not read on any vehicle. A report from any of these is new
information.

| Area | State |
|---|---|
| SuperSpeed isochronous transfers | Built from the specification against host tests. No SuperSpeed isochronous device has been held and QEMU models none |
| SuperSpeedPlus links, by mode | Accepted at the trained rate, built from the specification against host tests. Each mode is untested: Gen 2x1 (10 Gbit/s on one lane), Gen 1x2 (10 Gbit/s on two lanes, the same rate as Gen 2x1 and a different mode), and Gen 2x2 (20 Gbit/s). No Gen 2 device was read on any hardware; no 20 Gbit/s port is held |
| SuperSpeedPlus isochronous transfers | Built from the specification against host tests; no vehicle |
| SuperSpeedPlus hubs | Built from the specification against host tests; no vehicle |
| SuperSpeed hubs, on every target but Windows 98 SE | Read on real hardware under Windows 98 SE only; no virtual machine models one. On Windows 2000, which has no bench, built from the specification and untested |
| High-Speed hubs, single- and multi-TT, and Full and Low Speed devices behind them, on every target but Windows 98 SE | Read on real hardware under Windows 98 SE only. Virtual machines model only a Full-Speed hub, so on every other target the High-Speed paths rest on host tests |
| A UAS-only device at SuperSpeed on a controller that cannot stream | Built from the specification against host tests; no such controller held |

## Licensing

This driver's own source is under the GNU General Public License, version 2
(`GPL-2.0-only`); see `LICENSE`. The full third-party material and provenance
record is `docs/contributing/legal-provenance.md`.

`xhci98.sys`, `xhciuas.sys` and their INFs are this project's own work. No
Microsoft file is in the download: the `usbd.sys` and `usbui.dll` the install
needs are Windows' own and are copied by Windows from your own installation
source. On Windows 98 SE, `xhciuas.inf` names NUSB's `USBNTMAP.SYS` as a
filter on the UAS device; the file is named, never shipped, and NUSB's
mass-storage install places it.
