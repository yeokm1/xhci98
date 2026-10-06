# xhci98 - Release Notes

This file describes package version `2.2.0.0`, the eleventh release and the
fourth of the host controller driver, released on 2026-10-07 (`DriverVer`
`10/07/2026,2.2.0.0`, in all four INFs). What `2.2.0.0` changed from
`2.1.1.0`, `2.1.1.0` from `2.1.0.0`, and `2.1.0.0` from `2.0.0.0`, are
their entries in `releases/history.md`, and the sections below say where
each change was read. Where this file and
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
Controller", with "xHCI98 USB 3.x Root Hub" beneath it and the USB devices
beneath that. Since `2.1.0.0` an external hub has an entry of its own,
"xHCI98 USB Hub", with the devices behind it beneath it, and a device that
none of Windows' own INFs names is listed under its own product name; see "Devices and hubs in Device
Manager". A UAS device appears as "xHCI98 USB Attached SCSI Storage".

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
can be done on an ordinary start. After each install, the Program
Compatibility Assistant may say "Windows requires a digitally signed
driver"; the driver still loads on a start with enforcement disabled (read
on both systems for the `2.1.1.0` package). 32-bit Vista and Windows 7, and
Windows XP x64, do not refuse unsigned drivers.

## What this is not

- It is not tested evenly. Every Windows 2000 result comes from virtual
  machines: Windows 2000 Setup bugchecked on both physical machines this
  project tried, so the driver has never run on Windows 2000 on real silicon.
  Windows ME, both Windows XP builds, Windows Vista and Windows 7 are
  supported in virtual machines only for `2.2.0.0`. Windows 98 SE is the one
  target read on real hardware, at a bench session before the `2.0.0.0`
  release, on a ThinkPad E460 and a ThinkPad P14s Gen 1. `2.1.0.0`'s own
  changes were read in virtual machines only; of `2.1.1.0`'s, the Power tab
  and the Intel port switchover were read on the owner's real hardware under
  Windows 98 SE, and the registry values in virtual machines; of
  `2.2.0.0`'s, the SuperSpeed fix for Intel Sunrise Point was read on the
  owner's ThinkPad E460 under Windows 98 SE, and the handling of
  transfer, port and controller faults by making each fault it answers on
  purpose in QEMU virtual machines under Windows 98 SE and Windows 2000;
  no real controller has raised one under this driver yet.
  "Targets and their standing"
  says what each one rests on.
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
  of it is in this package. A `2.2.0.0` install replaces it, because both are
  called `xhci98.sys` (see "Upgrading from 1.2.0.0").

## Targets and their standing

Each target's row says what the `2.0.0.0` drivers were observed doing on
that system, and where; `2.1.0.0`, `2.1.1.0` and `2.2.0.0` carry that code
with the changes their history entries list, and the rows "What `2.1.0.0`
changed", "What `2.1.1.0` changed" and "What `2.2.0.0` changed" say where
each change was read. "The integration build" is the merged build of a release's
code before its cut, in the never-published `qemu` flavour. The ten install
legs are read on the `release` flavour of that build, and again from the
release asset itself after the cut.

