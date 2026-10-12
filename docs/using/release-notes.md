# xhci98 - Release Notes

This file describes package version `2.3.1.0`, the thirteenth release and
the sixth of the host controller driver, dated 2026-10-13 (`DriverVer`
`10/13/2026,2.3.1.0`, in all four INFs). It is a fix release: what it
changes from `2.3.0.0` is "What `2.3.1.0` changes", below, and what
`2.3.0.0` changed from `2.2.0.0` is "What `2.3.0.0` changed" after it;
what `2.2.0.0` changed from `2.1.1.0`, `2.1.1.0` from `2.1.0.0`, and
`2.1.0.0` from `2.0.0.0`, are their entries in `releases/history.md`, and
the sections below say where each change was read. Where this file and
`docs/contributing/roadmap-hcd.md`, `docs/contributing/build-and-test.md` or
`xhciqual/README.md` disagree, the other document wins and this one is the
copy to fix.

`2.3.0.0`'s changes were read in QEMU virtual machines, or rest on host
tests where QEMU models no such device or controller. The owner's bench of
2026-10-11 then read several on real hardware: a Dell Latitude E7250 under
Windows XP x64 and 7 x64, and a ThinkPad E460 under Windows 98 SE. "What
`2.3.0.0` changed", below, lists them and says which. `2.3.1.0`'s setup
fix was read on the same E7250 under Windows XP x64 Setup.

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

**From `2.3.0.0` the 64-bit `xhci98.sys` and `xhciuas.sys` are
test-signed, so on Windows Vista x64 and Windows 7 x64 the driver can run
in test-signing mode instead of disabling driver signature enforcement with
F8 at every boot.** Those systems refuse a kernel-mode driver without a
signature they accept; up to `2.2.0.0` the drivers were not signed, and
enforcement had to be disabled at every start. Once:

1. Open an elevated Command Prompt: Start, type `cmd`, right-click it, Run
   as administrator.
2. `bcdedit /set testsigning on` - a boot setting of the current Windows
   entry, so it stays on for every boot until it is turned off; no F8 is
   needed.
3. Restart. "Test Mode" shows on the desktop while it is on: at the bottom
   right on 7, with the version and build, and in the corners on Vista.
4. Install the driver as usual. Windows still says it cannot verify the
   publisher; choose "Install this driver software anyway". Afterwards the
   Driver tab says "Not digitally signed". Both are expected: the driver is
   test-signed, not signed by Microsoft, and the package has no catalog.

Check: `bcdedit /enum {current}` shows `testsigning Yes`. Turn it off:
`bcdedit /set testsigning off` and restart; the driver then needs F8 again,
or it does not load and the controller shows Code 39 on Vista, Code 52 on
7, with nothing plugged into it working inside Windows, a USB keyboard or mouse
included.

- With BitLocker on the system drive, suspend BitLocker before changing the
  boot setting, or the next boot asks for the recovery key.
- This is not Microsoft signing: the certificate is the project's own test
  certificate, which no Windows trusts by itself.
- Importing the `xhci98-test.cer` in the `-x64` directories is not needed:
  test-signing mode works without it, and it does not remove the install
  prompt either. It is not recommended.
- F8 -> "Disable Driver Signature Enforcement" still works as before, for
  one boot, without test mode. After an install on such a start the Program
  Compatibility Assistant may say "Windows requires a digitally signed
  driver"; the driver still loads on a start with enforcement disabled
  (read on both systems for the `2.1.1.0` package).
- 32-bit Vista and 7, and XP x64, need neither: they do not refuse unsigned
  drivers.

## What this is not

- It is not tested evenly. Every Windows 2000 result comes from virtual
  machines: Windows 2000 Setup bugchecked on both physical machines this
  project tried, so the driver has never run on Windows 2000 on real silicon.
  Windows ME, 32-bit Windows XP, Windows Vista and 32-bit Windows 7 are
  supported in virtual machines only for `2.3.1.0`. Real hardware: Windows
  98 SE, at a bench session before the `2.0.0.0` release, on a ThinkPad
  E460 and a ThinkPad P14s Gen 1, and Windows XP x64 and 7 x64 on the
  owner's Dell Latitude E7250. `2.1.0.0`'s own
  changes were read in virtual machines only; of `2.1.1.0`'s, the Power tab
  and the Intel port switchover were read on the owner's real hardware under
  Windows 98 SE, and the registry values in virtual machines; of
  `2.2.0.0`'s, the SuperSpeed fix for Intel Sunrise Point was read on the
  owner's ThinkPad E460 under Windows 98 SE, and the handling of
  transfer, port and controller faults by making each fault it answers on
  purpose in QEMU virtual machines under Windows 98 SE and Windows 2000;
  no real controller has raised one under this driver yet. Of
  `2.3.0.0`'s, the owner's bench of 2026-10-11 read polling, MSI, the
  interrupt routing repair, the port switchover, composite devices and the
  asynchronous-audio fix on real hardware ("What `2.3.0.0` changed"); of
  `2.3.1.0`'s, the Windows XP x64 Setup fix was read on the Dell Latitude
  E7250 ("What `2.3.1.0` changes").
  "Targets and their standing"
  says what each one rests on.
- It is not a mass-storage driver for Windows 98 SE. Storage on Windows 98
  SE, Bulk-Only and UAS alike, needs NUSB's mass-storage component; see
  "Windows 98 SE and ME: the stock install, and storage".
- It is not signed by Microsoft. The 32-bit drivers are not signed at all:
  Windows 98 SE and ME do not check; Windows 2000 and XP warn and install;
  32-bit Vista and 7 warn and install. From `2.3.0.0` the 64-bit drivers
  carry the project's own test signature, which Vista x64 and Windows 7 x64
  accept only in test-signing mode, and otherwise load only while driver
  signature enforcement is disabled (above). No 64-bit Windows after
  Windows 7 is supported.
- It does not put idle devices to sleep unless you turn that on. Selective
  suspend is new in `2.3.0.0` and off by default; see
  "XhciSelectiveSuspend: putting idle devices to sleep".
- It does not write to your disk. The driver creates no file. Its log is read
  out of the running driver by `XHCISNAP.EXE` when you ask for a report; see
  "The log, and how to send one".
- It is not the miniport. `1.2.0.0` is frozen and stays available; nothing
  of it is in this package. A `2.3.1.0` install replaces it, because both are
  called `xhci98.sys` (see "Upgrading from 1.2.0.0").

## What `2.3.1.0` changes

A fix release: every system `2.3.0.0` supports is supported, from the same
four directories. The Setup fix was read on real hardware, the rest in
QEMU virtual machines on development host A; each item says what was read.

- **Windows XP x64 Setup from a USB stick on the xHCI controller no longer
  stops with STOP 0x0000007B** (INACCESSIBLE_BOOT_DEVICE; reported on a
  ThinkPad X230, reproduced on a Dell Latitude E7250; issue 15 in
  `docs/issues`). When the firmware is still resetting the stick's port as
  Windows takes the controller over, `2.3.0.0` gave the port up at once
  instead of waiting, so text-mode Setup never found the stick it had started
  from. The driver now waits for that reset to end, up to one second, before
  its own; the log notes the wait as `enum.port.prwait`. A reset found
  running on any port is waited out the same way, on every target, not only
  in Setup. Read on the E7250 under Windows XP x64 Setup with the USB 3.0
  controller on: text mode reached the partition screen from the stick,
  and on the release build Setup ran through to the desktop, with no
  device in Device Manager marked with a problem.