| Target | Standing | What it rests on |
|---|---|---|
| Windows 98 SE | Primary | Virtual machines, under NUSB 3.3 and under SweetLow's stack: the install, mouse, keyboard, storage with a verified file compare, a composite audio device split into its functions and played to its end (unheard: the guest has no audio out), the ASIX Ethernet adapter passed through, the controller's Advanced tab and the root hub's Power tab, and disable, enable, remove and rescan. Hubs behind QEMU's Full-Speed hub to five tiers, 25 plug cycles per device class, and a 120-hub churn with the guest responsive. A stock install with no USB 2.0 stack: the controller, hubs, a HID mouse and a composite audio device work (next section). SuperSpeed storage and UAS at SuperSpeed (16 streams) and at High Speed, each with a verified round trip, on the integration build. UAS and forced Bulk-Only on a passed-through dual-transport bridge, each with a verified round trip. Real hardware, on a ThinkPad E460 and a ThinkPad P14s Gen 1: the install, HID, storage, the ASIX Ethernet adapter, and a Full-Speed audio device played and heard at a root port and behind a hub; High-Speed hubs, single- and multi-TT, with Low- and Full-Speed devices behind them, and a Full-Speed hub behind a High-Speed hub (a USB 2.0 hub held at Full Speed by a full/low-speed isolator); a Low-Speed mouse polled every 8 ms at a root port and behind a hub; two SuperSpeed drives on a root port, the link read as SuperSpeed in `XHCISNAP`'s slot table, with round trips and throughput against the same drive behind a USB 2.0 hub; a SuperSpeed hub with a SuperSpeed drive behind its SuperSpeed half and a High-Speed device behind its USB 2.0 half, plugged, unplugged and plugged in again; and UAS on a dual-transport bridge at SuperSpeed and, behind a USB 2.0 hub, at High Speed, UAS on a UAS flash drive at SuperSpeed with Bulk-Only behind a USB 2.0 hub, and the forced-Bulk-Only value on both at SuperSpeed, each with a round trip and throughput against Bulk-Only. On the E460, whose Intel Sunrise Point-LP controller publishes a speed table every release before `2.2.0.0` read too strictly, no SuperSpeed device was ever enumerated (`docs/issues/11-sunrise-point-ssic-psi-table.md`), so up to `2.1.1.0` the SuperSpeed clauses above can have been read on the P14s Gen 1 alone; `2.2.0.0`'s SuperSpeed reading on the E460 is in the row "What `2.2.0.0` changed" |
| Windows 2000 SP4 | Primary, virtual machines only | The same device, hub and Device Manager rows as Windows 98 SE, under Driver Verifier, plus a multiprocessor guest. SuperSpeed storage, and UAS at SuperSpeed and at High Speed, on the integration build. UAS, forced Bulk-Only and the switch between them on a passed-through dual-transport bridge, each with a verified round trip. SuperSpeed hubs: untested, as no virtual machine models one; built from the specification. Never run on real hardware |
| Windows ME | Virtual machines only | Under SweetLow's stack, on the integration build: the install, HID, storage with a verified file compare, unplug and replug, a hub with a mouse and a stick behind it, a composite audio device bound at a root port and behind a hub, the root hub's disable and enable, a 10-cycle soak per device class, SuperSpeed storage, and UAS at SuperSpeed (streams) and at High Speed. Bulk-Only storage works on Windows ME's own mass-storage files, and UAS once those are present (next section). Re-enabling the controller with a USB mouse attached hung Windows ME under `2.0.0.0` (a keyboard was never tried); `2.1.0.0` fixes it: the devices are kept across the controller's stop and revived on the re-enable, as Microsoft's hub driver does. Read on the `2.1.0.0` integration build: the re-enable with a mouse three times of three, with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; and on the `release` flavour once more after the update from `2.0.0.0` |
| 32-bit Windows XP SP3 | Virtual machines only | On the integration build: install, HID, storage with a verified file compare, unplug and replug, a hub with devices behind it, composite audio bound, the root hub's and the controller's disable and enable, shutdown; SuperSpeed storage, and UAS at SuperSpeed and at High Speed. A 10-cycle soak per device class (HID, a hub with a mouse, storage) |
| Windows XP x64 SP2 | Virtual machines only; the 64-bit drivers | The same clauses as 32-bit XP, with the 64-bit `xhciuas.sys` at SuperSpeed and at High Speed, and the same soak |
| Windows Vista SP2, 32-bit and x64 | Virtual machines only | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled. The same soak |
| Windows 7 SP1, 32-bit and x64 | Virtual machines only | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled; five controller disable and enable cycles on each. The same soak. Windows 7 is not read on real hardware for `2.0.0.0` |
| Every target, from the release package | | For `2.0.0.0`: the `release` flavour of the integration build, on the ten install legs (Windows 98 SE under NUSB and under SweetLow's stack, ME, 2000, XP, XP x64, and Vista and 7 in both architectures): installed, controller and root hub started, HID, storage with a verified file compare, composite audio bound, the controller's disable and enable, and shutdown, with SuperSpeed storage and UAS at SuperSpeed and at High Speed on most legs. The one defect was Windows ME's controller re-enable, which `2.1.0.0` fixes (the Windows ME row). For `2.1.0.0`: the `release` flavour of its integration build, installed over `2.0.0.0` on eleven virtual machines (the ten legs' systems and a stock Windows 98 SE) and read on the same clauses less SuperSpeed storage and UAS, all passing, the file compare skipped on the stock Windows 98 SE, which has no storage driver (see "Updating from 2.y.y.y"); and the ten install legs from the `2.1.0.0` release package, read after the cut, all passed: the same clauses with SuperSpeed storage and UAS at SuperSpeed and at High Speed, Windows ME's controller re-enable with a mouse attached, a hub disabled and enabled with a mouse and a stick behind it on the seven NT systems, and hidusbf Setup reading the mouse behind a hub on 32-bit XP. For `2.1.1.0`: the ten install legs from the `2.1.1.0` release package, read after the cut, all passed, on the same clauses as for `2.1.0.0` |
| What `2.1.0.0` changed | | All in QEMU on development host A, on the `2.1.0.0` integration build unless the `release` flavour is named. The controller re-enable with devices kept: Windows ME with a mouse (three of three), with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; Windows 98 SE and 2000 disable and enable with a stick behind a hub and a file compare after, and a 10-cycle soak on each; 32-bit XP disable and enable with no USB keyboard attached (XP refuses the disable while one is). Instance ids from the serial number: a stick moved between root ports kept its one entry and was not found again as new hardware on Windows 98 SE, ME, 2000 and 32-bit XP, and on Vista and 7 in both architectures; moved behind a hub as well on Windows 98 SE, 2000, 32-bit XP and 32-bit Vista, and to a SuperSpeed port on Windows 98 SE. Two sticks with different serial numbers had two entries, and of two with the same one the second was known by its port (Windows 98 SE, 2000 and XP). External hubs as entries of their own, on QEMU's hub, which is a USB 1.1 Full-Speed hub: a mouse and a stick behind it, the tree by connection, the hub unplugged and plugged in again, a device unplugged behind it, and a two-tier chain, on Windows 98 SE, 2000 and 32-bit XP; the tree and the hub unplugged and plugged in again on Vista and 7 in both architectures, the tree on ME and XP x64, and USBView walking into the hub on XP, XP x64, Vista and 7; the hub disabled and enabled in Device Manager with the devices behind it coming back, on Windows 98 SE, and on Windows 2000 and XP with the fix for the defect the first reading found (two disable and enable cycles each in QEMU, the same instance ids and the stick's files intact). A High-Speed hub, its transaction translators, and a USB 3 hub's SuperSpeed half were read in no virtual machine; the owner read a High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub" on real hardware after the cut, and both work. The Power tab: figures in mA on the root hub's and the hub's Power tabs on Windows 98 SE and 2000 (a mouse, a keyboard and an audio device 100 mA, a stick 0 mA), on 32-bit XP, and on XP x64 (a mouse and a keyboard 100 mA, a stick and the hub 0 mA; QEMU's stick and hub ask for 0 mA in their own descriptors), and a hub's Power tab on 32-bit 7. The Advanced tab's figure while audio plays was not read: Windows 98 SE's `USBAUDIO.VXD` fails on playing in QEMU ("Known limitations"). Device names: where a Windows class INF names the device, its name is shown, on every system read; the device's own product name was read in Windows 98 SE's "New Hardware Found" box, for devices no INF names on Windows 98 SE, 2000 and XP, and as Windows 7's "Bus reported device description" in both architectures. hidusbf: 1000, 500 and 250 Hz at a root port and behind a hub on Windows 98 SE under NUSB 3.6 and on ME, at a root port on 32-bit XP, the filter loaded and its setting reaching the controller on a stock Windows 98 SE (`release`), and behind a hub on 32-bit XP with the fix for the defect the first reading found (Setup read the mouse's speed, and 1000, 500 and 250 Hz programmed Interval 3, 4 and 5). Fast polling: not read. The F6 floppy, on the `release` flavour: Windows 2000 SP4 and 32-bit XP SP3 text-mode Setup with a USB keyboard and a USB stick, and the Recovery Console on both; see that section |
| What `2.1.1.0` changed | | The registry values: in QEMU on development host A, on the `release` flavour before `XhciIntelPortSwitch` was added, a fresh install on Windows 98 SE and 2000 writing the seven at their defaults, and an update over `2.1.0.0` on Windows 98 SE, 2000 and 32-bit Windows 7 keeping two values set by hand and adding the missing three ("Registry settings"). The Power tab: on the owner's ThinkPad P14s Gen 1, a SuperSpeed drive declaring 896 mA reading 510 mA and `XHCISNAP` giving its exact figure, a USB 2.0 device reading as before ("The controller's Advanced tab and the root hub's Power tab"). The Intel port switchover: on the owner's Lenovo B490 (`1E31`) under Windows 98 SE with NUSB, on Auto and Smart Auto, and its gate staying closed on the P14s Gen 1 and in QEMU ("Intel 7-, 8- and 9-series chipsets: the port switchover") |
| What `2.2.0.0` changed | | The SuperSpeed fix for Intel Sunrise Point-LP: on the owner's ThinkPad E460 (`8086:9D2F`) under Windows 98 SE, on the `release` flavour of a build taken before the cut with the fix and the enumeration notes in it, a UAS-capable USB 3 stick seen at SuperSpeed from power-on, after two unplug and replug cycles and behind a USB 3 hub, a large file copied there and back and compared clean, and ATTO Disk Benchmark at 242 MB/s writing and 245 MB/s reading at 8 MB transfers; the hub's SuperSpeed and USB 2.0 halves both present after a hot-plug, a replug and a cold boot; `XHCISNAP`'s report naming the port's speed ID 4 as SuperSpeed by its default meaning ("SuperSpeed and UAS"). No Bulk-Only SuperSpeed device was to hand. The speed tables of the ThinkPad P14s Gen 1, the Lenovo B490 and a tester's HP EliteBook 850 G5 were read with `XHCIQUAL`: the first two list ID 4, which is why they never failed, and the HP's Sunrise Point controller publishes the E460's table word for word. In QEMU on development host A, which publishes no speed table, on Windows 98 SE and on Windows 2000 SP4 at two processors under Driver Verifier: a SuperSpeed stick at a root port, a stick behind QEMU's Full-Speed hub, and a SuperSpeed unplug and replug, each with a file compare, read as `2.1.1.0` read beside them, on the integration build and, on Windows 98 SE, the `release` flavour, and again on the final code; with the E460's speed table put in QEMU's place by a test aid, the stick read at SuperSpeed by the default meaning. A SuperSpeed device behind a SuperSpeed hub was not read in QEMU, which models no SuperSpeed hub; the E460 reading above has it. `XhciIntelPortSwitch` 2: on QEMU's controller, which is not Intel, on Windows 98 SE on the `release` flavour, no switchover write and the devices working. The controller tolerance, on the `qemu` flavour with each fault made on purpose, on the same two guests: every fault design record 17 section 5 lists - a lost interrupt, a root port disabled, an over-current, the controller halted, all ones with and without the proof the controller stopped, a transaction error, the retry's failed reset and its two races, the reports nothing owns, and the control endpoint's - each made once on both guests, against a USB mouse with a Bulk-Only copy running on a SuperSpeed stick, and each made to persist on Windows 98 SE and on Windows 2000 but for three not run there (the stale context's and the retry's two races persisting); each transient fault answered with no replug, each persistent one ending in its stated state, and the counters and terminal text read in `XHCISNAP`; at `XhciTolerance` `0`, on both guests, the root port disabled, the over-current, the controller halted, the transaction error and a report nothing owns, each handled as `2.1.1.0` handles it and still counted (the others were not read at `0`). A copy running across a controller recovery fails, since every device is re-enumerated, and on Windows 2000 a mounted drive can come back at Code 31 ("Known limitations"). The three new registry values: written at `1`, `1` and `0` by the install into a driver key that had never had them on Windows 2000 and by an update on Windows 98 SE (a fresh install on Windows 98 SE was not read), and a user's `0` kept by an update on Windows 2000; `XhciIntervalCap` `2` capped the 255 ms status endpoint of QEMU's hub and `XhciAvgTrbEsit` `1` left the devices working, on Windows 98 SE on the `release` flavour |

The virtual machines are QEMU's `qemu-xhci`, which models no SuperSpeed hub,
no link faster than 5 Gbit/s, no Low-Speed device and no High-Speed hub; what
those need is the bench's, or is untested ground.

## Requirements

| | |
|---|---|
| Operating system | Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows ME (4.90.3000), 32-bit Windows XP (SP3), Windows XP x64 (SP2), and Windows Vista (SP2) and Windows 7 (SP1) in both architectures, in virtual machines only. Vista x64 and Windows 7 x64 load the drivers only while driver signature enforcement is disabled. Nothing after Windows 7 |
| USB stack | None. The driver replaces the port and hub drivers on every target. Windows 98 SE needs no USB 2.0 stack for the controller, hubs, HID or audio; **storage needs NUSB's mass-storage component** (next section). Windows ME: every Windows ME reading was taken under SweetLow's USB 2.0 stack; Windows ME without it has not been tested under the host controller driver. Do not install NUSB on Windows ME, 2000, XP, Vista or 7 |
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

Each flavour directory of the package holds five files: `xhci98.inf` and
`xhci98.sys`, the host controller driver, `xhciuas.inf` and `xhciuas.sys`,
the UAS driver, and `txtsetup.oem`, which only Windows 2000 and XP Setup read
(below). `release-x86\` is the one to install on 32-bit
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
5. An external hub installs as "xHCI98 USB Hub" from the driver already
   installed. On Windows 98 SE and 2000 it installed with nothing to answer
   (Windows 98 SE shows its "New Hardware Found" box briefly). On Windows XP
   every newly plugged hub brings the Found New Hardware wizard and the
   unsigned-driver warning, as the controller's install did; choose
   Continue Anyway. Read in virtual machines; on
   the other systems how a hub installs was not recorded.

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

### Installing Windows 2000 or XP, or using the Recovery Console: the F6 floppy

From `2.1.0.0` every flavour directory carries `txtsetup.oem`, for a machine
whose keyboard or install medium is on an xHCI controller. Copy the files of
`release-x86\` (`release-x64\` for Windows XP x64) to the root of a floppy,
press F6 when text-mode Setup offers it, press S, and pick "xHCI98 USB 3.x
Host Controller". Setup then drives a USB keyboard, mouse and USB stick
through its own drivers above this one. The same floppy serves everything
that starts from the Windows CD's text mode:

- **Installing Windows 2000 or XP.** GUI-mode Setup then installs the
  driver from `xhci98.inf` as usual.
- **The Recovery Console.** Press F6 as for an install, then R at Setup's
  Welcome screen (on Windows 2000, then C for the console). The USB
  keyboard logs in and types commands; the console runs in text mode
  throughout, so Windows XP's GUI-mode restriction below does not touch it.

Read on neither, and untested ground: a repair install, Windows 2000's
Emergency Repair Disk and Windows XP's Automated System Recovery, which run
the same text mode.

Limits:

- Pressing F6 needs the firmware's own USB keyboard support.
- The floppy must be drive A: as the firmware sees it.
- A disk the driver runs as UAS (most USB 3 enclosures and SSDs) is not
  usable until GUI-mode Setup; Bulk-Only flash sticks are.
- Installing Windows onto a USB disk is not supported.
- A USB drive present at the partition screen takes the letter C:. Unplug
  the USB drives you do not need, or Windows installs to the next letter
  (Windows XP x64 went to E: in a test).
- **Installing Windows XP and XP x64: have a PS/2 keyboard, or a laptop's
  built-in keyboard, to answer GUI-mode Setup's prompts.** Text-mode Setup
  drives the USB keyboard, mouse and stick, but it copies Windows' own
  `hidclass.sys`, `hidparse.sys` and `usbd.sys` to the new system only
  together with Microsoft's own USB controller drivers, which an xHCI-only
  machine does not use. So in GUI-mode Setup the USB keyboard and mouse do
  nothing until its "Installing Devices" step installs those files from
  the CD, and that step first shows Windows XP's unsigned-driver prompt for
  the xHCI98 controller and root hub, whose default button is No. A machine
  whose only keyboard is USB cannot answer it, and Setup stops there. A
  laptop's built-in keyboard (connected inside as PS/2, as on the ThinkPad
  E460 and P14s) or any PS/2 keyboard answers it, and after that step the
  USB keyboard and mouse work. Windows 2000 shows no such prompt. The
  package cannot carry those Microsoft files, `txtsetup.oem` can copy only
  from its own floppy, and only a signed driver or a remastered Windows CD
  would silence the prompt, so this is a limitation of this route.
- On Windows 2000, plug the USB keyboard and the USB stick in before Setup
  starts. Windows 2000's text-mode Setup uses only the USB devices present
  when the driver first reports them, as it does on its own USB stack; a
  device plugged in during text mode stays unused there. The driver waits,
  up to 5 seconds, for the devices plugged in at start before that first
  report (`XhciFirstEnumWaitMs`, under "Registry settings"; text-mode Setup
  always gets the defaults).
- GUI-mode Setup may ask for the floppy or the Windows CD.

What has run, in QEMU virtual machines on development host A only; no
install from the floppy and no Recovery Console has been run on real
hardware:

- **Text mode, on the `2.1.0.0` integration build's `release` flavour**:
  on Windows 2000 SP4 and 32-bit XP SP3, with a USB keyboard and a USB
  stick at root ports, the keyboard answered in text mode and the stick
  was listed at the partition screen. On Windows 2000 a keyboard behind a
  hub answered too (on the `qemu` flavour). Before the first-report wait
  neither the keyboard nor the stick worked in Windows 2000's text mode.
- **The Recovery Console, on the same build**: on Windows 2000 SP4 and
  32-bit XP SP3, with only the USB keyboard, the console logged in to the
  installed Windows, ran `dir` and exited.
- **Installs to the desktop, on the build of 2026-10-04 before the
  first-report wait**: on 32-bit XP SP3 and XP x64 SP2, Setup completed
  with the controller, the root hub and USB storage working, once GUI-mode
  Setup's prompts were answered from a PS/2 keyboard. Windows 2000's
  GUI-mode Setup showed no prompt and completed on that build. XP x64's
  text mode was not read again on the `2.1.0.0` build, and its Recovery
  Console not at all.

### Upgrading from 1.2.0.0

`2.2.0.0`, like every `2.x` release before it, replaces the `1.2.0.0`
driver: both are called `xhci98.sys`, and the install overwrites the file.
Going back is a reinstall of the `1.2.0.0` package from its own download.
The steps below were measured with `2.0.0.0`; nothing `2.1.0.0`, `2.1.1.0`
or `2.2.0.0` changed touches them.

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

The three `1.2.0.0` virtual-hub values have no effect under `2.x`; see
"Registry settings". Every device is a new Device Manager entry after the
upgrade, so a setting kept on a device's own entry, such as SweetLow's
hidusbf's polling rate, has to be applied again (see "SweetLow's hidusbf").

### Updating from 2.y.y.y

Install a newer `2.y.y.y` over an older one with Update Driver on "xHCI98
USB 3.x eXtensible Host Controller", pointed at the `release-` directory.
The steps are the same from every earlier `2.y.y.y`. The registry
values already there are kept and the missing ones added ("Registry
settings"); that update over `2.1.0.0` was read on Windows 98 SE (Have
Disk), 2000 and 32-bit Windows 7, and an update over `2.1.1.0` adds the
three values new in `2.2.0.0` the same way. What follows was read for
`2.1.0.0` over `2.0.0.0`. Neither
release uses NUSB's or SweetLow's port driver, so nothing needs renaming
first. Read in QEMU virtual machines on development host A, on the `release`
flavour of the `2.1.0.0` integration build installed over the published
`2.0.0.0`, on all ten install legs' systems and a stock Windows 98 SE:

- **Windows 98 SE (under NUSB, under SweetLow's stack, or stock) and ME:
  restart after the update, although Windows does not ask.** Update Driver
  offers `2.1.0.0` as a better driver, and Windows 98 SE may ask for its CD
  for `usbd.sys`. The new `xhci98.sys` cannot replace the running one, so
  Windows sets it to replace it at the next start; until then `2.0.0.0`
  keeps running. Under SweetLow's stack the controller showed a problem and
  the mouse stopped for a minute or two after Finish, then both recovered
  by themselves; on Windows ME the controller shows a problem until the
  restart, while the devices keep working.
- **Windows 2000: use Have Disk.** Update Driver letting Windows search
  answers that a suitable driver is already installed and keeps `2.0.0.0`.
  Use "Display a list of the known drivers", Have Disk, the `release-x86\`
  directory, "xHCI98 USB 3.x eXtensible Host Controller", as in the steps
  above. It runs at once, with no restart.
- **Windows XP, XP x64, Vista and 7**: read with a command-line driver
  update in place of Device Manager, so the dialogs on Device Manager's route
  (the steps above) were not read for this update. XP, XP x64 and 32-bit
  Vista showed their unsigned-driver warning once; on every one the update
  took effect at once, and no restart was asked for.

**The root hub's Driver tab keeps showing `2.0.0.0`** (on Windows 98 SE and
ME, its date) after the update, although the root hub runs the same new
`xhci98.sys` as the controller. To have it show `2.1.0.0`, run Update Driver
on "xHCI98 USB 3.x Root Hub" too, pointed at the same directory. On 32-bit
Vista and on Windows 7 x64 letting Windows search found it; on 32-bit
Windows 7 the search answered that the best driver was already installed,
and "Let me pick from a list of device drivers on my computer" with the
`2.1.0.0` entry did it; on Vista x64 a command-line update of the root hub
did it. On the other systems use Have Disk; the root hub's update was not
read there.

When updating from `2.0.0.0`, each device is found once more as new hardware, exactly
once, and Windows installs it again from the same drivers: a stick under its
serial number, which Windows now knows it by instead of its port, and the
other devices under a new id. On the NT systems this happened with nothing
to answer; on Windows 98 SE the Add New Hardware wizard ran for each and
asked for the Windows 98 SE CD again for `hidclass.sys`, and Windows ME
found each once as well. Nothing was
found again after that. A setting kept on such a device's own entry, such as
hidusbf's polling rate, has to be applied again on the new entry.

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one
installed, Windows 98 SE's "Search for a better driver" keeps the copy it
already has. Instead, Update Driver on "xHCI98 USB Attached SCSI Storage",
choose "Display a list of all the drivers in a specific location", then Have
Disk, and point it at the package directory.

## Devices and hubs in Device Manager

New in `2.1.0.0`. Every reading below is a QEMU reading on development host
A, on the `2.1.0.0` integration build; the row "What `2.1.0.0` changed"
under "Targets and their standing" lists them per system.

- **A device keeps its entry when it moves.** A device with a usable serial
  number is known to Windows by it, as under Microsoft's own hub driver: moved
  to another port, or behind a hub, it keeps its Device Manager entry and is
  not found again as new hardware. Read with a stick moved between root ports
  on every system but XP x64, behind a hub on Windows 98 SE, 2000, XP and
  32-bit Vista, and to a SuperSpeed port on Windows 98 SE. Two identical
  devices plugged into the same port in turn no longer share one entry. A
  device without a serial number, or whose serial number is not usable as a
  name, is still known by its port, as under Microsoft's hub driver, so moved
  to another port it is found again as new hardware; so is the second of two
  devices that report the same serial number while both are plugged in (read
  on Windows 98 SE, 2000 and XP). `XHCISNAP` counts that case as
  `serial.duplicate`, a counter the driver writes only when the controller
  stops, so a report taken while it runs does not show it.
- **External hubs have entries of their own.** A USB 2.0 or 1.1 hub appears
  as "xHCI98 USB Hub" beneath the port it is plugged into, with the devices
  behind it beneath it, as on Microsoft's own USB stacks. A USB 3 hub
  appears twice, once as "xHCI98 USB Hub" for its USB 2.0 half and once as
  "xHCI98 USB 3.x Hub" for its SuperSpeed half, each with the devices on
  that half beneath it, as Microsoft's own stack also shows two entries for
  one USB 3 hub. Each hub entry has a Power tab, and Device Manager's view by
  connection and USBView walk into it. The driver still runs every hub
  itself; Windows' own hub driver is not used. A hub installs from the driver
  already installed: on Windows 98 SE and 2000 with nothing to answer, and on
  Windows XP with the Found New Hardware wizard and the unsigned-driver
  warning for each newly plugged hub (see "Installing"). Disabling and
  enabling an external hub in Device Manager brings back the devices behind
  it (read twice each on Windows 98 SE, 2000 and XP in QEMU). While a drive
  behind the hub is open in Explorer, Windows refuses the disable, asks for
  a restart and leaves the hub running; close the window first (read on
  Vista in both architectures). QEMU's only hub is a USB 1.1 Full-Speed
  hub, so a High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub"
  were read in no virtual machine; the owner read both on real hardware
  after the cut, and they work.
- **Devices are named by their product name where Windows has no name for
  them.** Where one of Windows' own class INFs names a device (a mouse, a
  keyboard, a USB stick on most systems), Device Manager and the Add New
  Hardware wizard show the INF's name, as they do over Microsoft's own
  stack. The name the device reports for itself (for a part of a composite
  device, the name of that part where the device gives one) shows for a
  device no INF names, such as "QEMU USB SERIAL" on Windows 98 SE and
  "QEMU USB CCID" on Windows 2000 and XP, in Windows 98's "New Hardware
  Found" box, and as Windows 7's "Bus reported device description" on the
  Details tab. Under `2.0.0.0` each of those showed "USB Device". A device
  that reports no name is still "USB Device". On Windows 98 SE and ME
  every character outside plain ASCII is shown as `?`, since how those
  systems would convert it has not been read; the NT systems show the name
  as the device sends it. A name with characters outside plain ASCII was
  not read on any system.

## SweetLow's hidusbf: a mouse's polling rate

hidusbf, SweetLow's filter driver, now maintained by LordOfMice, sets the
polling rate Windows asks of a mouse or another HID device. It does so by
rewriting the polling interval in the configuration Windows selects for the
device, and this driver programs the controller from that configuration, so
the filter's setting reaches the controller.

Read in QEMU virtual machines on development host A, with hidusbf's release
of 2026-10-03 and the interval the driver programmed read back from the
controller's own endpoint context: a mouse set to 1000, 500 and 250 Hz was
polled every 1, 2 and 4 ms, and at its own rate with the setting off, at a
root port and behind a hub on Windows 98 SE under NUSB 3.6 and on Windows
ME, and at a root port on 32-bit XP (QEMU's mouse runs at High Speed on a
root port and at Full Speed behind QEMU's hub). On a stock Windows 98 SE,
on the `release` flavour, the filter loaded and its setting reached the
controller. hidusbf works behind a hub on XP too: its Setup reads the mouse's speed there, and 1000, 500 and 250 Hz programmed Interval 3, 4 and 5.

- **1000 Hz is the ceiling** for a Low- or Full-Speed device, as the xHCI
  specification sets it, unless `XhciFastPollFsLs` is set (under "Registry
  settings").
- **On Windows 98 SE and ME, set it on both keys.** The filter took effect
  only with its `LowerFilters` entry and `bInterval` value on both the
  device's hardware key and its `Class\HID` driver key; hidusbf's Setup
  writes them. It works on a stock Windows 98 SE as well as under NUSB:
  Windows 98 SE's own `usbd.sys` has the routine `hidusbf.sys` needs.
- **Apply the setting again after an upgrade from `1.x.x.x`**: each device
  is a new Device Manager entry under `2.x`, and the setting is kept on the
  entry. The same holds for a device found again once after an update from
  `2.0.0.0` (see "Updating from 2.y.y.y").

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
- **Intel Sunrise Point-LP (`8086:9D2F`): SuperSpeed since `2.2.0.0`.**
  Every release up to `2.1.1.0` never enumerated a SuperSpeed device on this
  controller, the USB controller of Intel's 100-series mobile chipset, found
  in the ThinkPad E460 and the HP EliteBook 850 G5 among others: a USB 3
  stick was not seen at all, and a USB 3 hub showed only its USB 2.0 half.
  The link trained; the driver could not name its speed. The controller
  publishes a table of speed IDs for its USB 3 ports that lists only three
  chip-to-chip rates and not the ID 4 it reports for a 5 Gbit/s device, and
  the driver read a published table as replacing the standard IDs. Since
  `2.2.0.0` an ID 4 to 7 that a USB 3 table does not list takes its standard
  meaning (4 is SuperSpeed at 5 Gbit/s), on every controller, and an ID the
  table does list still wins. On a controller whose table lists ID 4, as
  every other one read so far does, nothing changes. Read on the E460 under
  Windows 98 SE: a UAS stick at SuperSpeed at power-on, after replugs and
  behind a USB 3 hub, a file compared clean, about 242 MB/s writing and
  245 MB/s reading (ATTO, 8 MB transfers), and both halves of the hub
  present. `docs/issues/11-sunrise-point-ssic-psi-table.md` is the record.
  With `XhciLogVerbosity` at `2` or above, `XHCISNAP`'s report says how each
  root port's speed was read ("The log, and how to send one").
- **Throughput, for scale.** On a ThinkPad P14s Gen 1 under Windows 98 SE, an
  MSSU10 flash drive under the UAS driver at SuperSpeed read about 198 to
  224 MB/s and wrote about 205 to 217 MB/s from 512 KB transfers up (ATTO
  Disk Benchmark, Direct I/O, queue depth 1), an informal run. Windows 98
  sends one command at a time, so a drive reads slower there than on a
  modern Windows at higher queue depth.

## Intel 7-, 8- and 9-series chipsets: the port switchover

New in `2.1.1.0`. On a machine with an Intel 7-, 8- or 9-series chipset
(Ivy Bridge to Broadwell) or a C610/X99 one, each switchable connector,
usually a blue one, is wired to two USB controllers, the USB 2.0 (EHCI) one
and the xHCI one, and the chipset decides which of them has it. Firmware set
to "Auto" or "Smart Auto", or offering no setting at all, typically leaves
those connectors on the USB 2.0 controller for the operating system to move:
on Windows 7 Intel's own USB 3.0 drivers do it, and Linux does it as it
starts. Up to `2.1.0.0` this driver did not, so on such a machine it started
with no error and saw nothing on those connectors, and a SuperSpeed drive
there ran at High Speed under the USB 2.0 controller, where a driver for
that was loaded.

Since `2.1.1.0` the driver moves them itself, in the order Linux does: each
time the controller starts, and again each time it resumes from standby, it
turns on the SuperSpeed side of every connector firmware lets it switch,
then moves the USB 2.0 side of each to the xHCI controller. When the
controller stops - disabled or removed in Device Manager, a start that
fails, or the machine shutting down - it hands them back to the USB 2.0
controller. A standby or hibernate keeps them where they are.

- **Which machines.** Only an Intel xHCI controller with PCI device id
  `1E31` (7-series), `8C31` or `9C31` (8-series), `8CB1` or `9CB1`
  (9-series), or `8D31` (C610/X99). On every other controller the driver
  writes nothing for this, unless `XhciIntelPortSwitch` is set to `2` on an
  Intel one (below). One board is left alone among those: a Sony VAIO
  with subsystem id `104D:90A8`, which Linux records as unable to switch;
  and so is any board whose subsystem id cannot be read. A connector that
  firmware does not let the system switch stays with the USB 2.0 controller
  whatever the driver does.
- **A device on the USB 2.0 controller is disconnected when the driver
  starts.** Where a USB 2.0 driver is running (NUSB's or SweetLow's on
  Windows 98 SE and ME, Windows' own on 2000 and later) and has a device on
  a switchable connector when this driver's controller starts, that device
  is disconnected there and found again under this driver. A USB drive in
  use there loses its drive letter mid-session, and writes in flight are at
  risk, so do not copy files on a blue connector while installing this
  driver or enabling its controller. The reverse happens when the
  controller stops: the devices on those connectors go back to the USB 2.0
  controller, and work there if a driver for it is loaded.
- **Turning it off.** Set `XhciIntelPortSwitch` to `0` ("Registry
  settings") on a machine whose switchable connectors must stay with the
  USB 2.0 controller, for example for a device that works better there.
- **What it logs.** With `XhciLogVerbosity` at `2` or above, `XHCISNAP`'s
  report carries `psw.` records: `psw.gate` (`1` on a listed controller),
  `psw.value` with `psw.value.found` (`psw.value` 0 means turned off only
  when `psw.value.found` is 1), the two masks firmware allows and what each register read
  back after the write, and `psw.route.step`, `0` when every step was
  accepted. Since `2.2.0.0` an Intel controller's report also carries
  `psw.mode`, the setting in effect (`0` off, `1` on a listed controller,
  `2` on an unlisted Intel one by value `2`), and `psw.route.written` and
  `psw.release.written`, which of the two routing registers a route wrote
  and a release then gives back (bit 0 `USB3_PSSEN`, bit 1 `XUSB2PR`). Send
  them with any report from one of these machines.
- **A stop gives back only what was written.** Since `2.2.0.0` the hand-back
  at a stop or shutdown writes only the registers this driver's own routing
  wrote since the controller started; a routing that was refused part way
  gets back only the part it wrote. Up to `2.1.1.0` it wrote both registers
  whenever the switchover had been decided on.

**Value 2: any Intel controller, at your own risk.** The list above will miss
chipsets that have the same switchable connectors. Since `2.2.0.0`, setting
`XhciIntelPortSwitch` to `2` makes the driver do the switchover on any
Intel xHCI controller (PCI vendor `8086`), listed or not; it still does
nothing on another vendor's controller, on the Sony board, or where the
identity or subsystem id cannot be read. The driver does not check that the
registers are there:

> **Setting `XhciIntelPortSwitch` to 2 makes the driver read four Intel
> chipset registers and write two of them on any Intel USB 3 controller,
> not only on the 7-, 8- and 9-series and C610/X99 chipsets it was written
> for. On a controller that does not have those registers - every Intel
> chipset from the 100-series (Skylake) on, and any other whose layout has
> not been read - it writes registers of unknown meaning, at every start,
> resume, stop and shutdown, and the result is unknown. Use 2 only for an
> Intel chipset that has both an EHCI and an xHCI controller and that this
> driver does not list, and at your own risk. Everyone else should leave the
> value at 1.**

On a listed controller `2` does the same as `1`.

What it rests on. Read on 2026-10-05 by the owner on a Lenovo B490 (device
id `1E31`) under Windows 98 SE with NUSB, with the firmware's USB 3.0
setting on Auto: before Windows started, `XHCIQUAL` found the connectors on
the USB 2.0 controller (both routing registers 0); under the driver every
step was accepted and each register read back equal to firmware's mask; a
SuperSpeed drive on a blue connector linked at 5 Gbit/s and a file made the
round trip, and a USB 2.0 device worked under the driver; the one black
connector stayed with NUSB's USB 2.0 controller; disabling the controller
in Device Manager gave a device on a blue connector back to NUSB's driver,
and enabling it took the device back; with `XhciIntelPortSwitch` at `0` a
device on a blue connector stayed with the USB 2.0 controller; a mouse
plugged into a blue connector at boot ended up under the driver; and after a
shutdown the machine stayed off and the next boot found both registers at 0
again. On Smart Auto, the registers before Windows, the switchover, the
SuperSpeed link and the shutdown read the same. On a ThinkPad P14s Gen 1,
whose controller (`02ED`) has no USB 2.0 controller beside it, the report
showed `psw.gate` 0 and no other `psw.` record, and every external connector
worked as before. These are the owner's readings, clause by clause; the
B490's logs stayed on the B490.

Not read: a standby and resume (Windows 98 SE offers none on the B490); any
system but Windows 98 SE; the other five device ids, which rest on Linux's
handling of the same chipsets; and value `2` on any unlisted Intel
controller (see "Untested ground"). The readings above were taken on
`2.1.1.0`; `2.2.0.0`'s change to the hand-back and value `2` rest on host
tests of the sequence and on the virtual-machine reading in "Targets and
their standing".

## Controller faults: what the driver does about them

New in `2.2.0.0`. A tester on an AMD AM5 board reported that a USB mouse
stops working at random: "basically unusable" under the `1.x.x.x` releases,
less often under `2.1.x`, and for longer after being moved to another port.
Which controller sits behind those ports, and what the driver saw, have not
been sent. Looking for a cause, the project found a set of transfer, port
and controller faults that used to leave a device dead until it was
unplugged and plugged in again, with nothing in the driver noticing. Since
`2.2.0.0` the driver answers each of them itself; but for the lost
interrupt, whose pickup keeps delivering, it does so a few times at most,
and then stops in a stated state rather than try for ever:

- **A lost interrupt.** An event the controller has written and no
  interrupt has delivered is picked up by the driver's own thread once it
  has waited 100 ms, which in practice is about a third of a second after
  it was first seen.
- **A transfer error on a bulk or interrupt endpoint** (a USB Transaction
  Error: the device did not answer cleanly) is retried in place, up to three
  times for the same transfer, before the transfer fails as it did up to
  `2.1.1.0`. Not on the control endpoint, isochronous (audio) or UAS stream
  endpoints, not for a Low- or Full-Speed device behind a High-Speed hub's
  transaction translator (a mouse or a keyboard plugged into a USB 2.0 hub
  running at High Speed; one behind a Full-Speed-only hub plugged into the
  computer's own connector is retried), and
  not on the four controllers Linux excludes from the same retry (AMD
  `1022:43B9` and `1022:43BB`, Etron `1B6F:7023` and `1B6F:7052`).
- **A report the driver cannot pin on a transfer** - a completion code the
  specification gives no transfer, or an endpoint the controller halted
  with no transfer to blame, on any endpoint but an isochronous (audio)
  one - makes the driver re-enumerate the device, as
  if it had been unplugged and plugged in again. To Windows that is a
  departure and an arrival: transfers in flight end as on an unplug, a
  drive's open files with them, and the device is found again where it was.
- **A USB 2.0 root port the controller switched off** while the device is
  still connected is re-enumerated the same way; **an over-current** on a
  root port is waited out, the port powered again and its device found
  again.
- **Each port has a budget**, a root port and a port of an external hub
  alike: three re-enumerations and, on a root port, three repowers. Once it
  is spent, the next fault there removes the device and holds the port:
  nothing is enumerated there, and the report says so. A held port that is
  still powered is released by unplugging its device; one held unpowered after over-currents only by restarting the
  controller - restarting Windows, or disabling and enabling the controller
  in Device Manager. A port gets its budget back when its device then works
  with no fault for at least a minute of transfers completing (about two
  and a quarter minutes at the driver's timer's nominal rate), or when it
  is left empty and powered for at least a second (about 2.4 seconds) and a
  device is plugged in again. The driver's timer is counted short on
  purpose, so these intervals can run long but never short: a quick unplug
  and replug may not release a held port, so leave it unplugged a few
  seconds.
- **A controller that halts by itself** is recovered in place, as one that
  reports an error already was: every device on it is re-enumerated, so a
  file copy running across the recovery fails. At most three recoveries in
  a window of at least ten minutes (about 22 minutes at the nominal rate);
  after that its devices are removed and it stays stopped until it is
  restarted - disabling and enabling it in Device Manager, or restarting
  Windows.
- **A controller that stops answering at all** (its status register reading
  as all ones for a second or more) is closed off: new requests wait rather than
  fail, and once the driver has proved the controller can no longer write to
  memory, its devices are removed; without that proof everything the
  controller might still write into is kept, and only restarting the
  machine ends it: disabling the controller then hangs ("Known
  limitations").
- **A device the controller reports incompatible** (the specification's
  Incompatible Device Error) is removed and re-enumerated, as the
  specification asks, and counts against its port's budget like any
  re-enumeration, so a device that keeps getting that report ends with its
  port held. Up to `2.1.1.0` the report on a transfer was handled as an
  ordinary transfer error and the device stayed listed but unusable. This
  applies whatever `XhciTolerance` says.
- **A recovery that cannot stop the controller** now needs the same proof
  before anything the controller might still be writing into is handed
  back. Without it the controller is closed off as above, stays stopped
  until the machine is restarted, and `XHCISNAP`'s report reads "CONTAINED: halt and
  reset did not complete and Bus Master Enable would not clear; DMA not
  proven stopped, common buffer pinned". This too applies whatever
  `XhciTolerance` says.

Three rules on a controller that is latched failed - its recoveries
refused at the window's limit, or failed three times in a row - also
apply whatever `XhciTolerance` says, since they keep memory and requests
safe rather than answer a fault. When the controller shows it has halted,
the transfers it still holds are completed and its devices removed, so a
read of a drive fails at once and the controller can be disabled; a
resume from standby no longer brings such a controller back, only
disabling and enabling it or restarting Windows does (at `XhciTolerance`
`0` a change from `2.1.1.0`, where a resume could); and its state is not
saved or restored for standby while it is failed.

None of this acts while no fault is reported: the only cost then is one
memory read and one read of the controller's status register each time
the driver's thread looks, every 100 ms. A fault need not mean the
controller is unhealthy: a healthy controller reports a device's missing
handshake, a CRC failure or a timeout as a transaction error. What a class driver sees is either the same failed transfer it saw up to `2.1.1.0` or a device
leaving and arriving, never a new kind of error.

Two settings concern AMD controllers in particular (both under "Registry
settings"). **`XhciIntervalCap`**: Linux polls an interrupt endpoint that
asks for more than 32 ms every 32 ms on some AMD controllers; this driver
does the same on every AMD controller (PCI vendor `1022`), the AM5 and X570
ones included, which Linux does not list. **`XhciAvgTrbEsit`** is a
comparison switch, off by default, that changes one figure the controller is
told for each interrupt endpoint, for a tester whose machine misbehaves to
try. And **`XhciTolerance`** set to `0` turns everything in the list above
off at once, but for the last two entries and the three rules after the
list, for a machine where it does more harm than good.

`XHCISNAP`'s report counts what happened, with `XhciLogVerbosity` at `1`
or above: the transfer errors, unknown codes and unmatched events, summed
over the controller, a count of every completion code, the retries, each
re-enumeration and why, the lost interrupts the thread delivered, each
port's budget and hold, and the controller's recoveries and any
closing-off. At `XhciLogVerbosity` `2` the log also names the device and
endpoint of the first few of each error code. At `XhciTolerance` `0` the
errors, codes and unmatched events, and the root ports' disables and
over-currents, are still counted, so a report taken with the behaviours
off shows the faults they would have answered; the retries,
re-enumerations, holds, recoveries and the rest count only what the
behaviours did; those of the behaviours switched off stay at `0`, while
what applies at every value still counts (a held port after repeated
incompatible-device reports, a closing-off without proof). **If a USB device stops on your
machine**, send the controller's id (`XHCIQUAL`'s probe, or Device Manager)
and two `XHCISNAP` reports at `XhciLogVerbosity` `2`, one while it works and
one after it stops; and if it still stops, the same with `XhciAvgTrbEsit` at
`1`.

What it rests on. QEMU raises none of these faults, so each was made on
purpose in the never-published `qemu` flavour and read in QEMU virtual
machines on development host A, on Windows 98 SE and on Windows 2000 SP4
at two processors under Driver Verifier. Every fault the project's test
build can make (the lost interrupt, a root port disabled, an
over-current, the controller halted, all ones with and without the proof
the controller stopped, a transaction error, the retry's failed reset and
its two races, the reports nothing owns, and the control endpoint's) was
made once on both guests at a root port, against a USB mouse with a file
copy running on a SuperSpeed stick, and made to persist on both but for
three run on Windows 98 SE alone: a stale context and the retry's two
races, persisting. Each transient fault was answered with no replug, each
persistent one ended in its stated state, and the counters read in
`XHCISNAP` as above. At `XhciTolerance` `0`, on both guests, a root port
disabled, an over-current, the controller halted, a transaction error
and a report nothing owns were each handled as `2.1.1.0` handles it and
still counted; the other faults were not read at `0`. QEMU has no
SuperSpeed hub, so nothing was read behind one. The readings found one
defect, fixed before the release: a controller that had reached its limit
of recoveries kept its devices' transfers waiting, so a read of a drive
or a disable of the controller hung; it now removes the devices. Its
review found that a suspend could save the controller's state with
transfers still queued, also fixed.
The behaviours have not been seen to act on real hardware: no real
controller has raised one of these faults under this driver yet, and the
AM5 tester's machine has not run this release.

## Registry settings

Eleven values, each a `DWORD` in the controller's driver (software) key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host
Controller". The number varies from machine to machine, and a machine whose
controller was ever enumerated at another PCI slot can have more than one
such key: the device's own `Driver` value, under `Enum\PCI`, names the one in
use. `XHCISNAP -verbosity` finds it for you.

The install writes all eleven: each at the driver's own default, except
`XhciImodInterval250ns` at `160`. Each is written only where it is missing,
so a value you changed survives an install or update; to go back to the
default, set it by hand, or delete the value and update the driver.
`XhciTolerance`, `XhciIntervalCap` and `XhciAvgTrbEsit` are new in
`2.2.0.0`, and an update over `2.1.1.0` adds them. The install has written
every value since `2.1.1.0`, which added `XhciIntelPortSwitch`. Of the
seven before it, the
`2.1.0.0` install wrote three, the two log values and
`XhciImodInterval250ns`, and wrote them every time, over a value you had
changed. A machine
updated straight from `1.2.0.0` keeps that release's `XhciImodInterval250ns`
of `500`, not `160`; set it by hand. One that went through `2.0.0.0` or
`2.1.0.0` already holds `160`.

Read in QEMU virtual machines on development host A, on the `release`
flavour, before `XhciIntelPortSwitch` was added: a fresh install on Windows
98 SE and 2000 wrote the seven at their defaults, and an update over
`2.1.0.0` on Windows 98 SE, 2000 and 32-bit Windows 7, with
`XhciLogVerbosity` and `XhciFirstEnumWaitMs` set by hand first, kept both
and added the three that were missing, `XhciForceBulkOnly`,
`XhciFastPollFsLs` and `XhciFirstEnumPortMs`. The other systems install through INF
lines with the same values and the same flag, and were not read for this.
The three values new in `2.2.0.0` join the same INF lines with the same
flag. Read in QEMU virtual machines on development host A, before the
cut: on Windows 2000 SP4 the install wrote `XhciTolerance` `1`,
`XhciIntervalCap` `1` and `XhciAvgTrbEsit` `0` into a new driver key, and
an update with `XhciTolerance` set to `0` by hand kept the `0`; on Windows
98 SE an update over a driver key that never had them wrote all three. A
fresh install on Windows 98 SE, and a value kept by an update there, were
not read.

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
hardware. If audio stutters under storage load on your machine, raise the
value towards `4000` or delete it.

### XhciForceBulkOnly: Bulk-Only instead of UAS

`1` makes every storage device on that controller that offers both
transports use Bulk-Only (Windows' own `usbstor.sys`) instead of UAS. `0` or
absent, the default, means UAS wherever the device offers it. Since
`2.1.1.0` the install writes `0`.

It is read each time a device enumerates, so unplug and replug the device
after changing it. On Windows 2000 and later, a device already installed
keeps its driver until it is uninstalled in Device Manager and replugged. A
UAS-only device stays on UAS whatever the value says. A device whose vendor
id Windows' own `usbstor.inf` lists by hand can still get `usbstor.sys` on
the NT targets.

### XhciFastPollFsLs: Low- and Full-Speed polling above 1000 Hz

New in `2.1.0.0` and off by default; since `2.1.1.0` the install writes `0`. It lets a Low-
or Full-Speed mouse on a root port be polled faster than 1000 Hz, at the
rates hidusbf offers for that under Windows 8 and later, using hidusbf's own
numbers: the device is set in hidusbf to "31 Hz" or "62 Hz", and this value
turns those two settings into fast ones.

| Value | hidusbf "31 Hz" | hidusbf "62 Hz" |
|---|---|---|
| absent, `0`, `1` or anything else | 31 Hz | 62 Hz |
| `2` | 2000 Hz | 4000 Hz |
| `3` | 4000 Hz | 8000 Hz |

- **Root ports only.** A device behind a hub keeps its ordinary rate.
- **Outside the xHCI specification.** The specification's shortest interval
  for a Low- or Full-Speed device is 1 ms. A controller that refuses the
  shorter one is caught: the device is set up again at its ordinary rate,
  and the refusal is counted in `XHCISNAP`'s report as
  `fastpoll.fallbacks`. A controller that accepts it and then misbehaves -
  polls at its own rate, or starves other devices - cannot be caught. If
  anything misbehaves, delete the value and restart.
- **It applies to every device that asks for 16 to 63 ms.** While it is set,
  any Low- or Full-Speed device on a root port whose own polling interval is
  16 to 63 ms is polled faster too, hidusbf or not.
- It is read when the controller starts, so a change takes effect after a
  restart.

**Untested ground**: it has been read on no real controller and in no
virtual machine. The bench reading on the ThinkPad E460 and P14s Gen 1 was
not taken for this release.

### XhciFirstEnumWaitMs and XhciFirstEnumPortMs: the first report's wait

New in `2.1.0.0`. When the root hub, or an external hub, first reports its
devices to Windows after it starts, the driver first waits for the devices
already plugged in to be ready, so that they are in that first report.
Windows 2000's text-mode Setup uses only the devices in it. The wait ends as
soon as the devices are ready. Since `2.1.1.0` the install writes both at
their defaults.

| Value | Default | Meaning |
|---|---|---|
| `XhciFirstEnumWaitMs` | `5000` | The longest the first report waits, in milliseconds. `0` turns the wait off; a value above `30000` is held to `30000` |
| `XhciFirstEnumPortMs` | `2000` | The longest one port may hold it, in milliseconds; a slower device is reported later instead. Held to the total; `0` sets no limit per port |

Read with the defaults in QEMU virtual machines on development host A, on
Windows 98 SE and 2000: the root hub's first report went 20 to 30 ms after
its start with nothing plugged in, and 0.3 to 0.9 s after it with a mouse
and a stick plugged in at root ports or behind a hub; a hub's own first
report waited 0 to 31 ms. Windows 98 SE's reading at boot with a USB mouse
plugged in was taken with the virtual machine's SMM turned off (QEMU's
`-machine pc,smm=off`), because the virtual machine's firmware stopped in
SMM at boot with a USB mouse attached; that is the virtual machine's, not
the driver's. Neither value was read set to anything but its default.

### XhciIntelPortSwitch: the Intel port switchover

New in `2.1.1.0`; the install writes `1`. It matters only on the Intel
chipsets listed in "Intel 7-, 8- and 9-series chipsets: the port
switchover", unless it is `2`. Since `2.2.0.0` it is read on every Intel
controller, and still on no other vendor's.

| Value | Effect |
|---|---|
| `0` | Off: the driver leaves the connectors where they are, as every release up to `2.1.0.0` did |
| `1`, any other number but `2`, absent, or not a `DWORD` | On, on a listed controller: the driver moves the connectors to itself at each start and hands them back at each stop |
| `2` (since `2.2.0.0`) | On, on every Intel controller, listed or not. **At your own risk**: on an Intel controller without the switch registers - every Intel chipset from the 100-series (Skylake) on, and any whose layout has not been read - it writes registers of unknown meaning at every start, resume, stop and shutdown, and the result is unknown. Use it only for an Intel chipset with both an EHCI and an xHCI controller that the list misses; everyone else should leave the value at `1`. See "Value 2" in that section |

Only an explicit `0` turns it off, and only an exact `2` goes past the
list, so a mistyped value does neither. It is read
when the controller starts, so a change takes effect at the next restart or
at a disable and enable of the controller in Device Manager. Setting `0`
does not give the connectors back by itself: after a restart they are where
firmware puts them, and after a disable and enable they stay where the
disable left them, with the USB 2.0 controller. Read on a Lenovo B490 under
Windows 98 SE: at `0` and after a restart, a device on a blue connector
stayed with the USB 2.0 controller.

### XhciTolerance: the controller-fault handling, all together

New in `2.2.0.0`; the install writes `1`. It switches everything in
"Controller faults: what the driver does about them" except the two AMD
settings below and the last two entries of that list (a device reported
incompatible, a recovery that cannot stop the controller) and the three
rules on a latched-failed controller after it (its held transfers
completed, no resume bringing it back, no saved state), which apply at
every value.

| Value | Effect |
|---|---|
| `1`, any other number but `0`, absent, or not a `DWORD` | On: the lost-interrupt pickup, the transfer retry, the re-enumeration of a device after a report nothing owns, the root ports' disable and over-current handling and every port's budget, the recovery of a halted controller and its limit of three in a window of at least ten minutes, and the closing-off of a controller that stops answering |
| `0` | Off: every one of those paths is handled as `2.1.1.0` handled it, but for the rules above that apply at every value; one of them is a change from `2.1.1.0` at `0`: a resume no longer brings back a controller whose recoveries failed three times in a row |

Only an explicit `0` turns it off. It does not touch `XhciIntervalCap` or
`XhciAvgTrbEsit`, and at `0` `XHCISNAP`'s report still counts the faults
it sees, though not what the behaviours would have done about them (see
"Controller faults: what the driver does about them"). It is
read when the controller starts, so a change takes effect at the next
restart or at a disable and enable of the controller, and the new start
leaves nothing of the old setting's work behind. The budgets and intervals
themselves are fixed in the driver, not settings.

### XhciIntervalCap: the AMD interrupt-interval cap

New in `2.2.0.0`; the install writes `1`. An interrupt endpoint - a mouse,
a keyboard, a hub's status - that asks to be polled less often than every
32 ms is polled every 32 ms. Linux does the same on some AMD controllers;
this driver does it on every AMD one, since the AM5 and X570 controllers
are not on Linux's list and the report above is from an AM5 board.
Isochronous (audio) endpoints are never capped. Polling every 32 ms is
within USB 2.0 and costs a few polls a second for such a device.

| Value | Effect |
|---|---|
| `1`, any other number, absent, or not a `DWORD` | The cap on AMD controllers (PCI vendor `1022`) only |
| `0` | Off on every controller |
| `2` | The cap on every controller |

It is applied after a polling rate a setting such as hidusbf's asked for
and does not change which mouse `XhciFastPollFsLs` speeds up. It is read
when the controller starts.

### XhciAvgTrbEsit: a comparison switch

New in `2.2.0.0`; the install writes `0`. `1` tells the controller, for each
interrupt endpoint, an average transfer size equal to the endpoint's largest
transfer per service interval (held to 1 to 65535 bytes), as Linux does,
instead of the 1024 bytes the xHCI specification suggests and this driver
has always used. Isochronous and other endpoints are never changed. Any
value but `1` reads as `0`. That figure enters the controller's own
bandwidth reckoning, so it is not changed by default; the switch exists so
that a machine whose devices misbehave can be compared with it set, and
nothing ties it to the AMD report. Leave it at `0` unless you are
comparing. It is read when the controller starts.

### XhciLogVerbosity and XhciLogDebugView: the log

See "The log, and how to send one". Both default to `0`.

### The 1.2.0.0 virtual-hub values have no effect

`XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, which a
`1.2.0.0` install wrote, are not read by any `2.x` release and its INFs
write none of them. A value left behind by a `1.2.0.0` install has no effect. The driver
reports every device at its true speed with no virtual hub in the way, so
there is nothing for them to switch. Delete them if you want them gone.

## The controller's Advanced tab and the root hub's Power tab

The controller's properties carry an **Advanced** tab and the root hub's a
**Power** tab, on every target, as Windows' own USB controllers do. The pages
are Windows' own (`sysclass.dll` on Windows 98 SE and ME, `usbui.dll` on the
NT targets, with `usbui.dll` drawing the Windows 98 dialogs), and the driver
answers the requests they send. The Power tab reports the power budget the
driver itself keeps, since external hubs are the driver's own. Read on Windows 98 SE under both stacks and on Windows
2000, in virtual machines. Since `2.1.0.0` each external hub's entry has a
Power tab of its own, for the devices behind it, read on Windows 98 SE,
2000 and 32-bit Windows 7 in virtual machines.

Up to `2.0.0.0` the Power tab showed every device's power as unknown, on
every target: the driver refused the descriptor request the page sends,
whose request code Windows leaves at zero. `2.1.0.0` answers it. Read in
QEMU virtual machines on development host A, on the `2.1.0.0` integration
build: the root hub's and the hub's Power tabs list each device's power in
mA on Windows 98 SE and 2000 (a mouse, a keyboard and an audio device
100 mA, a stick 0 mA), on 32-bit XP, and on XP x64 (a mouse and a keyboard
100 mA, a stick and the hub 0 mA). The 0 mA is what QEMU's stick and hub
ask for in their own descriptors. Two things the pages show rest on Windows'
own arithmetic:

- **Bandwidth counts isochronous pipes in use, and nothing else.** On
  Windows 98 SE to XP the Advanced tab adds up only the isochronous pipes
  a device has open, as it does over Microsoft's own stack, so a mouse, a
  keyboard or a drive adds nothing, and an audio device adds its share only
  while it plays or records. That is read from Windows' own page, not on a
  running system: the figure while audio plays was not read, since Windows
  98 SE's `USBAUDIO.VXD` fails on playing in a virtual machine ("Known
  limitations"). On Vista and 7 the figure comes from a WMI query the driver
  does not answer, and stays at zero.