- **In text-mode Setup a disk that offers both UAS and Bulk-Only runs as
  Bulk-Only**, so Setup's own `usbstor` drives it; `2.3.0.0` chose UAS,
  for which text mode has no driver, and the disk was not seen. A missing
  `XhciForceBulkOnly` now reads as `1` ("XhciForceBulkOnly: Bulk-Only
  instead of UAS"). Every install writes `0`, so an installed system keeps
  UAS as before. Read on the same E7250, whose stick offers both.
- **Windows ME needs no USB 2.0 stack.** Not a change in the driver: the
  `2.3.0.0` package was read on a stock Windows ME with none installed,
  and the documents now say so ("Requirements", and the Windows ME row of
  "Targets and their standing").
- **After the controller is powered off for shutdown, a new request no
  longer waits for a resume that never comes.** A transfer, command or
  port reset that arrives then is refused at once, and once the
  controller is proven halted, what was still queued is failed. A
  transfer the controller might still write into is kept, as at any stop
  where that is not proven ("Known limitations"). Standby and hibernate
  are unchanged. Hardening: no hang was traced to this gap. Read in QEMU
  on Windows 98 SE, shutting down with a stick and a mouse attached.
- **An event left waiting when the interrupt is re-armed is served at
  once**, by one more bounded pass, instead of at the driver's 100 ms
  check, which still catches one left during that pass. Seen only on QEMU's
  MSI-X, which loses such an interrupt: on Windows Vista x64 and 7 x64
  there, a UAS disk ran at about 100 MB/s where `2.3.0.0` gave about
  4 MB/s. No real controller has been seen to lose one.
- **The port switchover's scripts are in one `portsw\` directory** at the
  top of the download, beside `xhciqual\` and `xhcisnap\`, instead of a
  copy in each driver directory ("Intel 7-, 8- and 9-series chipsets: the
  port switchover"). On Windows 98 SE and ME, change into `portsw\` before
  running a batch file from a command prompt.

## What `2.3.0.0` changed

Most were read in QEMU virtual machines on development host A; the
Low-Speed mouse fix rests on host tests of that mouse's descriptors alone,
since QEMU models no such device. Where the owner's bench of 2026-10-11 read
a change on real hardware, its item says so. The sections named give the
detail, and what each rests on.

- **A controller with no working interrupt is polled, not refused.** A
  controller with no interrupt pin, or whose line the driver sees raised
  and never delivered, has its events read on a timer instead (`XhciInterruptMode`, `XhciPollIdleMs`,
  `XhciPollActiveMs`; "XhciInterruptMode, XhciPollIdleMs and
  XhciPollActiveMs: how the controller's events arrive"). Read on real
  hardware: polling by choice on a ThinkPad E460 under Windows 98 SE, and
  the move to polling of a dead line on a Dell Latitude E7250 under XP
  x64.
- **Message-signalled interrupts on Windows Vista and 7**: on x64 directly,
  on 32-bit through the package's helper `xhcimsi.sys`. The 32-bit Vista
  and 7 install asks for a restart ("Windows Vista and 7: message-signalled
  interrupts"). Read on real hardware: MSI on a Dell Latitude E7250 under
  Windows 7 x64.
- **The Intel interrupt routing repair**, for firmware that routes the
  controller's interrupt to the wrong line (`XhciIntelIrqRoute`). Read on
  real hardware: on the Dell Latitude E7250 under XP x64, and under
  Windows 7 x64 with the line asked for.
- **UAS commands of 1 MiB** where `2.2.0.0` sent 64 KiB
  (`XhciMaxTransferKB`, default `1024`), and one drive model set apart with
  Microsoft's per-device `MaximumTransferLength` (`65535` to `2097152`
  bytes) ("XhciMaxTransferKB: the UAS command size").
- **Two transfer pieces at once, off by default** (`XhciWideMap`; Windows
  2000 and later only; "XhciWideMap: two transfer pieces at once").
- **The Intel port switchover is off by default** (`XhciIntelPortSwitch`
  `0`); `portsw.vbs`, or `portsw-on.bat` and `portsw-off.bat` in one
  double-click, turns it on or off, then a restart ("Intel 7-, 8- and
  9-series chipsets: the port switchover"). Read on real hardware: the
  Dell Latitude E7250 (`9CB1`) under Windows 7 x64.
- **Selective suspend, off by default** (`XhciSelectiveSuspend`; USB 2.0
  and SuperSpeed devices; "XhciSelectiveSuspend: putting idle devices to
  sleep").
- **A composite device has an entry of its own**, and every device has
  the USB device interface ("Devices and hubs in Device Manager"). Read on
  real hardware with two USB audio devices on the Dell Latitude E7250
  under Windows 7 x64.
- **USBView shows a device's strings.** The manufacturer, product, serial
  number and interface names, and any other descriptor a tool asks for, are
  read from the device ("Devices and hubs in Device Manager"). Read on real
  hardware on the Dell Latitude E7250 under Windows 7 x64.
- **A Low-Speed mouse that carries more endpoints than it declares
  works.** A mouse (`VID_073A&PID_2230`, GitHub issue 17) whose interface
  declares one endpoint and carries two was never configured under
  `2.2.0.0` and did not move. The driver now takes the endpoints the
  interface declares and ignores the rest, as Microsoft's stack does. Read
  with that mouse's descriptors in the host tests; the mouse itself has
  not yet been read on real hardware.
- **USB audio devices with a feedback endpoint are no longer refused on
  Windows 7.** Windows 7's audio driver asks for every packet of such a
  device (an asynchronous one, such as Creative's Sound Blaster Play! 2)
  at a frame it names. On a controller without the xHCI feature that lets
  every request name a frame, such as the Intel one in a Dell Latitude
  E7250, `2.2.0.0` refused every one of those requests, so the device was sent no sound and played only hiss.
  The driver now names the frame where the controller allows it (the
  first request after the endpoint falls idle) and otherwise sends the
  request at the controller's next opportunity. Devices without a
  feedback endpoint, such as C-Media's, were never affected. Read in host
  tests and in Windows 7's audio driver's own code, and on a Dell
  Latitude E7250 under Windows 7 x64, where a Sound Blaster Play! 2 played
  cleanly at 44.1 and 48 kHz on a test build of the fix, and again on this
  release. About half the reads of its feedback endpoint end in a USB
  Transaction Error; the sound is unaffected, and the cause is open.
- **A device's speed in USBView** on Windows 98 SE and ME: the newer
  connection query that tools such as SweetLow's USBView send reads
  SuperSpeed where it is; Windows' own hub pages are unchanged ("A
  device's speed on the hub pages"), and
  `XhciFastPollFsLs` `4` polls at the rate the client asks for.
- **`XHCIQUAL` passes a controller with no working interrupt as
  `QUALIFIED WITH CAVEATS`**, exit code 3 ("Before you install: check the
  machine"). Its `--gsi` check was read on real hardware: on the Dell
  Latitude E7250 under MS-DOS 6.22 it reported the routing mismatch the
  repair fixes.
- **`XHCISNAP`'s and `XHCIQUAL`'s reports name the system** they were
  taken on, with no serial numbers or names; `XHCISNAP -nosysinfo` and
  `XHCIQUAL --no-sysinfo` skip it ("The log, and how to send one", and
  "Before you install: check the machine").
- **Driver Verifier no longer stops the machine at boot.** On 32-bit
  Windows 7 under `verifier /standard /driver xhci98.sys`, `2.2.0.0`
  stopped Windows with bugcheck C9 (0x220) as the root hub started,
  because the root hub answered a WMI request itself instead of passing it
  on; with no debugger it looked like a hang at "Starting Windows". The
  hubs now pass it on. Read on 32-bit Windows 7 under Driver Verifier on
  both drivers; 32-bit Vista and 7 x64 are still to be read.
- **Low- and Full-Speed devices behind the VIA VL805's built-in hub**
  (`1106:3483`, GitHub issue 18). The card's USB 2.0 port lists only High
  Speed in its speed table, which the xHCI specification allows for a
  controller whose built-in hub serves the slower speeds, and `2.2.0.0`
  refused every mouse, keyboard or other Low- or Full-Speed device behind
  that hub. Such a device now gets the standard speed ID. The fix was built
  without the hardware: it rests on the specification, the reporter's logs
  and what Linux, FreeBSD and Haiku do, and it has never run on a VL805. It
  is on by default, on any controller, because without it every Low- and
  Full-Speed device on such a controller fails; set
  `XhciUsb2HubSpeedFallback` to `0` to turn it off ("XhciUsb2HubSpeedFallback:
  Low- and Full-Speed IDs behind a hub").
- **A lost interrupt can no longer stop Windows 98 SE.** In `2.2.0.0`, an
  interrupt the controller raised and Windows never delivered was picked
  up by the driver's own thread. Windows 98 SE could leave that thread
  waiting while a copy to a USB drive in MS-DOS
  compatibility mode waited for the very transfer the thread would pick
  up. Such a drive is one the computer's BIOS presented at start, the "E:"
  of a stick plugged in at power-on. Both waited for ever, and the
  computer stopped. One lost interrupt in the middle of such a copy was
  enough, in the default `XhciInterruptMode` `0`. On a controller whose
  interrupt never arrives at all, with `XhciInterruptMode` `1`, it
  happened on the first copy. The pickup now runs from the driver's
  100 ms timer instead, which kept running when the thread did not. Read
  in QEMU on Windows 98 SE, with lost interrupts made on purpose ("Controller
  faults: what the driver does about them").
- **Every registry value the driver reads is in its key after an
  install.** The install writes all nineteen, now including `XhciWideMap`
  `0`, `XhciSelectiveSuspend` `0` and `XhciUsb2HubSpeedFallback` `1`, each
  the value a missing one reads as, so nothing behaves differently; an
  update keeps a value you set ("Registry settings").
- **A UAS drive keeps working after a transfer error on its data.** A
  USB Transaction Error on a UAS drive's data or status stream is now
  retried in place, as on any other bulk endpoint ("Controller faults:
  what the driver does about them"); before, the read or write failed and
  the drive was reset. And after a reset of the drive, the driver starts
  each of its streams afresh. Read in QEMU on development host A with the
  error made on purpose; a real drive's error has not yet been read on
  real hardware.
- **UAS in QEMU: a workaround for an emulator defect.** QEMU's xHCI (read
  at QEMU 11.1.0, `hw/usb/hcd-xhci.c`: `xhci_find_stream` calls
  `xhci_ring_init`, which forces the consumer cycle to 1) ignores the cycle
  bit a UAS stream's context carries whenever an endpoint is stopped, reset
  or reconfigured, so a stream whose ring had wrapped an odd number of
  times stopped moving data: a UAS disk in a QEMU guest could then hang
  after any transfer error, cancel or reset until it was removed and added
  again. On QEMU's xHCI alone (PCI ID `1B36:000D`) the driver now gives
  each stream its cycle back after such a command. Real controllers read
  that bit and never take this path.

## Targets and their standing

Each target's row says what the `2.0.0.0` drivers were observed doing on that
system, and where; `2.1.0.0`, `2.1.1.0`, `2.2.0.0`, `2.3.0.0` and `2.3.1.0`
carry that code with the changes their history entries list, and the rows "What
`2.1.0.0` changed", "What `2.1.1.0` changed", "What `2.2.0.0` changed", "What
`2.3.0.0` changed" and "What `2.3.1.0` changes" say where each change was read.
"The integration build" is the merged build of a release's code before its cut,
in the never-published `qemu` flavour. The ten install legs are read on the
`release` flavour of that build, and again from the release asset itself after
the cut.

| Target | Standing | What it rests on |
|---|---|---|
| Windows 98 SE | Primary | Virtual machines, under NUSB 3.3 and under SweetLow's stack: the install, mouse, keyboard, storage with a verified file compare, a composite audio device split into its functions and played to its end (unheard: the guest has no audio out), the ASIX Ethernet adapter passed through, the controller's Advanced tab and the root hub's Power tab, and disable, enable, remove and rescan. Hubs behind QEMU's Full-Speed hub to five tiers, 25 plug cycles per device class, and a 120-hub churn with the guest responsive. A stock install with no USB 2.0 stack: the controller, hubs, a HID mouse and a composite audio device work (next section). SuperSpeed storage and UAS at SuperSpeed (16 streams) and at High Speed, each with a verified round trip, on the integration build. UAS and forced Bulk-Only on a passed-through dual-transport bridge, each with a verified round trip. Real hardware, on a ThinkPad E460 and a ThinkPad P14s Gen 1: the install, HID, storage, the ASIX Ethernet adapter, and a Full-Speed audio device played and heard at a root port and behind a hub; High-Speed hubs, single- and multi-TT, with Low- and Full-Speed devices behind them, and a Full-Speed hub behind a High-Speed hub (a USB 2.0 hub held at Full Speed by a full/low-speed isolator); a Low-Speed mouse polled every 8 ms at a root port and behind a hub; two SuperSpeed drives on a root port, the link read as SuperSpeed in `XHCISNAP`'s slot table, with round trips and throughput against the same drive behind a USB 2.0 hub; a SuperSpeed hub with a SuperSpeed drive behind its SuperSpeed half and a High-Speed device behind its USB 2.0 half, plugged, unplugged and plugged in again; and UAS on a dual-transport bridge at SuperSpeed and, behind a USB 2.0 hub, at High Speed, UAS on a UAS flash drive at SuperSpeed with Bulk-Only behind a USB 2.0 hub, and the forced-Bulk-Only value on both at SuperSpeed, each with a round trip and throughput against Bulk-Only. On the E460, whose Intel Sunrise Point-LP controller publishes a speed table every release before `2.2.0.0` read too strictly, no SuperSpeed device was ever enumerated (`docs/issues/11-sunrise-point-ssic-psi-table.md`), so up to `2.1.1.0` the SuperSpeed clauses above can have been read on the P14s Gen 1 alone; `2.2.0.0`'s SuperSpeed reading on the E460 is in the row "What `2.2.0.0` changed" |
| Windows 2000 SP4 | Primary, virtual machines only | The same device, hub and Device Manager rows as Windows 98 SE, under Driver Verifier, plus a multiprocessor guest. SuperSpeed storage, and UAS at SuperSpeed and at High Speed, on the integration build. UAS, forced Bulk-Only and the switch between them on a passed-through dual-transport bridge, each with a verified round trip. SuperSpeed hubs: untested, as no virtual machine models one; built from the specification. Never run on real hardware |
| Windows ME | Virtual machines only | Under SweetLow's stack, on the integration build: the install, HID, storage with a verified file compare, unplug and replug, a hub with a mouse and a stick behind it, a composite audio device bound at a root port and behind a hub, the root hub's disable and enable, a 10-cycle soak per device class, SuperSpeed storage, and UAS at SuperSpeed (streams) and at High Speed. Bulk-Only storage works on Windows ME's own mass-storage files, and UAS once those are present (next section). Re-enabling the controller with a USB mouse attached hung Windows ME under `2.0.0.0` (a keyboard was never tried); `2.1.0.0` fixes it: the devices are kept across the controller's stop and revived on the re-enable, as Microsoft's hub driver does. Read on the `2.1.0.0` integration build: the re-enable with a mouse three times of three, with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; and on the `release` flavour once more after the update from `2.0.0.0`. On a stock Windows ME, with no USB 2.0 stack installed, the `2.3.0.0` release package (2026-10-12): the install, HID, a hub, a composite audio device behind the hub playing a tone whole, Bulk-Only storage on Windows ME's own files and UAS at SuperSpeed with verified file compares, the controller's disable, enable, remove and rescan, and shutdown |
| 32-bit Windows XP SP3 | Virtual machines only | On the integration build: install, HID, storage with a verified file compare, unplug and replug, a hub with devices behind it, composite audio bound, the root hub's and the controller's disable and enable, shutdown; SuperSpeed storage, and UAS at SuperSpeed and at High Speed. A 10-cycle soak per device class (HID, a hub with a mouse, storage) |
| Windows XP x64 SP2 | Virtual machines, and real hardware on the Dell Latitude E7250; the 64-bit drivers | The same clauses as 32-bit XP, with the 64-bit `xhciuas.sys` at SuperSpeed and at High Speed, and the same soak. Real hardware: XP x64 on the owner's Dell Latitude E7250 |
| Windows Vista SP2, 32-bit and x64 | Virtual machines only | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled, the drivers being unsigned then; the test-signed x64 drivers of `2.3.0.0` were read in test-signing mode, with no F8. The same soak |
| Windows 7 SP1, 32-bit and x64 | Virtual machines; x64 also on real hardware, the Dell Latitude E7250 | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled, the drivers being unsigned then, and the test-signed x64 drivers of `2.3.0.0` in test-signing mode, with no F8; five controller disable and enable cycles on each. The same soak. Windows 7 is not read on real hardware for `2.0.0.0`. Real hardware: 7 x64 on the owner's Dell Latitude E7250 |
| Every target, from the release package | | For `2.0.0.0`: the `release` flavour of the integration build, on the ten install legs (Windows 98 SE under NUSB and under SweetLow's stack, ME, 2000, XP, XP x64, and Vista and 7 in both architectures): installed, controller and root hub started, HID, storage with a verified file compare, composite audio bound, the controller's disable and enable, and shutdown, with SuperSpeed storage and UAS at SuperSpeed and at High Speed on most legs. The one defect was Windows ME's controller re-enable, which `2.1.0.0` fixes (the Windows ME row). For `2.1.0.0`: the `release` flavour of its integration build, installed over `2.0.0.0` on eleven virtual machines (the ten legs' systems and a stock Windows 98 SE) and read on the same clauses less SuperSpeed storage and UAS, all passing, the file compare skipped on the stock Windows 98 SE, which has no storage driver (see "Updating from 2.y.y.y"); and the ten install legs from the `2.1.0.0` release package, read after the cut, all passed: the same clauses with SuperSpeed storage and UAS at SuperSpeed and at High Speed, Windows ME's controller re-enable with a mouse attached, a hub disabled and enabled with a mouse and a stick behind it on the seven NT systems, and hidusbf Setup reading the mouse behind a hub on 32-bit XP. For `2.1.1.0`: the ten install legs from the `2.1.1.0` release package, read after the cut, all passed, on the same clauses as for `2.1.0.0` |
| What `2.1.0.0` changed | | All in QEMU on development host A, on the `2.1.0.0` integration build unless the `release` flavour is named. The controller re-enable with devices kept: Windows ME with a mouse (three of three), with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; Windows 98 SE and 2000 disable and enable with a stick behind a hub and a file compare after, and a 10-cycle soak on each; 32-bit XP disable and enable with no USB keyboard attached (XP refuses the disable while one is). Instance ids from the serial number: a stick moved between root ports kept its one entry and was not found again as new hardware on Windows 98 SE, ME, 2000 and 32-bit XP, and on Vista and 7 in both architectures; moved behind a hub as well on Windows 98 SE, 2000, 32-bit XP and 32-bit Vista, and to a SuperSpeed port on Windows 98 SE. Two sticks with different serial numbers had two entries, and of two with the same one the second was known by its port (Windows 98 SE, 2000 and XP). External hubs as entries of their own, on QEMU's hub, which is a USB 1.1 Full-Speed hub: a mouse and a stick behind it, the tree by connection, the hub unplugged and plugged in again, a device unplugged behind it, and a two-tier chain, on Windows 98 SE, 2000 and 32-bit XP; the tree and the hub unplugged and plugged in again on Vista and 7 in both architectures, the tree on ME and XP x64, and USBView walking into the hub on XP, XP x64, Vista and 7; the hub disabled and enabled in Device Manager with the devices behind it coming back, on Windows 98 SE, and on Windows 2000 and XP with the fix for the defect the first reading found (two disable and enable cycles each in QEMU, the same instance ids and the stick's files intact). A High-Speed hub, its transaction translators, and a USB 3 hub's SuperSpeed half were read in no virtual machine; the owner read a High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub" on real hardware after the cut, and both work. The Power tab: figures in mA on the root hub's and the hub's Power tabs on Windows 98 SE and 2000 (a mouse, a keyboard and an audio device 100 mA, a stick 0 mA), on 32-bit XP, and on XP x64 (a mouse and a keyboard 100 mA, a stick and the hub 0 mA; QEMU's stick and hub ask for 0 mA in their own descriptors), and a hub's Power tab on 32-bit 7. The Advanced tab's figure while audio plays was not read: Windows 98 SE's `USBAUDIO.VXD` fails on playing in QEMU ("Known limitations"). Device names: where a Windows class INF names the device, its name is shown, on every system read; the device's own product name was read in Windows 98 SE's "New Hardware Found" box, for devices no INF names on Windows 98 SE, 2000 and XP, and as Windows 7's "Bus reported device description" in both architectures. hidusbf: 1000, 500 and 250 Hz at a root port and behind a hub on Windows 98 SE under NUSB 3.6 and on ME, at a root port on 32-bit XP, the filter loaded and its setting reaching the controller on a stock Windows 98 SE (`release`), and behind a hub on 32-bit XP with the fix for the defect the first reading found (Setup read the mouse's speed, and 1000, 500 and 250 Hz programmed Interval 3, 4 and 5). Fast polling: not read. The F6 floppy, on the `release` flavour: Windows 2000 SP4 and 32-bit XP SP3 text-mode Setup with a USB keyboard and a USB stick, and the Recovery Console on both; see that section |
| What `2.1.1.0` changed | | The registry values: in QEMU on development host A, on the `release` flavour before `XhciIntelPortSwitch` was added, a fresh install on Windows 98 SE and 2000 writing the seven at their defaults, and an update over `2.1.0.0` on Windows 98 SE, 2000 and 32-bit Windows 7 keeping two values set by hand and adding the missing three ("Registry settings"). The Power tab: on the owner's ThinkPad P14s Gen 1, a SuperSpeed drive declaring 896 mA reading 510 mA and `XHCISNAP` giving its exact figure, a USB 2.0 device reading as before ("The controller's Advanced tab and the root hub's Power tab"). The Intel port switchover: on the owner's Lenovo B490 (`1E31`) under Windows 98 SE with NUSB, on Auto and Smart Auto, and its gate staying closed on the P14s Gen 1 and in QEMU ("Intel 7-, 8- and 9-series chipsets: the port switchover") |
| What `2.2.0.0` changed | | The SuperSpeed fix for Intel Sunrise Point-LP: on the owner's ThinkPad E460 (`8086:9D2F`) under Windows 98 SE, on the `release` flavour of a build taken before the cut with the fix and the enumeration notes in it, a UAS-capable USB 3 stick seen at SuperSpeed from power-on, after two unplug and replug cycles and behind a USB 3 hub, a large file copied there and back and compared clean, and ATTO Disk Benchmark at 242 MB/s writing and 245 MB/s reading at 8 MB transfers; the hub's SuperSpeed and USB 2.0 halves both present after a hot-plug, a replug and a cold boot; `XHCISNAP`'s report naming the port's speed ID 4 as SuperSpeed by its default meaning ("SuperSpeed and UAS"). No Bulk-Only SuperSpeed device was to hand. The speed tables of the ThinkPad P14s Gen 1, the Lenovo B490 and a tester's HP EliteBook 850 G5 were read with `XHCIQUAL`: the first two list ID 4, which is why they never failed, and the HP's Sunrise Point controller publishes the E460's table word for word. In QEMU on development host A, which publishes no speed table, on Windows 98 SE and on Windows 2000 SP4 at two processors under Driver Verifier: a SuperSpeed stick at a root port, a stick behind QEMU's Full-Speed hub, and a SuperSpeed unplug and replug, each with a file compare, read as `2.1.1.0` read beside them, on the integration build and, on Windows 98 SE, the `release` flavour, and again on the final code; with the E460's speed table put in QEMU's place by a test aid, the stick read at SuperSpeed by the default meaning. A SuperSpeed device behind a SuperSpeed hub was not read in QEMU, which models no SuperSpeed hub; the E460 reading above has it. `XhciIntelPortSwitch` 2: on QEMU's controller, which is not Intel, on Windows 98 SE on the `release` flavour, no switchover write and the devices working. The controller tolerance, on the `qemu` flavour with each fault made on purpose, on the same two guests: every fault design record 17 section 5 lists - a lost interrupt, a root port disabled, an over-current, the controller halted, all ones with and without the proof the controller stopped, a transaction error, the retry's failed reset and its two races, the reports nothing owns, and the control endpoint's - each made once on both guests, against a USB mouse with a Bulk-Only copy running on a SuperSpeed stick, and each made to persist on Windows 98 SE and on Windows 2000 but for three not run there (the stale context's and the retry's two races persisting); each transient fault answered with no replug, each persistent one ending in its stated state, and the counters and terminal text read in `XHCISNAP`; at `XhciTolerance` `0`, on both guests, the root port disabled, the over-current, the controller halted, the transaction error and a report nothing owns, each handled as `2.1.1.0` handles it and still counted (the others were not read at `0`). A copy running across a controller recovery fails, since every device is re-enumerated, and on Windows 2000 a mounted drive can come back at Code 31 ("Known limitations"). The three new registry values: written at `1`, `1` and `0` by the install into a driver key that had never had them on Windows 2000 and by an update on Windows 98 SE (a fresh install on Windows 98 SE was not read), and a user's `0` kept by an update on Windows 2000; `XhciIntervalCap` `2` capped the 255 ms status endpoint of QEMU's hub and `XhciAvgTrbEsit` `1` left the devices working, on Windows 98 SE on the `release` flavour |
| What `2.3.0.0` changed | | In QEMU on development host A, on the integration builds before the cut; each item's section says what was read and on which systems. Then the owner's bench of 2026-10-11, on the signed release package. On a Dell Latitude E7250 (`8086:9CB1`) under Windows 7 x64, in test-signing mode: MSI with the routing repair writing nothing, and BackstopDrains 0; the line under `XhciInterruptMode` `1`, with the repair rewriting the route, also with the EHCI disabled; the switchover at `0` leaving a USB 3 stick at USB 2.0 speed and at `1` giving SuperSpeed; two USB audio devices, each under its composite entry, with USBView showing their strings, and the asynchronous one playing on the fix. Under XP x64: the repair rewriting the route at the default, and with it off, the dead line moved to polling and a SuperSpeed stick copying clean. On a ThinkPad E460 under Windows 98 SE: about 18 GB through a SuperSpeed stick on the line and about 4 GB under polling, every copy compared clean. On a ThinkPad P14s Gen 1 under Windows 98 SE, brief checks only. Not read: standby, remote wake, MSI-X, Windows 98 SE on the E7250, and the Low-Speed mouse of GitHub issue 17 ("Untested ground") |
| What `2.3.1.0` changes | | The Setup fix: on the owner's Dell Latitude E7250 (`8086:9CB1`) with the USB 3.0 controller on, Windows XP x64 Setup started from a WinSetupFromUSB stick carrying the X230 reporter's nLite-integrated install, on a build with both fixes: text mode reached the partition screen, where `2.3.0.0` stopped with 0x7B; the stick offers both transports and ran as Bulk-Only there. Then, on the `release-x64` build of the fix (2026-10-12, before the version stamp), the install ran from the stick to the desktop, with no device in Device Manager marked with a problem. The stock Windows ME reading is the Windows ME row's, on the `2.3.0.0` package. The shutdown latch, in QEMU on development host A: Windows 98 SE shut down twice with a boot-present stick and a mouse attached. The re-arm check: under QEMU's MSI-X on Vista x64 and 7 x64, BackstopDrains 0 and UAS at about 100 MB/s, and under MSI and the line, nothing queued by it. The `portsw\` layout: a local cut's tree. Not read: a real MSI-X controller, and the X230 itself |

The virtual machines are QEMU's `qemu-xhci`, which models no SuperSpeed hub,
no link faster than 5 Gbit/s, no Low-Speed device and no High-Speed hub; what
those need is the bench's, or is untested ground.

## Requirements

| | |
|---|---|
| Operating system | Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows ME (4.90.3000), 32-bit Windows XP (SP3), Windows XP x64 (SP2), and Windows Vista (SP2) and Windows 7 (SP1) in both architectures, in virtual machines only, but for Windows XP x64 and Windows 7 x64, which have also run on the owner's Dell Latitude E7250. Vista x64 and Windows 7 x64 load the drivers in test-signing mode or while driver signature enforcement is disabled (F8). Nothing after Windows 7 |
| USB stack | None. The driver replaces the port and hub drivers on every target. Windows 98 SE needs no USB 2.0 stack for the controller, hubs, HID or audio; **storage needs NUSB's mass-storage component** (next section). Windows ME needs no USB 2.0 stack either: the `2.3.0.0` package was read on a stock Windows ME with none installed (the Windows ME row), and the earlier readings under SweetLow's USB 2.0 stack stand. Do not install NUSB on Windows ME, 2000, XP, Vista or 7 |
| Controller | An xHCI controller presenting PCI class code `0C0330`, with a BAR0 mapped below 4 GB. Since `2.3.0.0` an interrupt pin is no longer required: a controller reporting `Interrupt Pin = 0` is polled, as is one whose line the driver sees raised and never delivered (another kind of dead interrupt needs `XhciInterruptMode` `2`), and on Windows Vista and 7 a controller may use a message-signalled interrupt instead of its pin ("XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs"). Up to `2.2.0.0` such a controller could not be driven at all. A controller with no USB 2.0 protocol port at all is accepted, built from the specification and untested: no such controller has been held |
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

**Windows ME needs no USB 2.0 stack either.** On a stock Windows ME install,
with no USB 2.0 stack, the driver installs with no file asked for and a
restart, and then drives hubs, HID devices, composite audio devices and
storage through Windows ME's own class drivers (read on `2.3.0.0`,
2026-10-12). SweetLow's stack, if installed, is likewise simply not used.

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
quick scan. It writes no PCI configuration register and prints one of four
verdicts:

| Verdict | Means |
|---|---|
| `LOOKS QUALIFIED` | nothing a read-only pass can see disqualifies this machine; the active tests still decide |
| `LOOKS QUALIFIED WITH CAVEATS` | the same, but the controller has no line interrupt (`Interrupt Pin = 0`): the driver polls it, or an MSI-capable OS takes a message interrupt instead; the line says whether it offers MSI |
| `DISQUALIFIED` | something a read-only pass genuinely sees: no `CC_0C0330` function, BAR0 unusable or above 4 GB, or no USB 2.0 protocol ports |
| `CANNOT SAY` | a state this pass may not change is in the way: the controller is not in D0, or Memory Space Enable is clear |

The qualifier is the same program the `1.x` releases carried, and its last
criterion, no USB 2.0 protocol ports, is the miniport's; see the Controller
row of "Requirements".

The full active run (`xhciqual/hardware-testing.md`) ends on `QUALIFIED`,
`NOT QUALIFIED`, or, since `2.3.0.0`, **`QUALIFIED WITH CAVEATS`**: a
controller with no working interrupt - no interrupt pin, or an interrupt that
never arrives while the controller's events reach memory - that passes the
rest when polled. It is not refused, because the driver polls such a
controller: with no pin under `XhciInterruptMode` 0, automatic; with an
interrupt that never arrives under `0` only where the driver sees the
controller raise it, otherwise under `2`, which the verdict names; at the cost of more CPU
wake-ups and up to the poll period of event latency; or an MSI-capable OS
takes a message interrupt, where the controller offers one (Windows Vista and
7: x64 directly, 32-bit with the `xhcimsi.sys` helper). The verdict prints
which applies. Its exit code is 3 (0 qualified, 1 not qualified, 2 a usage
error or no controller).

A passing interrupt test under DOS proves the line in the routing DOS
uses, which is not always the routing Windows uses. On the Dell Latitude
E7250 the test passed on IRQ 11, while under Windows XP x64 and 7 the same
line never reached the interrupt Windows assigned. `--gsi N`, beside
`--quick` or `--probe-only`, compares the Intel chipset's routing with N,
the interrupt Windows reports for the controller (Device Manager,
Resources tab), and on the E7250 it reported the mismatch ("XhciIntelIrqRoute: the Intel interrupt routing
repair").

Since `2.3.0.0` the report's header also names the machine it ran on: the
make, model and BIOS from the firmware's SMBIOS table, the processor, the
memory and the DOS version. It never prints a serial number, a UUID, an
asset tag, a SKU or a part number, and withholds any other string that repeats one, so a
report can be pasted into a public issue as it is. The quick scan shows
two summary lines on the screen and the whole block in its log. If reading
these details causes trouble on a machine, add `--no-sysinfo`: the header
then says they were skipped, and the verdict and every test are the same.

It is in the download's `xhciqual\` directory. **It must be run from real
DOS**, not a DOS box inside Windows, booted without EMM386 or any other V86 or
paging memory manager. `HIMEM.SYS` is allowed, and on some machines it is
needed: if the tool will not run at all on a boot that loads nothing, add
`DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` to `CONFIG.SYS` (adjusting the path)
and try again. `xhciqual/hardware-testing.md` has the staged active tests,
the safety notes, and how to read each result.

## Installing

Each flavour directory of the package holds six files (nine in `2.3.0.0`,
below). Five are in every
one: `xhci98.inf` and `xhci98.sys`, the host controller driver,
`xhciuas.inf` and `xhciuas.sys`, the UAS driver, and `txtsetup.oem`, which
only Windows 2000 and XP Setup read (below). The sixth is
`xhcimsi.sys` in the x86 directories, the MSI helper the INF installs on
32-bit Vista and 7 only, and `xhci98-test.cer` in the x64 directories, the
public half of the certificate the x64 drivers are test-signed with. The
Intel port switchover's script, `portsw.vbs`, and `portsw-on.bat` and
`portsw-off.bat`, which turn the switchover on or off through it in one
double-click, are not part of the install. After `2.3.0.0` they are in the
download's `portsw\` directory, one copy for every flavour directory; in
`2.3.0.0` they are at the root of each flavour directory. `release-x86\` is the one to install on 32-bit
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
   USB 3.x Root Hub" beneath it. Restart if asked: Windows 98 SE asks once,
   after the controller's install, and 32-bit Vista and 7 ask, for the MSI
   helper, holding the controller (Code 28 on 7, Code 1 on Vista) until
   the restart. After any restart asked for, neither entry should carry a
   warning mark.
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
- A disk that offers only UAS is not usable until GUI-mode Setup. Since
  `2.3.1.0` a disk that offers Bulk-Only as well as UAS, as most USB 3
  enclosures, SSDs and sticks do, runs as Bulk-Only in text mode and is
  usable there (`XhciForceBulkOnly`); Bulk-Only flash sticks always are.
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

`2.3.0.0`, like every `2.x` release before it, replaces the `1.2.0.0`
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
USB 3.x eXtensible Host Controller", then on "xHCI98 USB 3.x Root Hub",
both pointed at the `release-` directory.
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

**On Vista and 7, Have Disk does not take a build whose INF is identical
to the installed one** (the same `DriverVer`): Windows reuses the package
already in its driver store and keeps the old `xhci98.sys`. Replace the
`.sys` by hand, or use a build with a newer `DriverVer` (read on the
E7250 under Windows 7 x64).

**An update keeps the Intel port switchover as it was.** Since `2.3.0.0` a
new install writes `XhciIntelPortSwitch` at `0`, off; `2.1.1.0` and
`2.2.0.0` wrote `1`, and an update never overwrites a value that is already
there, so a machine updated from either keeps the switchover on. To take the
new default, run `portsw-off.bat` (or `portsw.vbs 0`) from the download's
`portsw\` directory (in `2.3.0.0`, from the flavour directory) and restart
Windows
("Intel 7-, 8- and 9-series chipsets: the port switchover").

**An update from `2.2.0.0` adds the eight values new in `2.3.0.0`** at
their defaults and keeps the rest ("Registry settings"). On 32-bit Vista
and 7 it also installs `xhcimsi.sys` and asks for a restart ("Windows
Vista and 7: message-signalled interrupts").

**Deleting the driver keeps the older packages.** On Vista and 7,
uninstalling the controller in Device Manager with "Delete the driver
software for this device" ticked removes only the package in use. Every
older `xhci98` package installed on that machine stays in Windows' driver
store, and the next time Windows finds the controller it may install one
of them (read on 32-bit Windows 7). To remove them all, list the store
from an administrator's prompt with `pnputil -e`, and remove each
`oemNN.inf` it lists with the provider "Yeo Kheng Meng" and the class
"Universal Serial Bus controllers" with `pnputil -d oemNN.inf`. The same
applies before going back to an older version
("Windows Vista and 7: message-signalled interrupts").

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
- **Every device has the USB device interface** (new in `2.3.0.0`; GitHub
  issue 17). Each device and each external hub registers
  `GUID_DEVINTERFACE_USB_DEVICE`, as under Microsoft's own hub driver, so
  a program that finds USB devices through it (`SetupDiGetClassDevs` with
  that interface, or `RegisterDeviceNotification` for its arrivals and
  removals) finds them. The root hub has none, as under Microsoft's stack.
  A composite device has it on its own entry (below), not on its parts, as
  under Microsoft's stack.
- **A composite device has an entry of its own** (new in `2.3.0.0`; GitHub
  issue 17). A device the driver splits into parts - a headset's audio and
  its buttons, a keyboard with media keys, a mouse with a second interface,
  and QEMU's own audio device - appears as "xHCI98 USB Composite Device"
  beneath the port it is plugged into, with its parts beneath it, so each
  port of a hub has one entry, as on Microsoft's own stacks (there the
  entry is "USB Composite Device"). USBView and the hub pages name that
  entry for the port. The driver still splits, configures and drives the
  device itself; Windows' own composite driver is not used. The entry
  installs from the driver already installed: on Windows 98 SE, 2000 and
  32-bit Vista with nothing to answer, and on Windows XP with the Found New
  Hardware wizard and the unsigned-driver warning, the parts then
  installing silently (read in QEMU with its audio device; the other
  systems are being read). Disabling it in Device Manager stops
  every part, and enabling it brings them back without the device being
  plugged again; disabling one part leaves the others working. **After an
  update from an earlier `2.y.y.y` each composite device is found once more
  as new hardware**, exactly once: its own entry is new, and on the NT
  systems a part that the device has no usable serial number for is found
  again too, under its new parent; Windows installs each from the drivers
  it used before. The update from `2.2.0.0` has not been read on any
  system yet; a setting made on a part's own entry (such as a sound
  device's default) may need to be made again.
- **USBView shows a device's strings** (new in `2.3.0.0`; GitHub issue
  17). Up to `2.2.0.0` a tool that asked the hub for a descriptor the
  driver had not kept - a string such as the manufacturer, product or
  serial number, another language, another configuration, the BOS - got
  nothing. The driver now reads it from the device, with the same length
  rules as Microsoft's hub driver, so SweetLow's USBView shows each
  device's and hub's strings. Read in QEMU on Windows 2000, XP, and Vista
  and 7 in both architectures, with a stick, a mouse, a tablet and a hub,
  with stick copies compared clean and the mouse moving while USBView
  refreshed every two seconds, and devices unplugged in the middle;
  Windows 98 SE and ME are being read. The WDK 7.1 USBView showed only the
  string numbers on 32-bit Vista, and does not run on 2000.
  A device that is suspended (`XhciSelectiveSuspend` `1`) is not asked:
  the request fails until it wakes.

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

Since `2.1.1.0` the driver can move them itself, and since `2.3.0.0` it does
so only when `XhciIntelPortSwitch` is `1` (or `2`, below): **it is off by
default.** A connector left on the USB 2.0 controller is not lost - it works
there, at USB 2.0 speed, wherever a driver for that controller is loaded
(NUSB's or SweetLow's on Windows 98 SE and ME, Windows' own on 2000 and
later) - so the driver no longer takes the connectors from it unasked
(owner, 2026-10-09; `2.1.1.0` and `2.2.0.0` installed it at `1`). Turned on,
it works in the order Linux does: each
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
- **Turning it on, or off again: `portsw.vbs`.** The download's `portsw\`
  directory, beside `release-x86\` and the others, carries `portsw.vbs`
  (in `2.3.0.0`, each flavour directory carries it instead), a Windows Script Host
  script that runs with nothing to install on every Windows this driver
  supports. Double-click it: it shows `XhciIntelPortSwitch` on every xhci98
  controller on the machine and asks for `1` (on) or `0` (off). From a
  command prompt in that directory, `cscript //nologo portsw.vbs show`, `... portsw.vbs 1`
  or `... portsw.vbs 0` does the same. Setting a value needs an
  administrator (on Vista and 7 the script asks Windows for that itself).
  **Restart Windows afterwards**: the value is read when the controller
  starts. Turn it on for a machine whose blue connectors should run at
  SuperSpeed under this driver; leave it off where the USB 2.0 controller
  should keep them. The script never sets `2` (below); that is set by hand
  ("Registry settings"). Read in virtual machines on Windows 98 SE, 2000
  and Windows 7 x64, where it showed, set and cleared the value; QEMU's
  controller is not an Intel one, so the switchover itself was not exercised
  there.
- **In one double-click: `portsw-on.bat` and `portsw-off.bat`.** Beside
  `portsw.vbs`, each shows the value, sets `1` or
  `0` through the script and says to restart Windows. On Windows 98 SE and
  ME double-click it (at a prompt, change to its directory first); on 2000 and XP run it as an administrator; on Vista
  and 7 right-click it and choose Run as administrator. **Then restart
  Windows.** Neither sets `2`. Read in virtual machines on Windows 98 SE,
  ME, 2000, XP and Windows 7 x64 (run as administrator, and not, where the
  script asks for the rights itself), each setting and clearing the value.
- **A USB 3 stick at USB 2.0 speed.** On one of these chipsets, a USB 3
  stick or drive on a switchable connector that copies only at USB 2.0
  speed is most likely still on the USB 2.0 controller, because the
  switchover is off by default. Run `portsw-on.bat` and restart Windows.
  (A USB 2.0 hub or cable in between gives the same speed, and the
  switchover does not change that.) This is what the owner's Dell
  Latitude E7250 showed on 2026-10-11 with the switchover at its default
  `0`; at `1`, after a restart, the stick linked at SuperSpeed.
- **What it logs.** With `XhciLogVerbosity` at `2` or above, `XHCISNAP`'s
  report carries `psw.` records: `psw.gate` (`1` on a listed controller),
  `psw.value` with `psw.value.found` (`psw.value.found` 0 means the value
  is absent or not a `DWORD`, which is off since `2.3.0.0`), the two masks firmware allows and what each register read
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
> driver does not list, and at your own risk. Everyone else should use 0
> or 1.**

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
system but Windows 98 SE, until the E7250 (`9CB1`) under Windows 7 x64 at
the `2.3.0.0` bench; the other four device ids, which rest on Linux's
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
  interrupt has delivered is picked up by the driver's own 100 ms timer
  once it has waited 100 ms, which in practice is about a third of a
  second after it was first seen.
- **A transfer error on a bulk or interrupt endpoint** (a USB Transaction
  Error: the device did not answer cleanly) is retried in place, up to three
  times for the same transfer, before the transfer fails as it did up to
  `2.1.1.0`. Since `2.3.0.0` that includes a UAS drive's data and status
  streams. Not on the control endpoint or isochronous (audio)
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
re-enumeration and why, the lost interrupts the driver picked up, each
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

## Windows Vista and 7: message-signalled interrupts (new in `2.3.0.0`)

On Windows Vista and 7 the controller may now use a message-signalled
interrupt (MSI or MSI-X) instead of its interrupt line, the way Windows'
own drivers for newer controllers do. A message does not travel through
the chipset's interrupt routing, so a BIOS routing fault such as the Dell
E7250's cannot lose it. Windows 98, ME, 2000 and XP (32- and 64-bit) have
no message-signalled interrupts and are unchanged.

- **64-bit Vista and 7**: the driver asks Windows for one message in the
  controller's hardware key, and connects it when Windows grants it.
- **32-bit Vista and 7**: the one 32-bit `xhci98.sys` also runs on Windows
  98, so it cannot itself use the Vista routine that connects a message.
  The 32-bit package therefore carries a small second driver for these two
  systems alone, `xhcimsi.sys`, which the INF installs as a service that
  starts with Windows and which hands `xhci98.sys` that routine. **The
  install and every update on 32-bit Vista and 7 ask you to restart**,
  because that service starts only at boot. After a first install the
  controller does not start until you restart (Device Manager shows it
  waiting for the restart: code 28 on 7, code 1 on Vista), so its USB 3
  ports work from the next start of Windows on, with the message. If
  `xhcimsi.sys` is missing or does not load, the controller keeps its
  interrupt line under `XhciInterruptMode` `0`.
- **Asking for the interrupt line** on Vista and 7: set `XhciInterruptMode`
  to `1` and disable and enable the controller, or set `MSISupported` to
  `0` under `Interrupt Management\MessageSignaledInterruptProperties` in
  the controller's hardware key (Device Parameters) and restart. Under `1`
  the driver asks Windows for the line; if Windows grants a message anyway
  (a controller that offers no line), the controller fails to start
  (Device Manager shows Code 10), on both architectures. Set `0` again.
- **Asking for a message-signalled interrupt** on Vista and 7: set
  `XhciInterruptMode` to `3` and disable and enable the controller. The
  driver asks Windows for the message alone and uses it; if Windows
  grants none (for instance with `MSISupported` `0`), or the message
  cannot be connected (on 32-bit Vista and 7, when `xhcimsi.sys` is not
  running), the controller fails to start (Code 10), with no fallback to
  the line or to polling. On Windows 98 SE, ME, 2000 and XP, which have no
  message-signalled interrupts, `3` always fails the start.
- **Going back to an earlier version** on Vista or 7: the request for a
  message stays in the hardware key, and releases before `2.3.0.0` do not
  know what to do with a message. Before installing an older package,
  uninstall the controller in Device Manager with "Delete the driver
  software for this device" ticked, or set `MSISupported` to `0` first.
  The tick removes only the package in use; older ones stay in the driver
  store ("Updating from 2.y.y.y", "Deleting the driver keeps the older
  packages"). The downgrade was read once, in a 32-bit Windows 7 virtual
  machine with an earlier test build: it ran on the line, and the
  `xhcimsi` service stayed.
- **Removing the 32-bit helper** after uninstalling the driver: it stays
  installed as a service and does nothing on its own; `sc delete xhcimsi`
  from an administrator's prompt, then a restart, removes it.

`XHCISNAP` shows which delivery the controller uses. These behaviours were
read in QEMU guests (the readings are listed in the run record), and on
real hardware on the owner's Dell Latitude E7250 under Windows 7 x64: MSI
by default, with nothing for the backstop to drain, and the line under
`XhciInterruptMode` `1`. MSI-X has not been read on real hardware.

## Registry settings

Each value is a `DWORD` in the controller's driver (software) key, and
the install writes every value the driver reads, nineteen, in this key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host
Controller". The number varies from machine to machine, and a machine whose
controller was ever enumerated at another PCI slot can have more than one
such key: the device's own `Driver` value, under `Enum\PCI`, names the one in
use. `XHCISNAP -verbosity` finds it for you. On Vista and 7 Device
Manager also shows it: the controller's Properties, Details tab, the
"Driver key" property, whose value ends in that `NNNN`. A change takes
effect when the driver next starts: restart Windows (or, for the values
that say so, disable and enable the controller in Device Manager).
`XhciForceBulkOnly` is the exception: it is read as each device is
enumerated ("XhciForceBulkOnly").

The install writes all nineteen: each at the driver's own default, except
`XhciImodInterval250ns` at `160` and `XhciForceBulkOnly` at `0`, which
the driver reads as `1` when it is absent (text-mode Setup;
"XhciForceBulkOnly"). Each is written only where it is missing,
so a value you changed survives an install or update; to go back to the
default, set it by hand, or delete the value and update the driver.
`XhciTolerance`, `XhciIntervalCap` and `XhciAvgTrbEsit` are new in
`2.2.0.0`, and an update over `2.1.1.0` adds them; `XhciIntelIrqRoute`,
`XhciMaxTransferKB`, `XhciInterruptMode`, `XhciPollIdleMs` and
`XhciPollActiveMs` are new in `2.3.0.0`, and an update over `2.2.0.0` adds
them. `XhciWideMap` (`0`), `XhciSelectiveSuspend` (`0`) and
`XhciUsb2HubSpeedFallback` (`1`) are read by `2.3.0.0` and written by its
install too, at the value that reads as a missing one; an update keeps any
of them you had already added. Since `2.3.0.0` the
install writes `XhciIntelPortSwitch` at `0`, where `2.1.1.0` and `2.2.0.0`
wrote `1`; an update keeps the `1` they wrote, and `portsw.vbs` changes it
("XhciIntelPortSwitch", below). The install has written
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

### XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs: how the controller's events arrive

New in `2.3.0.0`; the install writes `0`, `50` and `1`. Read at each start
of the controller: disable and enable it in Device Manager, or restart
Windows. Up to `2.2.0.0` the driver took only the controller's interrupt
line and refused to start a controller without one. Since `2.3.0.0` it
can also read the controller's events on a timer of its own (polling),
and on Windows Vista and 7 take a message-signalled interrupt ("Windows
Vista and 7: message-signalled interrupts").

| `XhciInterruptMode` | Effect |
|---|---|
| `0`, absent, not a `DWORD`, or `4` and above | Automatic, the default. The interrupt line where it works. A controller with no interrupt pin, or given no interrupt by Windows, is polled. A line the driver sees the controller raise and that never reaches it is detected after the start, and the controller moved to polling by the driver itself; an interrupt that fails in a way the driver cannot see (no sign of it in the controller's registers) is not, and needs `2`. On Vista and 7, a message-signalled interrupt where Windows grants one |
| `1` | The interrupt line only, as `2.2.0.0`, on every system. On Vista and 7 the driver asks Windows for the line rather than a message. A controller given no line - no interrupt, no interrupt pin, or on Vista and 7 a message Windows grants anyway - fails to start (Device Manager: Code 10) |
| `2` | Polling always, even where the line works. For a controller whose interrupt misbehaves; it costs more processor wake-ups |
| `3` | A message-signalled interrupt (MSI or MSI-X) only. On Vista and 7 the driver asks Windows for the message alone; if none is granted, or it cannot be connected (on 32-bit Vista and 7, when `xhcimsi.sys` is not running), the controller fails to start (Code 10), with no fallback to the line or to polling. On Windows 98 SE, ME, 2000 and XP (32- and 64-bit), which have no message-signalled interrupts, `3` fails the start |

| Value | Default | Effect |
|---|---|---|
| `XhciPollIdleMs` | `50` | While polling, how often (in ms) the driver looks when nothing is outstanding. `0` runs as `50`; above `5000` is held to `5000`. A device plugged in waits at most this long to be seen |
| `XhciPollActiveMs` | `1` | While polling, how often (in ms) it looks while transfers are outstanding. `0` runs as `1`; a value above the idle period is held to it. When the only transfers outstanding are periodic ones (a mouse's or keyboard's interrupt endpoint), the driver looks at that endpoint's interval instead, held between the active and the idle period: 8 ms with QEMU's mouse, on every system. Windows runs the timer no faster than its clock tick, so `1` means "as often as Windows allows"; 1 ms was read with transfers outstanding on a busy XP, Vista and 7 in QEMU |

A start that `1` or `3` refuses records why: with `XhciLogDebugView` set,
DebugView shows the counter dump's `intr.reason` line (14: a message
granted under `1`; 16: no message granted under `3`; 17: the message not
connected; 18: a system with no message-signalled interrupts) beside the
Code 10, and with `XhciLogVerbosity` `2` or above also the start's notes
and a `start.refused.delivery` line.

Neither period matters while the controller has a working interrupt line
or a message. Polling adds up to one period of delay to each completed
transfer. Read in QEMU on every system, modes `0`, `1` and `2`: a file
copy from a stick and the guest's idle processor load showed no step
change between the line and polling; audio played without a gap under
polling on 2000, XP x64 and 7 in both architectures, and with two gaps of
about 20 ms on Vista x64. The move to polling when a line never arrives
was read in QEMU on every system, and on real hardware on the Dell
Latitude E7250 under XP x64 with `XhciIntelIrqRoute` `0`, where the line
is dead: the driver moved to polling and a SuperSpeed stick copied clean.
There Windows ran the poll every 15 to 62 ms, its timer's granularity on
that machine, not every 1 ms. Polling by choice (`2`) was read on a
ThinkPad E460 under Windows 98 SE: about 4 GB through a SuperSpeed stick,
compared clean.
`XHCISNAP`'s report says
which delivery the controller uses, and why. `XHCIQUAL` passes a
controller with no working interrupt as `QUALIFIED WITH CAVEATS` ("Before
you install: check the machine").

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
transports use Bulk-Only (Windows' own `usbstor.sys`) instead of UAS. `0`,
which the install writes (since `2.1.1.0`), means UAS wherever the device
offers it. Absent reads as `1` since roadmap task 37.2 (`0` before): that is
how text-mode Setup sees it, before any INF has run, and there Setup's own
`usbstor` is the only storage driver, so a stick or disk that offers both
is usable there (issue 15).

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
| `4` | 31 Hz | 62 Hz (the codes below instead) |

With `2` or `3`:

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

**`4`: the rate the client chooses** (new in `2.3.0.0`; GitHub issue 17).
An interrupt endpoint whose polling interval in the configuration Windows
selects is 128, 129 or 130 is polled at 2000, 4000 or 8000 Hz; every other
interval is polled as the specification reads it, so value `4` does not do
`2`'s and `3`'s speeding up of 16 to 63 ms, and `2` and `3` do not honour
the codes. It applies to a Low- or Full-Speed device on a root port only,
and to a High-Speed or SuperSpeed device at a root port or behind a hub.
The procedure is the value first, then the client: set `XhciFastPollFsLs`
to `4` and restart, then set hidusbf (or another filter) to write 128, 129
or 130. Two cautions:

- 128 to 255 ms are legal polling intervals for a Low- or Full-Speed
  device, so a device that itself asks for 128, 129 or 130 ms is polled at
  2000 to 8000 Hz while the value is `4`.
- A filter that writes a code while the value is not `4` gets what the
  number means without it: a Low- or Full-Speed device is polled every
  128 ms, and a High-Speed or SuperSpeed one, for which the number is out
  of range, every 4.096 s; on a controller where `XhciIntervalCap` applies,
  either is polled every 32 ms instead.

A controller that refuses a code is caught as for `2` and `3` and counted
in `fastpoll.fallbacks`, and the device is then polled every 1 ms
(1000 Hz), not at the code's ordinary reading. A controller that accepts
it and misbehaves cannot be caught; delete the value and restart. At High
Speed and SuperSpeed the programmed polling interval is inside the
specification's range; at Low and Full Speed it is outside it, as `2`'s
and `3`'s are. On an AMD controller
the code is applied before `XhciIntervalCap`, which leaves it alone. A
part of a composite device keeps the code only until its class driver
issues a later select of its interface (`SELECT_INTERFACE`), even of the
same alternate setting, as with `2` and `3`; HID class drivers do not.

The value is read when the controller starts, so a change takes effect
after a restart.

**Untested ground on real controllers**: it has been read on none. In
QEMU (since `2.3.0.0`, on 32-bit Windows 7 and Windows 98 SE) value `4`
programmed the intervals the codes ask for, `2` and `3` ignored the
codes, a mouse behind a hub kept its rate and the mouse moved
throughout; that shows what the driver asks of the controller, not how
a real controller and device behave at those rates. The bench reading on
the ThinkPad E460 and P14s Gen 1 was not taken for this release.

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

New in `2.1.1.0`; since `2.3.0.0` the install writes `0` (`2.1.1.0` and
`2.2.0.0` wrote `1`, and an update keeps a value already there). It matters
only on the Intel chipsets listed in "Intel 7-, 8- and 9-series chipsets:
the port switchover", unless it is `2`. Since `2.2.0.0` it is read on every
Intel controller, and still on no other vendor's. `portsw.vbs`, in the
download's `portsw\` directory (in `2.3.0.0`, each flavour directory), shows it and sets it to `0` or `1` on every xhci98
controller ("Turning it on, or off again" in that section).

| Value | Effect |
|---|---|
| `0`, absent, or not a `DWORD` | Off, the default since `2.3.0.0`: the driver leaves the connectors where they are, as every release up to `2.1.0.0` did. Up to `2.2.0.0` absent or not a `DWORD` was on |
| `1`, or any other number but `2` | On, on a listed controller: the driver moves the connectors to itself at each start and hands them back at each stop |
| `2` (since `2.2.0.0`) | On, on every Intel controller, listed or not. **At your own risk**: on an Intel controller without the switch registers - every Intel chipset from the 100-series (Skylake) on, and any whose layout has not been read - it writes registers of unknown meaning at every start, resume, stop and shutdown, and the result is unknown. Use it only for an Intel chipset with both an EHCI and an xHCI controller that the list misses; everyone else should use `0` or `1`. See "Value 2" in that section |

Only a `DWORD` that is not `0` turns it on, and only an exact `2` goes past
the list. It is read
when the controller starts, so a change takes effect at the next restart or
at a disable and enable of the controller in Device Manager. Setting `0`
does not give the connectors back by itself: after a restart they are where
firmware puts them, and after a disable and enable they stay where the
disable left them, with the USB 2.0 controller. Read on a Lenovo B490 under
Windows 98 SE: at `0` and after a restart, a device on a blue connector
stayed with the USB 2.0 controller.

### XhciIntelIrqRoute: the Intel interrupt routing repair

New in `2.3.0.0`; the install writes `1`. On the Intel 7-, 8- and 9-series
and C610/X99 chipsets, two chipset registers decide which interrupt line the
xHCI controller's interrupt reaches. Some firmware leaves them pointing at a
different line from the one it tells Windows: the Dell Latitude E7250's
sends the interrupt to IRQ 16 while Windows listens on IRQ 21, so the
driver never hears from the controller and every device on it is slow or
stops (docs/issues/12). At each start and each return from standby the
driver reads those registers and, where the value allows it, points the
controller's interrupt at the line Windows assigned. It changes nothing else.

| Value | Effect |
|---|---|
| `0` | Off: the driver reads and reports, and writes nothing |
| `1`, absent, any other number, or not a `DWORD` | On the Dell Latitude E7250 only (controller `8086:9CB1`, subsystem `1028:062D`, chipset `9CC3`); elsewhere it reads and reports |
| `2` | On any of the six reviewed controller and chipset pairs: controller `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` or `8D31` with one of its own chipset ids (docs/issues/12 section 5.1) |

**It does nothing on every other machine**, at any value: another vendor's
controller, an Intel controller not in that list (every chipset from the
100-series on included), a chipset id that does not match its controller,
or a check of an id, a register or the interrupt Windows gave that
fails. **It also does nothing
when the controller's interrupt is a message (MSI) rather than a line**, as
it can be on Windows Vista and 7, **and nothing when the driver is polling
the controller instead of taking its interrupt**: neither goes through the
line it would repair. On Windows 2000, XP, Vista and 7 it also writes
nothing when Windows gave the controller an interrupt below 16, since
which route an I/O APIC system then uses is not proven; it reports what it
would have done. It decides again at each start and return from standby,
so a change takes effect at the next restart or at a disable and enable of
the controller.

What it logs. `XHCISNAP`'s report has a section "interrupt routing" with
the value in effect, how many times it ran and wrote, and the last run's
decision, what it read and, after a write, what the register read back;
with `XhciLogVerbosity` at `2` or above the report also carries `pirq.`
records. Send them with any report from one of these machines.

What it rests on. On the owner's E7250 under Windows XP x64, the same
register written by hand to the value this setting writes fixed the
mouse at once and let a USB stick work. The driver's own write was then
read there at the `2.3.0.0` bench: under XP x64 at the default, and under
Windows 7 x64 with the line asked for (`XhciInterruptMode` `1`, also with
the EHCI disabled), it moved INTA from PIRQA to PIRQF (`D20IR` `3210` to
`3215`, IRQ 21) and read it back; under Windows 7's default MSI it wrote
nothing, as designed. No other machine has been read.

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
and does not change which mouse `XhciFastPollFsLs` speeds up; under
`XhciFastPollFsLs` `4` a code is applied first, and the cap leaves it
alone. It is read when the controller starts.

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

### XhciMaxTransferKB: the UAS command size

New in `2.3.0.0`; the install writes `1024`. In **KiB**: the most one UAS
command asks the drive to move, which the UAS driver tells Windows' storage
class driver as the largest request it takes. `2.2.0.0` and earlier always
said 64 KiB. Fewer, larger commands mean fewer command and status
exchanges per megabyte; the controller still moves each command in pieces
of at most 128 KiB, one after another.

| Value | Effect |
|---|---|
| `1024`, absent, not a `DWORD`, outside `64`-`2048`, or not a multiple of `4` | 1 MiB commands (`XHCISNAP`'s report notes why a value was not used) |
| `64` | 64 KiB commands, as `2.2.0.0` and earlier |
| `2048` | 2 MiB commands, the largest |
| any multiple of `4` from `64` to `2048` | that size |

It applies to every UAS device on that controller; Bulk-Only devices keep
Windows' own `usbstor.sys` limits whatever it says. Windows reads the size
when the storage device starts and keeps it, so **a change takes effect at
the next restart of Windows**. `XHCISNAP`'s report shows the value read and
the command size in effect, and each UAS device's hardware key records the
size it took (`XhciUasCommandBytes`, in bytes).

**Trying `2048` by hand.** Set `XhciMaxTransferKB` to `2048` (decimal; `800`
hex) in the driver key above, with `regedit` or a `.reg` file, and restart
Windows. To go back, set `1024`, or `64` for a drive that misbehaves at
large commands, and restart again. Linux's UAS driver sends commands of up
to 512 KiB, and its Bulk-Only driver 120 KiB, or 1 MiB at SuperSpeed, with
64 KiB or 120 KiB for drives known to misbehave; Windows 7's Bulk-Only
driver sends 64 KiB, raised per device up to 2 MiB by Microsoft's KB 2581464.
So `1024` is within what drives already see elsewhere, and `2048` beyond
most of it. A drive that fails at a larger size usually fails at its first
large read or write: if copies fail or the drive drops off after raising
the value, set `64`.

**A later release with another default.** The install writes the value
only where it is missing, like every value here, so an update keeps the
number already in the key - the old default included. To take a new
release's default, delete `XhciMaxTransferKB` before updating the driver,
or set the new number by hand, and restart.

**One model only: Microsoft's per-device value.** On Windows 2000 and
later the UAS driver also honours the value Windows' own Bulk-Only driver
uses for one kind of device, so a drive model can be set apart from the
rest:

| | |
|---|---|
| Key | `HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP`, where `VVVV` and `PPPP` are the drive's vendor and product IDs as four uppercase hex digits each (`VID_0BDA&PID_9210` is `0BDA9210`) |
| Value | `MaximumTransferLength`, a `DWORD`, in **bytes** |
| Accepted | `65535` to `2097152`: the range Microsoft's KB 2581464 gives, `65535` to `2097120`, with `2097152` - what tools write for 2 MiB - taken as 2 MiB as well |

A value in range replaces `XhciMaxTransferKB`, larger or smaller, for
every drive with those vendor and product IDs - the key is per model, for
the whole machine, not per physical drive - and every other drive keeps
the controller's value. It is rounded to the nearest 4 KiB (at most 2048
bytes either way), so `65535` is 64 KiB, `2097120` and `2097152` are both 2 MiB, and `131072`
(`20000` hex) is 128 KiB exactly. A value that is missing, not a `DWORD` or
out of range is ignored. Restart Windows after setting it. It is the same
key Windows' `usbstor.sys` reads for a Bulk-Only drive, so tools and
guides written for that - Uwe Sieber's pages on `uwe-sieber.de` cover it -
set it for a UAS drive too, 2 MiB included. The UAS device's hardware key (under
`Enum\USB`, on Windows 2000 and later in its `Device Parameters`) shows
what was taken: `XhciUasCommandBytes` the size in bytes,
`XhciUasCommandSource` `1` for the controller's value and `5` for the
drive's own, `XhciUasDeviceLimit` the value read and
`XhciUasDeviceLimitWhy` `0` absent, `1` used, `2` not a `DWORD`, `3` out of
range. Behind a controller driver that does not answer the transfer-size
query (`2.2.0.0` and earlier) the UAS driver keeps 64 KiB and the drive's
value is not used (`XhciUasDeviceLimitWhy` `4`).

**Windows 98 SE and ME.** Both values are read, and neither changes what
reaches the drive: Windows' mapping layer, `USBNTMAP.SYS`, sends at most
64 KiB at a time whatever the UAS driver offers. Read in QEMU virtual
machines on development host A, Windows 98 SE with NUSB and Windows ME,
with `XhciMaxTransferKB` at `1024`: no command above 64 KiB, one at a
time, and file reads no faster than at `64`.

### XhciWideMap: two transfer pieces at once

On Windows 2000 and later the controller driver gives a large bulk
transfer to the controller two pieces at a time, the next one queued
before the current one ends, where `2.2.0.0` and earlier gave one piece and waited -
when this value turns it on. It is **off unless you set it to `1`**. It applies to
large bulk writes on any device, and to large bulk reads only on a UAS
drive's streams. It does so only where the machine's DMA needs no
translation - the check is made at every start, and again for each
transfer:

| | |
|---|---|
| Key | the controller's driver key, where the install writes the other `Xhci` values ("Registry settings", above): on 2000, XP, Vista and 7 `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN`, the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host Controller" (on Vista and 7 Device Manager's Details tab gives it as the "Driver key") |
| Value | `XhciWideMap`, a `DWORD`; change it with `regedit` (Modify) or a `.reg` file |
| Default | off: the install writes `0`, and a missing value means off too |
| On | `1`, then **restart Windows**; `0` and a restart turn it off again |

The decision rests on a reading of every Windows 2000, XP, Vista and 7 HAL
of the service packs these notes name, x86 and x64. Later HAL hotfixes were
not read; the checks made at each start and each transfer stand for them.
Under Driver Verifier's DMA checks, and on a machine whose DMA does need
translation (Windows 2000 running its PAE kernel; 32-bit Vista and 7, and
every x64 system, where memory may lie above 4 GB), nothing changes: every transfer goes one piece at a time, as in `2.2.0.0`.
Windows 98 SE and ME never send a transfer large enough for it to matter.
On a UAS drive at SuperSpeed the same switch also lets one command's
data go to the controller as a single piece of up to 2 MiB, on rings the
driver makes larger for that drive's streams (about 1.5 MiB of memory per
drive while it is attached; if that memory cannot be had the drive works
as before). It is off by default because a regression, though unlikely,
would show as silently wrong data rather than an error. If you turn it on
and a large copy misbehaves, set `0`, restart and report whether that
changed it.

### XhciSelectiveSuspend: putting idle devices to sleep

New in `2.3.0.0`, and **off unless you set it to `1`**. Up to `2.2.0.0` the
driver never suspended a device: it answered Windows' power requests for
a device by recording them, so an idle device kept drawing power. With
this value at `1` the driver suspends a device when Windows asks it to -
when a device's driver puts it in a low-power state while the machine
runs, or, on XP and later, when a driver uses Windows' idle notification -
and wakes it when Windows asks again or the device signals a remote wake.
It never suspends a device on a timer of its own; whether and when a
device is put to sleep is the choice of that device's driver and of
Windows.

| | |
|---|---|
| Key | the controller's driver key ("Registry settings", above) |
| Value | `XhciSelectiveSuspend`, a `DWORD`; the install writes `0` |
| `0`, absent, any other number or not a `DWORD` | Off, the default: the driver behaves as `2.2.0.0` did, bit for bit |
| `1` | On, for USB 2.0 devices (High, Full and Low Speed, at a root port or behind a hub) and SuperSpeed devices alike |

Read at each start of the controller: restart Windows after changing it.
Hubs, the root hub, a composite device's own entry and the controller are
never suspended, so the hubs above a sleeping device stay awake; a
composite device's parts are suspended together, when all of them are
idle (their own entry stays in D0 in Device Manager meanwhile). A device
that cannot signal a remote wake is offered to Windows as able to sleep
but not to wake.

Read in QEMU virtual machines with the project's own test driver bound to
a device, on Windows 98 SE, ME, 2000, XP and 32-bit Windows 7 (there is no
64-bit test driver; the run record has the legs), and with the value at `0`
reading as the build before it on every system. Windows' own drivers' idle was not
read: Windows' HID driver never idles QEMU's mouse. No real machine has
been read with it on, and no real remote-wake device ("Untested ground").
So leave it off unless you are testing it.
If a device misbehaves
with it on, set `0`, restart and report whether that changed it.

### XhciUsb2HubSpeedFallback: Low- and Full-Speed IDs behind a hub

New in `2.3.0.0`, and **on unless you set it to `0`**. A device behind a hub
has no port of the controller's own to report its speed, so the driver
writes a speed ID for it, looked up in the controller's speed table for the
USB 2.0 port the hub hangs from. Some controllers - the VIA VL805
(`1106:3483`) is one - list only High Speed there, the link to their own
built-in hub, and `2.2.0.0` refused every Low- or Full-Speed device behind
that hub. With this value on, such a device gets the standard ID (1 for
Full Speed, 2 for Low Speed) where the table names neither that speed nor
that ID; a table that names them is followed as before, and devices on the
controller's own ports are not affected.

| | |
|---|---|
| Key | the controller's driver key ("Registry settings", above) |
| Value | `XhciUsb2HubSpeedFallback`, a `DWORD`; the install writes `1` |
| Absent, `1`, any other number or not a `DWORD` | On, the default |
| `0` | Off: such a device is refused, as in `2.2.0.0` |

Read at each start of the controller: restart Windows after changing it.
Built without the hardware and never run on a VL805; in QEMU virtual
machines, whose controller lists no speed table, devices behind a hub read
as before. `XHCISNAP`'s report says whether it is on, how many devices it
gave an ID and which slots hold one, and on the VL805 the card's firmware
version (`pci.vl805.fw`). If a Low- or Full-Speed device behind such a hub
misbehaves with it on, set `0`, restart and report whether that changed it.

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

### A device's speed on the hub pages (new in `2.3.0.0`)

On Windows 98 SE and ME the newer form of the connection information
(`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`, which tools such as
SweetLow's USBView ask for) reads 3 for a SuperSpeed device and 4 for a
SuperSpeedPlus one, as Intel's USB 3 drivers for those systems report it;
up to `2.2.0.0` both read High Speed. Windows' own pages on those systems
send the older form, which says only whether a device is Low Speed, and
are unchanged. On the NT systems the newer form still reads High Speed,
because Vista's and 7's hub page would list any other value among the
Full-Speed devices. Every system also answers the request Windows 8
introduced (`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`), which tools
use to tell SuperSpeed and SuperSpeedPlus apart: it gives the port's
protocols and whether the device runs at, and can run at, SuperSpeed or
SuperSpeedPlus. A USB 3 device running at High Speed is not reported as
SuperSpeed-capable, since the driver does not read that part of its
descriptors at High Speed.

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

From `2.3.0.0` the top of `MYDUMP.TXT` also says which system it came from:
the Windows version, build and service pack, 32-bit or 64-bit, the
processor, the memory, and the make, model and BIOS where that Windows
records them (on Vista and 7 the make and model; on 2000 and XP, 98 SE and
ME the BIOS only). It does not print the computer's name, your user name or
the domain, and does not read any serial number; it reads those names only
to withhold a firmware string that contains one (a name of three characters
or more). The file is meant to be pasted into a public issue as it is, but
if your machine's firmware strings were customised, read the top of the
file first. If reading those details causes a problem on your machine, add
`-nosysinfo` to step 4 (`XHCISNAP -nosysinfo -o C:\MYDUMP`): the report
then says they were skipped and is otherwise the same.

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

- **Selective suspend is off unless you turn it on, and hubs are never
  suspended.** With `XhciSelectiveSuspend` at its default `0` no device is
  suspended to save power, as up to `2.2.0.0`. At `1` a device is
  suspended when Windows asks, but a hub, the root hub and the controller
  never are, and the driver never suspends anything on a timer of its own.
  See "XhciSelectiveSuspend: putting idle devices to sleep".
- **No USB storage on a stock Windows 98 SE.** Storage needs NUSB's
  mass-storage component; see "Windows 98 SE and ME: the stock install, and
  storage".
- **Windows Vista x64 and Windows 7 x64 need test-signing mode, or driver
  signature enforcement disabled.** The x64 drivers carry the project's own
  test signature, not Microsoft's. Turn on test-signing mode once (see
  "What this is"), or disable driver signature enforcement at every start
  (F8); otherwise the controller sits at Code 39 on Vista, Code 52 on 7, and
  nothing on it works.
- **Windows 98 SE: a USB audio device can wedge the machine as it loads,
  and it is not this driver's doing.** After a cold boot, attaching a USB
  audio device wedges the guest often, with the taskbar clock stopped and
  "Msgsrv32" not responding. Every request the driver was given had been
  answered: the wait is above it, in Windows 98's configuration manager.
  Windows 98 SE's own USB stack on a UHCI controller, with this driver not
  loaded, wedges the same way at about the same rate (14 of 30 tries,
  against 23 of 39 under this driver, attaching 40, 120 and 300 seconds
  after the boot, with selective suspend on or off), and `1.2.0.0` and
  `2.2.0.0` showed it too. Waiting after the boot did not avoid it in
  these readings, though an earlier reading had suggested two minutes
  would. Only a restart ends it.
  Likewise, USBView walked and closed while a USB audio device was
  attached froze the machine, or stopped it with a fatal exception in
  `USBAUDIO`, about half the time, on Windows' own stack as well; with no
  audio device, or no USBView, it never did.
- **Windows 98 SE in a virtual machine cannot play USB audio** through
  Windows 98's own `USBAUDIO.VXD`; that is QEMU's emulated device and the
  VxD, through this driver and through a UHCI controller alike. On real
  hardware it plays: audio was played and heard at a root port and behind a
  hub.
- **Windows 98 SE: a device at a location the system has not seen before
  raises the Add New Hardware Wizard, and holds that port until it is
  answered.** While the device stays plugged in, that hold is per port and
  by design of Windows 98. **Finish the
  wizard before unplugging the device.** A device unplugged while its
  wizard is open holds more than its port: Windows asks the bus about no
  port again until the wizard is closed, so neither that device plugged
  back in nor a device plugged in, at any port, where another was
  unplugged meanwhile is seen until then; closing the wizard brings them
  all at once. Keys typed into the abandoned wizard wedged the machine.
  The same happens on Windows 98 SE's own USB stack and under `2.2.0.0`.
- **Windows 98 SE: Stand by is untested on real hardware; remove USB disks
  before Stand by.** It has been read only in QEMU virtual machines, where
  Windows 98 SE itself does not resume reliably: after the wake, a program
  started, or Ctrl+Alt+Del, can stop with a fatal exception 0E in
  `KERNEL32` even with no USB controller in the machine. With a USB disk
  that was attached when the computer started, the resume stops before the
  desktop returns, under this driver and under NUSB's USB 2.0 (EHCI) driver
  alike. The BIOS presents such a disk to Windows, and Windows 98 SE then
  uses it in MS-DOS compatibility mode (System Properties, Performance
  tab). A disk plugged in after Windows started did not stop the resume,
  and it was still attached afterwards. Windows 98 SE's own UHCI driver did
  not resume at all there. With only a USB mouse attached, at a root port
  or behind a hub, the resume completed and the mouse worked through this
  driver.
- **Windows 98 shows no driver version on the Driver tab**, only the file
  date; the four-part version is under Driver File Details, which also lists
  `xhci98.tmp`, a leftover of the install's temporary copy (cosmetic).
- **Windows ME: do not unplug a device while Windows is installing it.**
  ME's own device manager stops responding; it does the same on Microsoft's
  own USB stack.
- **Windows ME: Shut Down can hang with a USB disk that has not been opened
  since the start.** Open the disk once (a `dir` of it is enough), or
  remove it, before shutting down. ME does the same on Microsoft's own USB
  stack, and `2.2.0.0` did too.
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
- **Intel 7-, 8- and 9-series chipsets, with the switchover turned on
  (`XhciIntelPortSwitch` `1` or `2`; off by default since `2.3.0.0`): a device on a blue connector moves
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
  controller. A later release is planned to hand back what is kept at the
  stop, once the driver can prove the controller has left the bus or has
  stopped writing to memory.
- **Windows 2000: after the driver recovers the controller, or after a
  resume from hibernation, a USB drive whose volume was mounted may come
  back at Code 31** (same section). The
  mouse and other devices come back; the drive stays at Code 31 until it
  is unplugged and plugged in again, or the controller is disabled and
  enabled in Device Manager. Seen in a virtual machine (five times of five
  with the volume mounted); Windows 98 SE did not show it. The recovery
  path is the one `2.1.1.0` has, but `2.2.0.0` recovers the controller on
  more faults. Seen again in `2.3.0.0`'s virtual-machine tests on Windows
  2000: after a recovery with the controller polled (`XhciInterruptMode`
  `2`), and after a resume from hibernation. A controller that lost its
  state across the hibernation, as QEMU's does, is started afresh, and the
  driver then finds every device again as on a recovery. Each time, a
  restart of the drive's own device cleared it (read with a command-line
  device restart, not in Device Manager).
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
- **After an update, the root hub's Driver tab still shows the old
  version** until the root hub is updated too, although it runs the new
  file (read again after an update from `2.2.0.0` on Windows 2000, XP and
  XP x64). On Windows 98 SE and ME the old driver also keeps running until
  the next restart, which they do not ask for (read again after an update
  from `2.2.0.0`). Same section.
- **Windows Vista and 7: deleting the driver keeps the older packages.**
  "Delete the driver software for this device" removes only the package in
  use, and Windows may install an older one the next time it finds the
  controller; `pnputil -d` removes them. Same section.
- **32-bit Windows Vista and 7: the install asks for a restart**, and the
  controller does not start until it is done, because the helper
  `xhcimsi.sys` starts only at boot ("Windows Vista and 7:
  message-signalled interrupts").

`2.0.0.0`'s list also had "a device moved to a different port is found
again as new hardware", which `2.1.0.0` answers for every device with a
usable serial number, and Windows ME's controller re-enable hang, which
`2.1.0.0` fixes ("Targets and their standing").

The `1.2.0.0` limitations, and what each is under `2.0.0.0`, are in the
next section.

## The 1.2.0.0 known limitations, under 2.0.0.0

`1.2.0.0`'s own notes (linked at the top) list its limitations. Each was
re-measured, or answered by the design, under `2.0.0.0`. No change since
`2.0.0.0` answers a row differently, except the standby row, which says
what `2.3.0.0` adds:

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
| On a controller without Force Save Context, a wake from standby rebuilt the bus | Under `2.0.0.0`: untested, the power handlers had not run. Since `2.3.0.0` (read in QEMU, which has no Force Save Context): hibernate and resume on XP, Vista and 7, and standby and hibernate on ME, idle, rebuild the bus through the controller's reinitialization and every device is found again; with a copy running on a stick on XP, Vista and 7, the copy's requests fail for about 10 s while the stick is found again, then work. Open: Windows ME hangs on a standby or hibernate with a copy running on an xHCI stick. Windows 98 SE's standby resumed with a USB mouse at a root port or behind a hub, and with a USB disk plugged in after Windows started, and stopped with a USB disk attached when the computer started (the BIOS presents it, and Windows uses it in MS-DOS compatibility mode), as it does under NUSB's EHCI driver; QEMU's Windows 98 SE does not resume reliably with no USB controller either (the 98 SE Stand by entry in "Known limitations"). Standby on the other systems, and with devices suspended, is not yet read |
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
| Low- and Full-Speed devices behind a built-in hub whose USB 2.0 speed table lists only High Speed (`XhciUsb2HubSpeedFallback`; the VIA VL805, GitHub issue 18) | Built from the specification, the reporter's logs and other systems' practice against host tests; never run on a VL805 or on any controller with such a table. QEMU's controller lists no table, so its legs read only that nothing else changed |
| A UAS-only device at SuperSpeed on a controller that cannot stream | Built from the specification against host tests; no such controller held |
| Low- and Full-Speed polling above 1000 Hz (`XhciFastPollFsLs`) | Outside the xHCI specification, built against host tests; read on no real controller. Value `4`'s programming was read in QEMU on 32-bit Windows 7 and Windows 98 SE ("XhciFastPollFsLs") |
| The Intel port switchover beyond two machines | Read on one `1E31` machine under Windows 98 SE and on one `9CB1` machine, the Dell Latitude E7250, under Windows 7 x64 ("Intel 7-, 8- and 9-series chipsets: the port switchover"). Device ids `8C31`, `9C31`, `8CB1` and `8D31`, every other system, and a standby and resume rest on Linux's handling of the same chipsets and on host tests of the sequence |
| `XhciIntelPortSwitch` `2` on an unlisted Intel controller | Built and host-tested; read on no unlisted Intel controller. At your own risk: on one without the switch registers it writes registers of unknown meaning ("Value 2") |
| The controller-fault handling on real hardware | Each fault was made on purpose at a root port in QEMU virtual machines, under Windows 98 SE and Windows 2000; QEMU has no SuperSpeed hub, so nothing was read behind one. No real controller has raised one under this driver yet, so none of the behaviours has been seen to act on real hardware ("Controller faults: what the driver does about them") |
| `XhciIntervalCap` on an AMD controller, and `XhciAvgTrbEsit` at `1` | Built against host tests. In virtual machines, on QEMU's controller, which is not AMD: `XhciIntervalCap` `2` capped the 255 ms status endpoint of QEMU's hub, and `XhciAvgTrbEsit` `1` left the mouse, the hub and a stick working, on Windows 98 SE; `XhciIntervalCap` `1` did not apply, on Windows 2000. Neither has been read on an AMD controller or on any real controller |
| Standby (S3) on real hardware | Read only in virtual machines. At the `2.3.0.0` bench the ThinkPad E460's Windows 98 SE offered no Stand by, the Dell Latitude E7250's Windows 7 x64 had no graphics driver, and the ThinkPad P14s Gen 1 was not read |
| Selective suspend and remote wake on real hardware | Read only in QEMU, which makes no SuperSpeed function wake and has no SuperSpeed hub. No real remote-wake device has been read: a USB 2.0 HID device waking a sleeping machine's bus, or a SuperSpeed device waking from a root port or from behind a USB 3 hub |
| MSI-X on real hardware | Read only on QEMU's controller, whose MSI-X loses an interrupt now and then; since `2.3.1.0` the driver looks for such an event when it re-arms the interrupt and serves it in one more pass, where `2.3.0.0` waited for its 100 ms check, which still covers one left during that pass (`XHCISNAP`'s BackstopDrains). On a real MSI-X controller BackstopDrains should stay near 0; not read. MSI was read on the Dell Latitude E7250 |
| `XHCIQUAL` on a line that is dead under DOS | Read only in QEMU. The Dell Latitude E7250's line is dead only under Windows' interrupt routing; under DOS it works, so its interrupt test passes there |
| Windows 98 SE on the Dell Latitude E7250 | The interrupt routing repair under Windows 98 SE (PIC mode) has run on no machine: the E7250, the one machine read with that fault, runs XP x64 and 7 x64 |
| `XhciWideMap` on real hardware | Not read on an updated 32-bit Windows 7, nor at `1` with a real UAS drive. In QEMU the UAS device holds one piece per stream, so a UAS drive at `1` could not be read there either |
| A real UAS drive after a transfer error or a stop | The retry in place on a UAS stream was read only with the error made on purpose in QEMU. No real drive has met one under this driver |
| UAS speed at 1 MiB on real hardware | The 1 MiB default rests on readings in QEMU. ATTO with the MSSU10, compared with `2.2.0.0` on the same machine, was not read at the `2.3.0.0` bench; the E460's copies at about 200 MB/s are the only real-hardware figure |
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