- **A SuperSpeed device's power: 510 mA means 510 mA or more.** The page
  doubles the configuration descriptor's `bMaxPower`, which is in 2 mA units
  at USB 2.0 and in 8 mA units at SuperSpeed, so under `2.1.0.0`, the
  first release to answer the page, a SuperSpeed device read a quarter of
  its draw. Since `2.1.1.0` the driver
  hands the page, and only the page, a SuperSpeed device's value converted
  to 2 mA units. That byte cannot count past 510 mA, so a device declaring
  up to 510 mA reads its own figure and one declaring more reads 510 mA.
  Every other reader of the descriptor gets the device's own bytes. A
  bus-powered hub is not counted this way: the page charges it
  (ports + 1) x 100 mA, at most 500 mA, whatever it declares. The
  exact figure is in `XHCISNAP`'s report, with `XhciLogVerbosity` at `2` or
  above ("The log, and how to send one"): `dev.ss.maxpower.ma`, the declared
  current in hexadecimal mA, after the device's `dev.ss.vidpid`. Read on a
  ThinkPad P14s Gen 1 under Windows 98 SE: a SuperSpeed drive declaring 896 mA, which read
  224 mA under `2.1.0.0` on Windows 98 SE, reads 510 mA, and the report
  shows `dev.ss.maxpower.ma=00000380` (896 mA); a USB 2.0 device reads as
  it did. A SuperSpeed device declaring less than 510 mA has not been read
  on hardware.

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
a copy kept from an earlier release: one from before `2.2.0.0` neither
decodes the enumeration records nor reads the per-port section below.

| Value | What it does |
|---|---|
| `XhciLogVerbosity` | `0` off (the driver does not answer `XHCISNAP` at all); `1` counters only; `2` adds the log of what happened (use this one); `3` adds the USB port register table; `4` everything, including internal addresses in the plain-text report. A value outside `0`-`4` is treated as `0` |
| `XhciLogDebugView` | `1` hands the log to a debug-output capture tool (DebugView) from the driver's own thread as it runs, and once more at each stop. Use it only when asked |

Since `2.2.0.0` the report also says what happened to a device on a root
port that did not appear. At `XhciLogVerbosity` `2` or above each root
port's enumeration leaves a few short `enum.port.` records - why the port
was looked at, the reset's result, the raw speed ID and how it was read,
Enable Slot's result, and a failure's cause - decoded in the report under
"root port enumeration notes, decoded", a few for each port between
enumerations so a port that keeps failing cannot fill the log. And from
`XhciLogVerbosity` `1`, each root port's enumeration state and failure
cause, and the driver's counters, are in the report as well, which
`2.1.1.0`'s was not. `xhcisnap/README.md` lists every record. A report
from `2.1.1.0` on a machine such as the E460 above showed the port trained
and nothing more; one from `2.2.0.0` names the step that stopped.

The log cannot capture a crash: a machine that has bugchecked is not running
for anything to read.

> **Run `XHCISNAP -disable` once you have sent the capture.** While the channel
> is on, anyone using the machine can read the driver's diagnostic state
> through it, including internal addresses in the raw dump.

> **Do not run DebugView on Windows 98 on real hardware.** Under the `1.x`
> miniport, plugging in a device while it captured crashed a ThinkPad E460
> three times over. The `2.x` driver prints only from its own thread rather than
> from interrupt context, which is what that crash was traced to, but no
> build of it has been run under DebugView on Windows 98 hardware.

## Known limitations

Each was measured, in a virtual machine unless it names a physical machine,
or says it was found by reading the code.

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
- **Windows Vista and 7: the controller's Advanced tab shows no bandwidth.**
  The figure comes from a query the driver does not answer, and stays at
  zero. See "The controller's Advanced tab and the root hub's Power tab".
- **A SuperSpeed device, other than a bus-powered hub, declaring more than
  510 mA reads 510 mA** on the
  Power tab, on every system: the value the page reads cannot count past
  510 mA. `XHCISNAP`'s report gives the exact figure. Same section. (Under
  `2.1.0.0` every SuperSpeed device read a quarter of its draw there.)
- **Intel 7-, 8- and 9-series chipsets: a device on a blue connector moves
  between the USB 2.0 controller and this driver when the controller starts
  or stops**: it is disconnected on one side and found again on the other.
  Read on a Lenovo B490 under Windows 98 SE with NUSB, with a mouse plugged
  in at boot, and with a device across a disable and enable of the
  controller. A drive in use is disconnected
  the same way, so do not have one busy on a blue connector when the
  controller starts or stops. See "Intel 7-, 8- and 9-series chipsets: the
  port switchover".
- **A device the controller faults on is unplugged and plugged in again by
  the driver** (since `2.2.0.0`; "Controller faults: what the driver does
  about them"). Whatever it was doing ends as on an unplug: a file copy to a
  drive fails part way, and a drive should be checked as after any
  surprise removal. After three on one port, the next fault there removes
  the device and holds the port, and the device stays gone until it is
  unplugged, or, on a root port held unpowered after over-currents, until
  the controller is restarted. `XhciTolerance` `0` turns it off, except for
  a device the controller reports incompatible. A recovery of the
  controller itself re-enumerates every device on it the same way, so a
  copy running across it fails, as it did under `2.1.1.0`.
- **After the driver closes off a controller it could not prove stopped,
  disabling the controller hangs: restart the machine** (since `2.2.0.0`;
  same section). Only when the controller stopped answering altogether, or
  a recovery could not stop it, and the driver could not prove it had
  stopped writing to memory, so kept everything it might still write into
  (`XHCISNAP`'s report reads "CONTAINED" and "PINNED"): a later stop of the
  controller - disabling it in Device Manager, or updating or removing its
  driver - waits for ever on the transfers kept, and only restarting the
  machine ends it. Seen in virtual machines on Windows 98 SE and 2000 with
  the fault made on purpose. Restart the machine instead of disabling the
  controller. A later release may release what is kept at the stop.
- **Windows 2000: after the driver recovers the controller, a USB drive
  whose volume was mounted may come back at Code 31** (same section). The
  mouse and other devices come back; the drive stays at Code 31 until it
  is unplugged and plugged in again, or the controller is disabled and
  enabled in Device Manager. Seen in a virtual machine (five times of five
  with the volume mounted); Windows 98 SE did not show it. The recovery
  path is the one `2.1.1.0` has, but `2.2.0.0` recovers the controller on
  more faults.
- **Windows 98 SE and ME: a device name with characters outside plain ASCII
  shows them as `?`.** See "Devices and hubs in Device Manager".
- **Windows XP and XP x64 installed with the F6 floppy: GUI-mode Setup asks
  about the unsigned driver before the USB keyboard works.** Setup copies
  Windows' own HID and USB helper files only with Microsoft's own USB
  controller drivers, so the USB keyboard and mouse wait for GUI-mode
  Setup's device install, which first shows the unsigned-driver prompt. A
  machine whose only keyboard is USB cannot answer it and Setup stops; a
  laptop's built-in keyboard or any PS/2 keyboard answers it. Windows 2000
  shows no such prompt, and the Recovery Console is not affected. See
  "Installing Windows 2000 or XP, or using the Recovery Console: the F6
  floppy".
- **After an update from `2.0.0.0`, some devices are found once more as new
  hardware**, one time each. See "Updating from 2.y.y.y".
- **After an update from `2.0.0.0`, the root hub's Driver tab still shows
  `2.0.0.0`** until the root hub is updated too, although it runs the new
  file; and Windows 98 SE and ME keep running `2.0.0.0` until the next
  restart, which they do not ask for. Same section.

`2.0.0.0`'s list also had "a device moved to a different port is found
again as new hardware", which `2.1.0.0` answers for every device with a
usable serial number, and Windows ME's controller re-enable hang, which
`2.1.0.0` fixes ("Targets and their standing").

The `1.2.0.0` limitations, and what each is under `2.0.0.0`, are in the
next section.

## The 1.2.0.0 known limitations, under 2.0.0.0

`1.2.0.0`'s own notes (linked at the top) list its limitations. Each was
re-measured, or answered by the design, under `2.0.0.0`. No change since
`2.0.0.0` answers a row differently:

| `1.2.0.0` limitation | Under `2.0.0.0` |
|---|---|
| Windows 98 under NUSB: stopping the controller (disable, uninstall, upgrade) crashed the machine, in NUSB's `usbport.sys` | `2.0.0.0` does not use `usbport.sys`. Disable, enable, remove and rescan of the `2.0.0.0` controller completed under NUSB 3.3 and under SweetLow's stack in Windows 98 SE virtual machines. Carried on one path only: updating in place over a running `1.2.0.0` under NUSB still crashes, because NUSB stops `1.2.0.0` before `2.0.0.0` runs. "Upgrading from 1.2.0.0" has the route around it |
| Windows 98: fast, repeated plug and unplug could freeze the machine | Gone in every reading: the hub churn that froze Windows 98 under `1.2.0.0` at 12 to 18 hubs ran 120 of 120 with the guest responsive, and the device matrix's hub churn passed on Windows 98 SE and 2000 on the final build |
| Windows 7 (32-bit, on a ThinkPad E460): disabling the controller hung | Gone under `2.0.0.0`. It belonged to the miniport under Microsoft's `usbport.sys`; under `2.0.0.0` it did not occur in five disable and enable cycles each on Windows 7 x86 and x64 |
| Windows 2000: a USB audio device unplugged during playback was never fully removed | Gone: the removal arrived within about a second, 7 times out of 7 |
| The controller never went to sleep | The driver idles nothing it is not asked to; see the selective-suspend entry in "Known limitations" |
| Every root-port device reported to Windows as High Speed, and its consequences: root-port polling in 1, 2 and 4 ms bands, a Full-Speed audio device on a root port silent from Windows XP on, a USB 1.1 hub on a root port crashing Vista and 7, and the Advanced tab's bandwidth figures | The cause is gone: there is no `usbport.sys` to report to, and every device is given its true speed, read on every device row of the virtual-machine matrix on both primary targets. A Low-Speed mouse is polled every 8 ms at a root port and behind a hub, on real hardware under Windows 98 SE. A Full-Speed hub on Vista and 7 enumerated with devices behind it, and a Full-Speed audio device on a root port bound on XP, Vista and 7 and played in real time on XP x64, in virtual machines |
| Windows 98 SE: USB audio could stutter while a drive was read at full speed | Not re-measured under `2.0.0.0` |
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
| Low- and Full-Speed polling above 1000 Hz (`XhciFastPollFsLs`) | Outside the xHCI specification, built against host tests; read on no real controller and in no virtual machine |
| The Intel port switchover beyond one machine | Read on one `1E31` machine under Windows 98 SE ("Intel 7-, 8- and 9-series chipsets: the port switchover"). Device ids `8C31`, `9C31`, `8CB1`, `9CB1` and `8D31`, every other system, and a standby and resume rest on Linux's handling of the same chipsets and on host tests of the sequence |
| `XhciIntelPortSwitch` `2` on an unlisted Intel controller | Built and host-tested; read on no unlisted Intel controller. At your own risk: on one without the switch registers it writes registers of unknown meaning ("Value 2") |
| The controller-fault handling on real hardware | Each fault was made on purpose at a root port in QEMU virtual machines, under Windows 98 SE and Windows 2000; QEMU has no SuperSpeed hub, so nothing was read behind one. No real controller has raised one under this driver yet, so none of the behaviours has been seen to act on real hardware ("Controller faults: what the driver does about them") |
| `XhciIntervalCap` on an AMD controller, and `XhciAvgTrbEsit` at `1` | Built against host tests. In virtual machines, on QEMU's controller, which is not AMD: `XhciIntervalCap` `2` capped the 255 ms status endpoint of QEMU's hub, and `XhciAvgTrbEsit` `1` left the mouse, the hub and a stick working, on Windows 98 SE; `XhciIntervalCap` `1` did not apply, on Windows 2000. Neither has been read on an AMD controller or on any real controller |
| Installing Windows 2000 or XP, or the Recovery Console, from the F6 floppy on real hardware | Read in virtual machines only: text mode on Windows 2000 and 32-bit XP, the Recovery Console on both, and installs to the desktop on 32-bit XP and XP x64. Never on real hardware; a repair install, Windows 2000's Emergency Repair Disk and Windows XP's Automated System Recovery on no vehicle |

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
