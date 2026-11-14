# xhci98 - Release Notes

This file describes package version `3.5.0.0`, dated 2026-11-14 (`DriverVer`
`11/14/2026,3.5.0.0` in every INF of the package, Stor98's included).
"What's new" lists what the newest releases change; older releases are in
`releases/history.md`. Where this file and `docs/contributing/roadmap-hcd.md`,
`docs/contributing/build-and-test.md` or `xhciqual/README.md` disagree, the
other document wins and this one is the copy to fix.

`1.2.0.0`, the last release of the USB 2.0-only miniport, has its own notes:
[docs/using/release-notes.md on the `1.2.0.0` branch](https://github.com/yeokm1/xhci98/blob/1.2.0.0/docs/using/release-notes.md),
and in plain text in that release's
[readme.txt](../../releases/1.2.0.0/readme.txt). Nothing below describes it.

**Contents:** [What this is](#what-this-is) -
[What's new](#whats-new) -
[Before you install](#before-you-install) -
[Installing](#installing) -
[Using it](#using-it) -
[Thunderbolt 3 and USB4](#thunderbolt-3-and-usb4) -
[Settings](#settings) -
[Reporting a problem](#reporting-a-problem) -
[Known limitations](#known-limitations) -
[Licensing](#licensing) -
[Trademarks](#trademarks)

---

## What this is

The package drives a computer's USB 3 (xHCI) controllers on Windows that
have no driver for them, and its Thunderbolt 3 and USB4 controllers:

| Driver | What it does | Windows |
|---|---|---|
| `xhci98.sys` | The USB 3 controller, its root hub, every hub and the splitting of composite devices: a whole USB stack below Windows' own class drivers (HID, `usbstor.sys`, USB audio, vendor drivers) | 98 SE, ME, 2000 SP4, XP, XP x64, Vista and 7 (32- and 64-bit); on 8, 8.1 and 10 only if you tick it |
| `xhciuas.sys` | UAS (USB Attached SCSI) disks | the same; not on 8 to 10, which have their own |
| `xhcicm.sys` | The Thunderbolt 3 / USB4 connection manager: it brings up the PCI Express tunnel to a dock, an enclosure or an NVMe case | 98 SE to 10 (on 8 to 10 the default install) |
| Stor98 | The NVMe driver for a drive behind a Thunderbolt or USB4 port | 98 SE to 8.1; opt-in on 10 |

It handles SuperSpeed (5 Gbit/s) devices and hubs, USB 2.0 and 1.1
devices and hubs at their true speed, and UAS. A link that trains faster
than 5 Gbit/s (USB 3.1 Gen 2, USB 3.2 Gen 1x2 and Gen 2x2) runs at its
trained rate.

The 32-bit drivers serve 98 SE, ME, 2000, XP and 32-bit Vista to 10; the
64-bit ones are a separate build for XP x64 and 64-bit Vista to 10.
Pointing Windows at the wrong folder is harmless: it finds no driver there.

What it is not:

- **Not a USB storage driver for Windows 98 SE.** USB disks there need
  NUSB's storage files; the download carries them and the installer offers
  them ([USB storage on 98 SE and ME](#windows-98-se-and-me-the-stock-install-and-storage)).
- **Not signed by Microsoft.** The 32-bit drivers are not signed: 98 SE and
  ME do not check, 2000 to 10 warn and install. The 64-bit drivers carry
  the project's own test signature, which 64-bit Vista to 10 accept only in
  test-signing mode ([below](#64-bit-vista-to-10-test-signing-mode)).
- **It does not put idle devices to sleep** unless you turn that on
  (`XhciSelectiveSuspend`, [Settings](#settings)).
- **It does not write to your disk.** It creates no file; the xHCI98 USB
  Tree reads its log from the running driver when you ask for a report.
- **Not the `1.2.0.0` miniport.** Installing this package replaces it
  (both are called `xhci98.sys`).
- **Not a disk benchmark.** To measure a disk, use CrystalDiskMark or ATTO
  Disk Benchmark (suggestions, not part of this package).

## What's new

### What `3.5.0.0` changes

USB:

- **Windows 2000:** a USB drive that is not UAS (a Bulk-Only stick or disk)
  is kept across a hibernation that starts the controller afresh, with its
  drive letter and open files, when it is a single drive (not a card reader
  with several slots, nor part of a composite device) on a root port, has a
  serial number, was not in the middle of a command and is found unchanged
  after the wake (`XhciResumeKeepBot`).
  Setup writes `1` on Windows 2000 and `0` on XP and later.
- Two drives that report the same serial number are no longer kept across
  such a resume, so that one cannot be taken for the other: both are found
  again instead.
- **A controller older than xHCI 1.0** (such as an early ASMedia ASM1042)
  always uses the smaller event ring, 256 entries, whatever
  `XhciEventRingTrbs` says: with the larger one it could stop answering
  right after Windows started.
- A fresh install writes `XhciEventRingTrbs` `256`, the smaller event ring;
  an update keeps the value it has (`3.4.2.0` and `3.4.3.0` wrote `1024`).
- **Standby and hibernation with a Thunderbolt or USB4 drive the bundled
  Stor98 runs (not on 98 SE and ME):** a drive that woke before its
  connection was back could stop answering, and Windows could then not
  finish a copy to it, find new devices or shut down. The driver now powers
  it up again once its connection is back, so Windows keeps answering it
  and can shut down (`XhciCmWakePowerUp`).
- `XHCITR /set` no longer shows a setting's warning before it refuses the
  change: a refused change now shows only the refusal.
- A controller that the driver restarts in place after it stopped answering
  is given time to finish a slow reset, instead of being given up at once.
- **A UAS drive that a hardware monitor program (HWMonitor and the like)
  asks for its SMART data no longer hangs until it is unplugged.** A command
  the drive answered but whose data never arrived resets the drive, and its
  volume carries on ("device reset: the drive answered but its data never
  arrived").
- A UAS drive that refuses an ATA pass-through request as unknown is asked
  it once: after that refusal the driver answers the same request itself,
  for that drive and unit, until the drive is plugged in again.
- After a request on a UAS drive's stream is cancelled, the driver checks
  where the controller left each stream and sets a stale one again
  (`XhciStreamDequeueRepair`; `0` only logs what it finds).
- A UAS drive whose read or write data still never arrives after 3 resets
  for that direction, with none arriving between them, is given up until
  it is plugged in again, instead of being reset again and again
  ("given up: the drive's data never arrived after 3 resets; until it is
  replugged"); every request to it then fails at once. If its requests in
  flight cannot be ended, the driver instead disconnects the drive and
  finds it again. Such a drive can be set to Bulk-Only with
  `XhciForceBulkOnly`.
- **Intel 7-, 8- and 9-series and C610/X99 chipsets:** at a first install
  on these boards Setup proposes the port switchover, already selected, so
  that USB 3 devices on the blue connectors run at USB 3 speed. The
  download's `tools\portsw\` scripts are gone: the USB Tree does what they
  did.

Thunderbolt and USB4:

- The xHCI98 USB Tree shows, for each Thunderbolt or USB4 controller, a
  "Ring interrupt:" line: whether its interrupt is in use and how, and if
  not, why.
- **98 SE and ME:** the xHCI98 USB Tree and `INSTALL /console /eject`
  refuse to stop a Thunderbolt or USB4 port while a drive behind it runs
  through the BIOS (MS-DOS compatibility mode), and when they cannot tell
  whether one does: stopping it took the drive away under Windows. Shut
  Windows down before you unplug it.
- **98 SE and ME:** when the xHCI98 USB Tree or `INSTALL /console /eject`
  refuses to stop a drive because it cannot be asked to stop cleanly, it
  now says why: "The drive cannot be asked to stop cleanly (Stor98 is not
  running it, or it was connected before Windows started), so it is not
  ejected from here. Shut down Windows before you unplug it."

- **2000 to 10, a USB4 host with no pre-boot support:** a drive behind a
  dock or in a chain no longer disappears about 40 seconds after a standby.
  On Windows 7 to 10 a drive plugged in after Windows started is still
  missing after the wake where the computer's firmware does not set up its
  USB4 ports again (see
  [Known limitations](#thunderbolt-and-usb4-known-limitations)).
- **98 SE:** a drive behind a Thunderbolt or USB4 port is kept across a
  standby. A dock's or a chain's drives are removed after the wake and
  found again a few seconds later (see the standby item in
  [Known limitations](#thunderbolt-and-usb4-known-limitations)).

Installer:

- **98 SE and ME:** when `XHCIINST.LOG` cannot be written at the root of
  the system drive, Setup writes it in the temp folder and shows where.
- **2000 to 10:** only one Setup runs at a time. A second one stops at
  once: "Cannot continue: another Setup run is in progress: nothing is
  changed. Run Setup again when it has finished."
- Every registry value the drivers read is a row of the xHCI98 USB Tree's
  settings page, but for the few set on a page of their own (the log
  levels, Thunderbolt networking, the network adapter's own settings), and
  every one of them but a UAS drive's own values is a row of Setup's
  settings too.
- The USB Tree's settings page sets a UAS drive's own values
  (`XhciUasNoAtaPassThrough`, `XhciUasFastReset`) for each connected
  drive.
- The settings page's Find box lists the settings whose name, meaning or
  choices hold the text typed; `XHCITR /set /show <text>` and `INSTALL
  /console /set /show <text>` list them the same way.
- The USB Tree's snapshot takes every controller this driver drives, not
  the first alone, and the Thunderbolt and USB4 connection manager's state
  as well: Save Snapshot writes it beside the snapshot and says where ("The
  Thunderbolt / USB4 state is in ..."), and Save Report carries the whole
  text.
- The USB Tree's quick save writes its files through to the disk before it
  says it is done, and says so when its folder is on a USB drive this
  driver drives.
- **Another manufacturer's USB 3 driver** on a controller is left as it is
  unless you tick the box shown for it (`/takeover` in the console; not on
  98 SE and ME): xHCI98 then replaces it there, one controller at a time.
  The uninstall gives the controller back to that driver while Windows
  still has it; Windows Update may put it back.
- **Logging level 4:** the USB Tree shows both drivers' logs at level 4 as
  it does at levels 2 and 3, and Save Report includes them and the
  snapshot's dump. At level 4 they hold memory addresses; levels 1 to 3
  hold none.
- The USB Tree says when a controller's Device Manager entry was installed
  from an older version's files than the driver that is running: "This
  entry was installed from another version's files (2.2.0.1) than the
  driver that is running (3.4.3.0): run SETUP.EXE to update it."
- Each Device Manager entry in the USB Tree also shows the INF file it was
  installed from, the hardware or compatible ID Windows matched and the
  provider, where Windows recorded them.
- **A port on hold:** when a device keeps failing, the driver stops trying
  its port. The USB Tree now shows such a port as "(on hold)" and says
  what to do: "This port is on hold after repeated failures. Unplug the
  device and plug it in again. The hold also ends when the controller
  restarts." (for a port the driver switched off, restart Windows, or
  disable and enable the controller in Device Manager). The tool's icon
  also says so in a balloon, once for each hold (on Windows 2000 and
  later while "Warn when a device is removed unexpectedly" is ticked, as
  it is by default).
- The USB Tree's report shows what a USB device's driver asked for and was
  not given, in twelve new counters (`PdoQiUnanswered`, `PdoIoctlRefused`,
  `XferShortRefused` and the `Usbdx...` ones) and in the log, so a report
  from a UAS drive that does not appear under another UAS driver shows
  where it stopped.

Other drivers:

- usb-wdm-free 0.1.10: the network drivers' Advanced tab (Energy-Efficient
  Ethernet, speed and duplex, flow control, offloads), cdcnet tries again when
  an adapter does not come back after standby, sound and webcam streams start
  again after a stall, and uac1 (98 SE) is copied only: point Update Driver at
  its folder. See its release notes
  (https://github.com/yeokm1/usb-wdm-free/releases).
- usb-wdm-nonfree v1.4.0: Realtek's drivers for its RTL8157 and RTL8159
  adapters on Windows 7, 8 and 8.1, and Realtek's driver for its other USB
  Ethernet adapters on 8 and 8.1. Both are copied only: point Update Driver
  at their folders. The RTL8157 and RTL8159 driver is Realtek's with six lines
  of its INFs enabled, so it is unsigned. See its README
  (https://github.com/yeokm1/usb-wdm-nonfree).

### What `3.4.3.0` changes

The full list is the `3.4.3.0` entry of `releases/history.md`.

USB:

- **Windows 2000: USB audio plays again.** `3.4.2.0` refused how Windows
  2000's own audio driver starts a stream, so no sound played ("Your audio
  hardware cannot play files like the current file"; GitHub issue 32).
- A USB 3 device whose SuperSpeed link keeps failing is moved to the USB
  2.0 side of the same connector or hub and runs at High Speed, instead of
  being reset again and again (`XhciSsFlapFallback`; `0` leaves it off
  until it is plugged in again).
- `XhciDmaStaging` `1` (off by default) lets the disable, update or removal
  of a controller that stopped answering finish instead of waiting, at the
  cost of one more copy of the data ([tuning.md](tuning.md)).
- Smaller fixes: an audio or video request whose last packet failed ends at
  once; a USB 2.0 endpoint with an unusual packet size is opened.

Thunderbolt and USB4:

- **Standby and hibernation with a Thunderbolt or USB4 drive attached work
  on Windows 2000 and later** (`XhciCmSleepEntry`). On 98 SE and ME, stop
  and unplug the drive first.
- **98 SE and ME:** a dock's second and further USB controllers, left at
  Code 29 for want of an interrupt, now start without one, polled.
- **98 SE and ME:** `XhciCmRingInterrupt` `1` works with `XhciPfwNhiLine`
  `0`: plugs, unplugs and network traffic are seen at once.
- **USB4 hosts whose tunnels xHCI98 builds:** on 98 SE and ME a device in a
  daisy chain is set up when plugged in and removed when unplugged; on 2000
  to 10 stopping it closes its own connection.
- **Hosts that do not say which port leads where (ASMedia ASM4242):**
  `XhciCmInferAtStart` `1` (off by default) lets the driver work out the
  port of a device attached at start, so its eject closes the connection.
  Where the port stays unknown, the eject says so.
- **Windows 8.1:** a tunnelled NVMe drive keeps Stor98 across a disable, a
  driver update or a restart of its devices.

Networking:

- **98 SE and ME:** Thunderbolt networking no longer stops Windows with a
  fatal exception 0E in TCP/IP while sending, and incoming data no longer
  stalls.
- Network traffic is reported at once with the controller's interrupt on
  (`XhciCmNetInterrupt`).
- **With a Windows 11 computer:** traffic passes where the link came up
  empty, and the link comes back after the cable is plugged in again.
- Two speed tips for 98 SE and ME in [tuning.md](tuning.md): the TCP
  receive window, and the controller's interrupt.

Storage:

- **Stor98 on 98 SE:** `StorTakeFirmware` `1` (off by default) takes an
  NVMe drive the BIOS has set up. **Taking it can hang Windows' start or
  corrupt data if the BIOS still uses the drive.** Ignored on ME.

Installer:

- A reinstall at the same version replaces every file whose bytes differ.
- **98 SE and ME:** after a Stor98 update Setup offers "Restart Windows
  now", as on other systems. Disconnect external NVMe drives before a
  Stor98 update, as before.
- **32-bit Windows 10 (2004 or later):** Setup offers `XHCIPFW.SYS`, as on 8
  and 8.1. On 8 to 10 it acts only at a full start: restart Windows after
  installing it (with fast startup on, Shut down and power on is not a full
  start).
- The text-mode Setup (F6) entry for Windows 2000 also serves 32-bit XP:
  "xHCI98 USB 3 Controller (2000/XP, USB keyboard/mouse, files from CD)".
- The USB Tree: optional USB drive notices; a Thunderbolt networking node;
  `/eject` with the ID of a device inside a Thunderbolt or USB4 device stops
  that whole device.

### What `3.4.2.0` changes

The full list is the `3.4.2.0` entry of `releases/history.md`.

- **2000, XP and XP x64: a USB 3 controller the BIOS gives no interrupt**
  (some HP laptops of 2012 with the Intel 7 Series controller) gets the
  line the chipset routes it to, or is polled, where XP stopped it at Code
  12 and Windows 2000 stopped at start ([Known limitations](#known-limitations)).
- **ME:** unplugging a USB audio device while it plays no longer stops
  Windows.
- **A UAS drive's refused SMART read** could leave the drive waiting for
  good, its volume lost and Windows frozen; the driver now recovers it.
- **A drive that offers UAS and Bulk-Only, used as Bulk-Only,** has a
  Device Manager entry of its own for that, so it no longer stops at Code
  10 (`XhciBulkOnlyOwnId`, [USB storage](#usb-storage-uas-and-bulk-only)).
- **Stor98:** 4 KiB-sector drives work on every system (with a limit: see
  [Known limitations](#known-limitations)); a drive above 2 TiB has its size
  on 7.
- **UAS, per drive:** `XhciUasNoAtaPassThrough` and `XhciUasFastReset`
  ([tuning.md](tuning.md)).
- **The USB Tree's Settings window** shows and changes the drivers'
  settings; **Quick save** saves a report and a snapshot with no questions
  ([The xHCI98 USB Tree](#the-xhci98-usb-tree)).
- **XP x64 and Server 2003 x64:** Setup copies `usbui.dll` from Windows'
  own files when it is missing, for the Advanced and Power tabs; answer
  Windows' "Windows Logo testing" box with Continue Anyway.
- **98 SE:** the uninstall also removes Windows Setup's records of the INFs
  it placed.
- XHCIQUAL: `--edd` ([EDD reports](edd-reports.md)).
- usb-wdm-free 0.1.9; usb-wdm-nonfree v1.3.0 adds Microsoft's USB smart
  card reader driver for 2000 and XP, copied only.

### Older releases

What each earlier release changed is its entry in
`releases/history.md`. The sections these notes carried for `2.3.0.0`
to `3.4.3.0`, as published, are in `docs/contributing/release-notes-archive.md`.

## Before you install

### Requirements

| Windows | What it needs |
|---|---|
| 98 SE (4.10.2222) | Nothing for the controller, hubs, HID and audio. USB storage needs NUSB's storage files: the installer adds NUSB 3.3's (next section). Have the Windows 98 SE CD (or its CAB files on the disk) at hand: Windows may ask for its own files |
| ME (4.90.3000) | Nothing; ME's own storage files are used. Have the ME CD (or the CABs its Setup left on the disk) at hand |
| 2000 SP4, XP SP3 | Nothing |
| XP x64 SP2, Vista SP2, 7 SP1 (32- and 64-bit) | Nothing. 64-bit Vista and 7 need [test-signing mode](#64-bit-vista-to-10-test-signing-mode) |
| 8, 8.1, 10 (32- and 64-bit; 10 below build 22000) | **Secure Boot off.** The x64 editions need test-signing mode. The installer installs the Thunderbolt 3 / USB4 support by default and the USB drivers only when you tick them; Windows' own USB 3 driver keeps the controllers otherwise. Windows Server 2016, 2019 and 2022 count as 10. Windows 11 is refused |

No USB 2.0 stack is needed on any system: the driver replaces the port
and hub drivers. On 98 SE and ME, NUSB's or SweetLow's stack, if
installed, is simply not used. **Do not install NUSB on ME, 2000, XP,
Vista or 7.**

The controller must be an xHCI controller (PCI class `0C0330`) whose
register window (BAR0) lies below 4 GB. A controller with no interrupt is
polled.

### Windows 98 SE and ME: the stock install, and storage

- **98 SE and ME need no USB 2.0 stack.** On a stock install the driver
  runs the controller, hubs, HID and composite audio devices through
  Windows' own class drivers. On 98 SE the install asks for the Windows CD
  for `usbd.sys`, and the first HID device asks again for `hidclass.sys`.
- **USB storage on 98 SE needs NUSB's mass-storage files**, for Bulk-Only
  and UAS alike: `USBSTOR.INF`, `USBSTOR.SYS`, `USBAUTH.SYS`, `USBU2A.SYS`,
  `USBNTMAP.INF`, `USBNTMAP.SYS` and `USBMPHLP.PDR`. The download carries
  NUSB 3.3's copies, unmodified, in `USBDRV\NUSB33\` (with a `NOTICE.md`
  saying whose they are; they are not this project's work), and Setup's
  "Storage drivers for Win 98 SE" row installs them (`/storage`,
  `/nostorage`). An NUSB 3.3 or 3.6 already installed is kept.
  - Without them a Bulk-Only stick is "Unknown Device" (Code 28), and a UAS
    disk stays at Code 2 even after a restart.
  - **Installing the seven files by hand:** first copy `USBNTMAP.INF` into
    `C:\WINDOWS\INF`, **before the first USB disk is plugged in**, then
    point the Add New Hardware wizard (or Update Driver) at the folder with
    the files. Otherwise Windows' own `NTMAP.INF` takes the disk, and Windows
    stops with "Invalid VxD dynamic link call from NTMAPHLP" and hangs - even
    with all seven in the folder. If a disk was installed before
    `USBNTMAP.INF` was in place and shows as a generic "Disk drive" with no
    letter, run Update Driver on it, pointed at the same folder, then unplug
    it and plug it back in.
- **The eject icon on 98 SE** ("Unplug or Eject Hardware") comes only with
  NUSB 3.3's own setup (Microsoft's KB 242975), not with the seven files.
  It lists USB sticks and UAS disks, as ME's does; neither lists a
  Thunderbolt or USB4 drive. The xHCI98 USB Tree stops all of them.
- **ME** copies its own storage files from its CABs when the first
  ordinary stick installs. A UAS drive plugged in before that shows Code 2:
  plug in an ordinary stick once, then unplug the UAS drive and plug it back
  in.
- The UAS driver installs on 98 SE with no restart.

**The files Windows supplies.** The install needs two of Windows' own
files, which the package does not carry: `usbd.sys` and `usbui.dll` (the
USB property pages). The INF has Windows copy each from its own
installation source when it is missing: on 98 SE an "Insert Disk" prompt
for the Windows 98 Second Edition CD (give it the CD's `WIN98` folder if
asked); the NT systems take them from their driver cache with no prompt.
On XP x64 and Server 2003 x64 the INF fetches `usbd.sys` alone: Setup
copies `usbui.dll` from Windows' files when it is missing, but an install
from the INF alone (Update Driver, the F6 floppy) leaves the controller
without its Advanced tab and the root hub without its Power tab. Vista and
later have both files already.

### 64-bit Vista to 10: test-signing mode

The 64-bit drivers carry the project's own test signature, not
Microsoft's. 64-bit Vista to 10 load them only in test-signing mode, or on
a start with driver signature enforcement disabled (F8 on Vista and 7,
Startup Settings on 8 to 10). On 8, 8.1 and 10, turn Secure Boot off in the
firmware setup first. Then, once:

1. Open an elevated Command Prompt: Start, type `cmd`, right-click it, Run
   as administrator.
2. Run `bcdedit /set testsigning on`. It stays on for every boot until you
   turn it off.
3. Restart. "Test Mode" shows on the desktop.
4. Install the driver. Windows still says it cannot verify the publisher:
   choose "Install this driver software anyway". The Driver tab then says
   "Not digitally signed". Both are expected.

- Check: `bcdedit /enum {current}` shows `testsigning Yes`.
- Turn it off: `bcdedit /set testsigning off` and restart. Without it (and
  without F8) the controller shows Code 39 on Vista, Code 52 on 7, and
  nothing plugged into it works, a USB keyboard or mouse included.
- **With BitLocker on the system drive, suspend BitLocker before changing
  the boot setting**, or the next boot asks for the recovery key.
- This is not Microsoft signing: the certificate is the project's own test
  certificate, which no Windows trusts by itself. Importing
  `nt-x64\xhci98-test.cer` is not needed and not recommended.
- F8 -> "Disable Driver Signature Enforcement" still works, for one boot.
  After an install on such a start the Program Compatibility Assistant may
  say "Windows requires a digitally signed driver"; the driver still loads
  on a start with enforcement disabled.
- 32-bit Vista to 10 and XP x64 do not need it.

**Keep test-signing mode on while the Thunderbolt 3 / USB4 support is
installed** on 64-bit Windows with a Thunderbolt or USB4 controller,
whether or not anything is plugged in. Setup installs that support there
only in test-signing mode.

- Started with test-signing mode off, Windows reaches the desktop, and the
  Thunderbolt controller, its PCI ports and the USB controllers this
  package runs show Code 52 (Code 39 on Vista). Turn test-signing mode on
  again and restart.
- After an install Setup did not run (a port that appears later, Update
  Driver by hand), while the safe-removal mark service is stopped or has not
  yet checked, the connection manager can still be a boot driver. Then a
  start with test-signing mode off can stop 64-bit Windows 8 to 10 in
  Automatic Repair, and Windows 7 x64 starts but rolls the xHCI98 install
  back. The way back: one start with signature enforcement disabled (F8 on
  Vista and 7, Startup Settings on 8 to 10), turn test-signing mode on
  there, restart, and run Setup again.
- Where the disk Windows starts from sits behind a port the filter is on,
  the connection manager stays a boot driver, and Setup says so.
- A Windows feature update, turning Secure Boot on, or a boot repair can
  turn test-signing mode off.

### Thunderbolt and USB4: the BIOS settings

- Boot in legacy (CSM) mode, as for every system here.
- **Thunderbolt BIOS Assist off.** With it on and nothing attached at
  power-on, the BIOS switches the controller off, and the driver cannot
  switch it back on (that takes an ACPI method). On the ThinkPad P14s Gen 1
  (BIOS 1.36) Assist on with pre-boot support off hides the controller
  even with a device attached; with pre-boot support on, the controller
  goes away about 15 seconds after the last device is unplugged and does
  not come back. On 98 SE and ME, keep "Thunderbolt BIOS Assist Mode" off
  on every ThinkPad that has it.
- **Security level:** User Authorization or No Security. Secure Connect,
  DisplayPort-only and USB-only are not supported.
- **Pre-boot support** (boot from Thunderbolt) may be on or off. When on, a
  device attached at power-on keeps working under the driver.
- **Above 4G Decoding:** off for 32-bit Windows, on for 64-bit.
- **A dock plugged in after Windows starts needs bus numbers the BIOS
  reserves for the Thunderbolt ports**, as boards commonly do. Without them
  the dock's switch stays at Code 31 on 7, and nothing behind the port
  appears on XP or 98 SE.

### XHCIQUAL: checking a machine's controller (optional)

`XHCIQUAL.EXE`, in the download's `tools\xhciqual\`, is a DOS tool that
reads the machine's xHCI controller and says whether the driver can work on
it. It changes nothing. **Run it from real DOS**, not a DOS box in Windows,
booted without EMM386 or any other memory manager (`HIMEM.SYS` is fine; if
the tool will not start on a bare boot, add `DEVICE=C:\WINDOWS\HIMEM.SYS
/M:1 /V` to `CONFIG.SYS`).

Run with no arguments, it prints one of four verdicts:

| Verdict | Means |
|---|---|
| `LOOKS QUALIFIED` | nothing a read-only pass can see rules the machine out |
| `LOOKS QUALIFIED WITH CAVEATS` | the same, but the controller has no interrupt line (the driver polls it, or Vista and 7 take a message-signalled interrupt where it offers one), or no USB 2.0 ports (a limit of the tool's tests, not of the driver) |
| `DISQUALIFIED` | no `CC_0C0330` controller, or BAR0 unusable or above 4 GB |
| `CANNOT SAY` | the controller is powered down or its memory is switched off: change the BIOS setting and run it again |

- **For a bug report:** run `QUALREP.BAT` from that folder. It runs the
  read-only probe and saves a new log, named from the date and time, in the
  current directory; attach the `.LOG` it names.
- The report names the machine (make, model, BIOS, processor, memory, DOS
  version) but never a serial number, UUID, asset tag, SKU or part number.
  `--no-sysinfo` leaves that out.
- `--gsi N` compares an Intel chipset's interrupt routing with N, the IRQ
  Windows shows for the controller (Device Manager, Resources tab).
- `xhciqual/hardware-testing.md` has the full active test, which resets
  the controller.

## Installing

The download has one folder per Windows family, each holding only what
that Windows installs:

| Folder | For | At its root | `cm\` | `stor98\` |
|---|---|---|---|---|
| `98-me\` | 98 SE, ME | `xhci98.inf`/`.sys`, `xhciuas.inf`/`.sys` | `xhcicm.inf`/`.sys`, `XHCIPFW.VXD`, `tbnet\` | Stor98, `stor98.mpd`, `S98VSD.VXD` |
| `nt-x86\` | 32-bit 2000 to 10 | the same, `xhcihlpr.sys` (the helper 32-bit 2000 to 7 use) and `txtsetup.oem` (the root files are the F6 floppy) | the same, `xhcicmf.inx` (the PCI-port filter), `XHCIPFW.SYS`, a second `xhcihlpr.sys` | Stor98 |
| `nt-x64\` | XP x64, 64-bit Vista to 10 | the same without the helper, `xhci98-test.cer`, a catalog beside each INF (keep the `.cat` files there) | `xhcicm.inf`/`.sys`, `xhcicmf.inx`, `tbnet\` | Stor98 |

`INTEGRATE.TXT`, at the download's root, says which folder to give nLite,
NTLite or DISM (the `nt-x64\` folder also goes into a Windows PE or Setup
`boot.wim`). No Microsoft file is in these folders; the 98 SE storage
files are beside them, in `USBDRV\NUSB33\`.

### With SETUP.EXE

1. Unzip the whole download.
2. Double-click `SETUP.EXE` at its top (on 2000 and XP as an administrator;
   on Vista and later, answer Yes when Windows asks).
3. Go through the pages: Welcome, Components, Thunderbolt and USB4 (when
   that support is ticked), Free USB drivers and Non-free USB drivers (when
   the download carries them), Summary. Help, or F1, explains each page.
   Nothing changes until you click Install.
4. Answer Windows' unsigned-driver questions. If the last page lists steps
   left for you (a Found New Hardware window, say), do them and run
   `SETUP.EXE` again. Restart when it offers.

What it installs:

- **The USB drivers**, on every controller this package can run, except one
  another manufacturer's driver runs. On 8, 8.1 and 10 they are unticked: tick
  them to replace Windows' own USB 3 driver (a warning follows).
- **Another manufacturer's USB 3 driver** on a controller is left as it is
  unless you tick the box shown for it (`/takeover` in the console; not on
  98 SE and ME): xHCI98 then replaces it there, one controller at a time.
  The uninstall gives the controller back to that driver while Windows
  still has it; Windows Update may put it back.
- **The Thunderbolt 3 / USB4 support** (the connection manager, its
  PCI-port filter and, on 2000 to 10, the safe-removal mark service). It
  can be installed without the USB drivers on 2000 to 10, for a computer
  whose USB controllers keep another driver; 98 SE and ME need the USB
  drivers. Where Intel's Thunderbolt driver runs the controller, the box is
  unticked; ticked, it replaces Intel's driver after a warning, and the
  uninstall gives the controller back.
- **Stor98**, ticked with the Thunderbolt 3 / USB4 support (unticked on
  10), and only ever on NVMe drives behind a Thunderbolt or USB4 port
  ([NVMe drives](#nvme-drives-behind-thunderbolt-or-usb4-stor98)).
- **XHCIPFW** with the Thunderbolt 3 / USB4 support, where it is offered:
  98 SE, ME and 32-bit 2000 to 10 (10 from version 2004). It closes PCI
  bridges' memory windows that lie wholly above 4 GB, so Windows finds the
  devices behind them. On 2000 and XP it acts only when one processor
  runs: on a computer with several, start Windows with `/NUMPROC=1` in
  `BOOT.INI` if it needs this. On 8 to 10 it acts only at a full start: restart
  Windows after installing it (with fast startup on, Shut down and power on
  is not a full start).
- **Thunderbolt networking**, unticked by default (`/tbnet`, `/notbnet`).
- **The xHCI98 USB Tree and the tools**, in `C:\XHCI98` unless you choose
  another folder (on 98 SE and ME a path of short names with no spaces).
- **Each setting the drivers read**, at its default, only where it is
  missing: a value you set survives an update.
- On 98 SE and ME, **the silent install** (off by default): with it on, a
  hub, a boot keyboard or mouse and a Bulk-Only stick install with no Add
  New Hardware wizard ([tuning.md](tuning.md), "The silent install").

Per system:

- **98 SE and ME:** Setup copies the drivers to the install folder, adds it
  to the places the hardware wizard searches, and installs each device's
  driver. A Windows dialog may ask for the Windows CD (for `usbd.sys`). A
  USB 3 controller added later brings the hardware wizard, which asks once
  for the "xhci98 xHCI Host Controller Disk": type the install folder
  (`C:\XHCI98`). The first install of the Thunderbolt network adapter on 98
  SE may ask for `xhcinet.sys`: type `C:\XHCI98` too. Restart when asked.
- **2000 to 10:** Setup installs every driver of the package on the devices
  present and stages the INFs for devices plugged in later. It never
  replaces a newer release unless told to (`/force`).
- **8, 8.1 and 10:** where Secure Boot is on, Setup refuses, before any
  change, a run with the USB drivers, and on 32-bit Windows one with the
  Thunderbolt 3 / USB4 support. On 64-bit 10 it says when memory integrity
  (Core isolation) is on: Windows then refuses a driver it finds
  incompatible with it.

Every run writes `XHCIINST.LOG` at the root of the system drive.
Attach it to a report about an install.

### From a command line or a script (INSTALL.BAT)

`INSTALL.BAT`, at the download's top, runs the same installer: with no
argument it opens the window; `INSTALL /console` runs it in the console
(on 98 SE and ME change to that folder first; on 2000 and XP as an
administrator; on Vista and later from a Command Prompt run as
administrator). `tools\xhciinst\readme.txt` is the full reference.

- `/list` shows what it would do and changes nothing; `/yes` skips the
  confirmation; `/defaults` asks nothing; `/set Name=Value` sets a value;
  `/to <path>` picks the install folder; `/uninstall` removes the drivers.
- At the end it says "Changes were made. Please restart Windows now." or
  "No changes were made."
- **Exit codes:** 0 done; 1 refused or cancelled, nothing changed; 2
  something failed (the screen and the log say what was done); 3 done,
  restart Windows; 4 done as far as it can, with the steps left printed.

### By hand from Device Manager

1. On 98 SE, for USB storage, install NUSB's storage files first
   ([above](#windows-98-se-and-me-the-stock-install-and-storage)).
2. In Device Manager, find the xHCI controller (usually under "Other
   devices") and update its driver from the family folder:
   - **98 SE and ME:** Properties, Driver, Update Driver, Specify a location.
     The Add New Hardware wizard asks the same; give it the same folder.
   - **2000 and XP:** Properties, Driver, Update Driver, Have Disk. XP warns
     about the unsigned driver: Continue Anyway.
   - **Vista and 7:** Update Driver Software, Browse my computer for driver
     software; install the driver anyway when Windows warns. Use Device
     Manager rather than right-clicking the INF.
3. It installs as "xHCI98 USB 3 Controller" with "xHCI98 USB 3 Root Hub"
   beneath it (a hand install shows no manufacturer and product in the name).
   Restart if asked: 98 SE asks once; 32-bit Vista and 7 ask for the
   helper and hold the controller (Code 28 on 7, Code 1 on Vista) until the
   restart - also after a reinstall when Windows does not ask.
4. The first UAS disk asks for its driver: point it at the same folder. 7
   shows no wizard: install it from the disk's entry in Device Manager
   (Update Driver Software, Browse my computer, the same folder).
5. An external hub installs as "xHCI98 USB Hub" from the installed driver:
   with nothing to answer on 98 SE and 2000, with the Found New Hardware
   wizard and the unsigned-driver warning on XP.
6. A Thunderbolt or USB4 controller shows as an unknown PCI device: point
   its Update Driver at the folder's `cm\`. On 2000 to 7 the PCI-port filter
   is needed as well, which only Setup installs.
7. A tunnel drive's NVMe controller without a driver: Update Driver, Have
   Disk, the folder's `stor98\` (on 98 SE and ME pick `stor98.inf`).

On 2000, if the first Update Driver after a cancelled Found New Hardware
wizard leaves the controller with no driver, run Update Driver again.

### Installing Windows 2000 or XP, or using the Recovery Console: the F6 floppy

For a machine whose keyboard or install medium is on the xHCI controller:

1. Copy the files at the root of `nt-x86\` (`nt-x64\` for XP x64), without
   its subfolders, to the root of a floppy.
2. **Windows 2000, and 32-bit XP with a USB-only keyboard:** from the
   `I386` folder of the Windows CD you install from, copy `USBD.SY_`,
   `HIDCLASS.SY_` and `HIDPARSE.SY_` to the root of the same floppy, as
   they are: do not expand or rename them. The package does not carry them.
3. Start Setup, press F6 when it offers, press S, insert the floppy and
   press Enter. Pick "xHCI98 USB 3 Controller (2000/XP, USB keyboard/mouse,
   files from CD)" when you copied the three files, otherwise "xHCI98 USB 3
   Controller (32-bit Windows 2000/XP)"; on XP x64 its x64 entry. Press
   Enter to continue.

Setup then drives a USB keyboard, mouse and stick through its own drivers
above this one, and GUI-mode Setup installs the driver from the floppy's
copies. The same floppy serves the **Recovery Console**: press F6, then R
at the Welcome screen (on 2000, then C). Its USB keyboard works throughout.

Limits:

- **Windows 2000 with the first entry and a USB keyboard, mouse or storage
  device on the controller stops at the first start of GUI-mode Setup**
  (`STOP c000026c`, error `0xc0000034`, naming `hidusb.SYS` or
  `usbstor.SYS`): text mode copies those class drivers but not the Windows
  files they need. The "files from CD" entry with the three files avoids
  it. Without the three files on the floppy, that entry stops while copying
  with "Insert the disk labeled: USBD.SY_ HIDCLASS.SY_ HIDPARSE.SY_ ...";
  put a floppy holding them in drive A: and press Enter, or F3 to quit.
  Windows 2000 then keeps three idle service entries, `usbd`, `hidclass`
  and `hidparse`.
- **XP with the first entry, and XP x64: have a PS/2 or built-in laptop
  keyboard to answer GUI-mode Setup.** Its USB keyboard and mouse do nothing
  until a step that first asks about the unsigned driver (default No); a
  machine whose only keyboard is USB cannot answer it, and Setup stops
  there. With the "files from CD" entry, 32-bit XP's USB keyboard answers
  those questions.
- **On Windows 2000, plug the USB keyboard and stick in before Setup
  starts:** text mode uses only the devices present when the driver first
  reports them (it waits up to 5 seconds for them).
- Pressing F6 needs the BIOS's own USB keyboard support, and the floppy
  must be drive A: as the BIOS sees it.
- **A USB floppy drive works for F6 only if Setup knows it:** the Mitsumi
  (`USB\VID_03EE&PID_6901`), the Y-E Data (`USB\VID_057B&PID_0000`) or the
  TEAC (`USB\VID_0644&PID_0000`). No INF can add another; use a floppy
  drive on the floppy controller instead.
- A disk that offers only UAS is not usable until GUI-mode Setup; a flash
  stick, or a UAS disk that also offers Bulk-Only (most do), is.
- Installing Windows onto a USB disk is not supported.
- A USB drive present at the partition screen takes `C:`. Unplug the USB
  drives you do not need, or Windows installs to the next letter.
- GUI-mode Setup may ask for the floppy or the Windows CD.
- **Slipstreaming into a Windows 2000 CD** (`I386\TXTSETUP.SIF`) instead of
  F6 meets the same stop unless the driver's own file section also lists
  `usbd.sys`, `hidclass.sys` and `hidparse.sys`, as Windows' own USB
  controller sections do. The package ships no slipstreamed CD or recipe.

### Updating

Run `SETUP.EXE`: it updates every driver and keeps every value you set.
By hand: Update Driver on "xHCI98 USB 3 Controller", then on "xHCI98 USB 3
Root Hub", pointed at the family folder (an older install may still show
"xHCI98 USB 3.x eXtensible Host Controller" and "xHCI98 USB 3.x Root
Hub").

- **98 SE and ME:** restart after the update, although Windows does not
  ask; the old driver runs until then. Connect no new hub or composite
  device between the update and the restart.
- **2000:** use Have Disk (Update Driver, "Display a list of the known
  drivers", Have Disk, `nt-x86\`, the first model). Letting Windows search
  keeps the old driver. Windows 2000 ignores the dates of unsigned drivers:
  after an uninstall and a scan it may put the older package back; use Have
  Disk again.
- **XP, Vista and 7:** the new driver runs at once, after the unsigned-driver
  warning. On Vista and 7 use Have Disk too ("Let me pick from a list",
  Have Disk), and note that they do not take a build with the same
  `DriverVer` as the installed one (Setup's `/restage` replaces it).
  **32-bit Vista and 7 ask for a restart**, for the helper, and run the
  old driver until then.
- **The root hub's Driver tab** keeps showing the old version until the
  root hub is updated too, although it runs the new file.
- **`xhciuas.sys` at the same `DriverVer`:** Update Driver on "xHCI98 USB
  Attached SCSI Storage", "Display a list of all the drivers in a specific
  location", Have Disk, the family folder; searching keeps the old copy.

### Uninstalling

Use Remove in Add/Remove Programs (Programs and Features on Vista and
later), or `INSTALL /console /uninstall`. **On a machine whose only USB
controller is xHCI, the USB keyboard and mouse stop working until another
driver is installed**: the uninstall goes on only when you tick that you
have another keyboard and mouse (the console asks for the word `REMOVE`).

- **2000 to 10:** it removes, in order, the filter on a Thunderbolt port,
  every device entry of the package, its INFs, services and files, and its
  copies of the package, stopping at a step that needs a restart (run it
  again after the restart). It never removes Windows' own `usbd.sys` and
  `usbui.dll`.
- **98 SE and ME:** it removes every device entry, the package copy and its
  search path, the INF copies Windows kept and the driver files. **On
  Windows ME, wait a minute after you pull out a USB stick before you
  uninstall the drivers.**
- **Stor98** stays while a drive the installer did not put it on uses it
  (an internal drive, say).
- **Vista and 7 keep older packages:** "Delete the driver software for this
  device" removes only the package in use, and Windows may install an older
  one when it next finds the controller. Remove them all from an
  administrator's prompt: `pnputil -e`, then `pnputil -d oemNN.inf` for each
  one whose provider is "Yeo Kheng Meng" and class "Universal Serial Bus
  controllers".
- **The 32-bit helper after an uninstall by hand:** `sc delete xhcihlpr`
  from an administrator's
  prompt, then restart; on Windows 2000, which has no `sc.exe`, delete
  `HKLM\SYSTEM\CurrentControlSet\Services\xhcihlpr` with `regedit` and
  restart.

## Using it

### What Device Manager shows

- **The controller** is "xHCI98 USB 3 Controller", with its manufacturer and
  product after it where Setup knows them, such as "xHCI98 USB 3
  Controller (Renesas uPD720202)" ([every name](controller-names.md)).
  Setup writes that name at every install and update; a hand install shows
  the plain name, and a name you gave the controller yourself is kept.
  Beneath it is "xHCI98 USB 3 Root Hub", and the devices beneath that.
- **A hub** has an entry of its own, "xHCI98 USB Hub", with the devices
  behind it beneath it. A USB 3 hub shows twice: "xHCI98 USB Hub" for its
  USB 2.0 half and "xHCI98 USB 3 Hub" for its SuperSpeed half, as on
  Microsoft's own stack. Each hub has a Power tab. The driver runs every
  hub itself; Windows' own hub driver is not used. While a drive behind a
  hub is open in Explorer, Windows refuses to disable the hub: close the
  window first.
- **A composite device** (a headset's audio and buttons, a keyboard with
  media keys) shows as "xHCI98 USB Composite Device", with its parts
  beneath it. Disabling it stops every part; disabling one part leaves the
  others working.
- **A device keeps its entry when it moves** to another port or hub, if it
  has a usable serial number. One without (most mice and keyboards) is
  known by its port, as under Windows' own hub driver: moved, it is found
  again as new hardware.
- **Names.** Where one of Windows' own INFs names a device (a mouse, a
  keyboard, a stick), Device Manager shows that name. Otherwise the device's
  own product name shows ("USB Device" if it reports none). On 98 SE and ME
  a character outside plain ASCII shows as `?`.
- **Every device has the USB device interface**, as under Microsoft's hub
  driver, so programs that look for USB devices that way find them.
- **SuperSpeed shows as High Speed.** Device Manager and the other Windows
  tools on these systems show a SuperSpeed device as High Speed at most:
  what they read predates SuperSpeed. The xHCI98 USB Tree shows the real
  speed.
- **Thunderbolt and USB4 controllers:** "xHCI98 TB2 Controller (Intel
  Falcon Ridge)", "xHCI98 TB3 Controller (...)" for Alpine Ridge, Titan
  Ridge and Ice Lake, "xHCI98 USB4 Router (...)" for the USB4 hosts, and
  "xHCI98 USB4 Router" for any other USB4 host. On 2000 to 7 the PCI ports
  the filter sits on are "xHCI98 Thunderbolt PCI Bridge" or "xHCI98 USB4
  PCI Bridge" ("xHCI98 USB4 PCI Bridge (ASMedia ASM4242)" on an ASM4242's
  downstream ports): ordinary PCI bridges run by Windows' own driver, with
  this package's filter. The service is "xHCI98 Thunderbolt and USB4
  Connection Manager (xhcicm)".
- Up to `3.2.0.0` the names were longer ("xHCI98 USB 3.x eXtensible Host
  Controller", "xHCI98 USB 3.x Root Hub", "xHCI98 Thunderbolt 3
  Controller"): an older install shows them until it is updated.

### The controller's Advanced tab and the hub's Power tab

The controller has an **Advanced** tab and the root hub (and every hub) a
**Power** tab, as Windows' own USB controllers do, wherever `usbui.dll` is
present. They are Windows' own pages, answered by the driver.

- The Power tab lists each device's power in mA as the device declares it.
  A SuperSpeed device declaring more than 510 mA reads 510 mA there; a
  bus-powered hub is charged (ports + 1) x 100 mA, at most 500 mA. The
  snapshot has the exact figure.
- The Advanced tab's bandwidth counts only the audio and video streams in
  use. On Vista and 7 it stays at zero.

### The xHCI98 USB Tree

`XHCITR.EXE` (`XHCITR64.EXE` on 64-bit Windows), installed with the
drivers and started at login as an icon in the notification area. It runs
as the logged-on user, with no administrator rights unless it asks for
them. "Start at login" (View menu, or the Settings window's "xHCI98 USB
Tree" tab) turns the icon off for you; `INSTALL /notray` for the whole
machine. `xhcitr\README.TXT` (`tools\xhcitr\readme.txt` in the download)
has every command.

- **The window** shows every USB controller (this driver's and Windows'
  own), its hubs and devices with speed, port, IDs, driver and Device
  Manager status, and every Thunderbolt and USB4 controller with its
  devices, links, tunnels and the Windows devices behind them. View >
  Descriptors decodes a device's descriptors. A USB storage device shows
  whether it runs UAS or Bulk-Only and why. "PCI configuration" items show
  what Windows does not: the interrupt pin and line, MSI and MSI-X, the
  PCI Express link, the BARs, a bridge's buses and windows. The drivers only
  read.
- **Removing a device safely:** the icon's menu lists the USB storage
  devices and the Thunderbolt and USB4 devices that can be removed. The
  tool confirms, asks Windows to stop the device and says when to unplug
  it, or which drive is in use if Windows refuses. Only a USB device with a
  disk on it is offered. On 98 SE and ME the stop removes the device from
  Device Manager, and Refresh can start it again until it is unplugged; on
  XP and later Windows holds it stopped (Code 47) until it is unplugged.
- **Settings** (View > Settings..., or the icon's menu) shows the drivers'
  settings with Setup's meanings, choices and defaults, a tab each for the
  USB driver, the Thunderbolt and USB4 driver and Stor98, and changes them
  for every controller or one. After Apply it says what each change needs
  and offers the restart. On 2000 to 10 it asks for administrator rights,
  and it changes nothing while Setup runs. `XHCITR /set Name=Value` does the
  same from a command line.
- **Logging level** (View > Logging level...) shows and sets both drivers'
  log levels: `0` off, `1` counters only, `2` the log (the default), `3`
  adds port status, `4` adds memory addresses (for developers). The drivers
  read it at their start, so the tool offers a restart.
- **Quick save** (the icon's menu, or a hotkey you turn on, `Ctrl+Alt+F11`
  by default) saves the report, the snapshot and the settings with no
  questions, in a new dated folder under `C:\XHCI98\Reports\` (or "xHCI98
  Reports" in your documents). A balloon says where.
- **Balloons** say when a device connects, waits for approval or
  disconnects (on 98 SE, the icon's tooltip). On 2000 and later, "A
  Thunderbolt or USB4 device was removed unexpectedly." warns of a device
  removed without being stopped first; it does not say whether data was
  lost. Optional USB drive notices say when a USB drive is connected or
  removed.
- **"Can run faster":** when a USB device that can run at SuperSpeed runs
  slower on this driver's controller (on a USB 2.0 port, behind a USB 2.0
  hub, fallen back to USB 2.0, or a 10 Gbit/s device at 5 Gbit/s), or a High
  Speed device runs at Full Speed behind a Full Speed hub, a balloon names it
  and the icon turns red. Click the balloon for the port, the speed and what
  to try. "Tell me when a device can run faster" turns it off for you;
  Setup's box, or `INSTALL /faster` and `/nofaster`, sets it for the
  computer. "Watch Windows' own USB ports too" (off by default) also watches
  the ports of the computer's own EHCI, OHCI or UHCI controllers (not on 98
  SE). Windows' own "can perform faster" notices do not appear under
  `xhci98.sys`.
- **The red icon** means something you can put right: a device that can
  run faster, a Thunderbolt or USB4 device waiting for approval, or a
  controller that stopped answering. Its tooltip names it, and a double
  click opens the window on it.
- **Restart Windows to finish the driver update** at the end of the status
  bar means the running driver is another version than the file the next
  start loads.

### USB storage: UAS and Bulk-Only

- **Which driver a disk gets.** A UAS-capable disk gets UAS through
  `xhciuas.sys`, anything else Windows' own `usbstor.sys` (Bulk-Only), at
  whatever speed it connects. `XhciForceBulkOnly` `1` makes a disk that
  offers both use Bulk-Only.
- **SuperSpeed UAS needs streams.** On a controller that cannot stream (the
  ones Linux lists, such as the ASMedia ASM1042), a disk that offers both
  runs Bulk-Only at SuperSpeed; a UAS-only disk is sent back to the USB 2.0
  side of its connector and runs UAS at High Speed (or is refused if it has
  none; behind a SuperSpeed hub it is refused in place).
- **A disk used as Bulk-Only has a Device Manager entry of its own**
  (`XhciBulkOnlyOwnId`): Windows installs its Bulk-Only driver there, and
  the disk keeps its UAS entry where it runs UAS. A disk installed as
  Bulk-Only by a release before `3.4.2.0` is found once more as a new entry
  and may get another drive letter.
- **A UAS disk that runs Bulk-Only anyway:** Windows keeps the driver an
  entry was installed with. To give it back to UAS, uninstall the disk's
  entry in Device Manager with the disk plugged in, then unplug it and plug
  it in again. A disk a `usbstor.inf` names by its ids (Windows' own on 2000
  and later, NUSB's on 98 SE) can take `usbstor.sys` again that way.
- **For scale:** a UAS flash drive at SuperSpeed read and wrote about 200 to
  220 MB/s under 98 SE (one command at a time); modern Windows, with more
  commands in flight, is faster.

### A mouse's polling rate (hidusbf)

SweetLow's hidusbf (now maintained by LordOfMice) sets the polling rate
Windows asks of a mouse or another HID device, and this driver programs
the controller from it: 1000, 500 and 250 Hz poll every 1, 2 and 4 ms,
behind a hub too.

- **1000 Hz is the ceiling** for a Low- or Full-Speed device, unless
  `XhciFastPollFsLs` is set ([tuning.md](tuning.md#polling-a-low--or-full-speed-mouse-above-1000-hz-xhcifastpollfsls)).
- **On 98 SE and ME, set it on both keys:** the filter's `LowerFilters`
  entry and `bInterval` value on the device's hardware key and on its
  `Class\HID` driver key. hidusbf's Setup writes them; it works on a stock 98
  SE too.
- **A composite device:** hidusbf's "Process Parent" puts the filter on
  "xHCI98 USB Composite Device" and sets every part's rate there; a filter
  on a part's own entry wins for that part. A new rate reaches a part when
  it next starts: on 2000 and later restart the composite entry; on 98 SE
  and ME also restart the part's entry, or replug the device. If hidusbf's
  Setup does not offer the composite entry, set `LowerFilters` and
  `bInterval` on it by hand.
- **A device found again as new hardware loses its setting:** set it again.

### Intel 7-, 8- and 9-series chipsets: the port switchover

On Intel 7-, 8- and 9-series chipsets (Ivy Bridge to Broadwell) and C610/X99,
each switchable connector, usually a blue one, is wired to the USB 2.0
(EHCI) controller and to the xHCI controller, and the chipset decides which
has it. With the firmware's USB 3.0 setting on Auto or Smart Auto, or none,
those connectors start on the USB 2.0 controller. There they still work,
at USB 2.0 speed, wherever a driver for that controller runs (NUSB's or
SweetLow's on 98 SE and ME, Windows' own on 2000 and later).

**The switchover is off by default** (`XhciIntelPortSwitch` `0`). SETUP.EXE
proposes to turn it on when it finds one of these controllers at a first
install; an update keeps the value it had. To turn it on or off, use the
USB Tree's View > Settings..., or `XHCITR /set XhciIntelPortSwitch=1` (or
`=0`), and restart Windows. On, the driver moves those connectors to the
xHCI controller at each
start and resume, and hands them back when the controller is disabled or
removed or the machine shuts down. A standby keeps them where they are.

- **A USB 3 stick that copies only at USB 2.0 speed on a blue connector** is
  most likely still on the USB 2.0 controller, as after an install by hand
  or an update from an earlier version: turn the switchover on as above. (A
  USB 2.0 hub or cable in between gives the same speed.)
- **A device on a blue connector moves when the controller starts or
  stops:** it is disconnected on one side and found again on the other. A
  drive in use loses its drive letter and writes in flight are at risk, so
  do not copy files on a blue connector while installing this driver or
  enabling its controller.
- Only Intel xHCI controllers `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` and
  `8D31` are switched (not a Sony VAIO with subsystem `104D:90A8`).

**Value 2: any Intel controller, at your own risk.** `XhciIntelPortSwitch`
`2`, set by hand, does the switchover on any Intel xHCI controller, listed
or not:

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

### When a device or the controller keeps failing

The driver does not leave a device dead until it is replugged after a
transfer, port or controller fault:

- a lost interrupt is picked up by the driver itself, and a USB
  transaction error is retried up to three times;
- a fault the driver cannot pin on a transfer, or a USB 2.0 root port the
  controller switched off, **re-enumerates the device, as if it had been
  unplugged and plugged in again**: whatever it was doing ends as on an
  unplug, a file copy to a drive fails part way, and a drive should be
  checked as after any surprise removal;
- an over-current on a root port is waited out and the port powered again;
- a controller that halts is recovered, every device on it found again;
- after a few tries on one port (three), the next fault there removes the
  device and holds the port until the device is unplugged;
- a controller that stops answering is closed off; if the driver cannot
  prove it stopped writing to memory, **do not disable the controller
  (that hangs); save your work, and if Windows then does not finish
  shutting down or restarting, switch the computer off at the power
  button** ([Known limitations](#known-limitations)).

`XhciTolerance` `0` turns most of this off. The details are in
[tuning.md](tuning.md#controller-faults-xhcitolerance-xhciintervalcap-xhciavgtrbesit).
If a USB device stops on your machine, send two reports (one while it
works, one after it stops) as [below](#reporting-a-problem), with the
controller's PCI id; if it still stops, the same with `XhciAvgTrbEsit` `1`.

## Thunderbolt 3 and USB4

With `xhcicm.sys`, a Thunderbolt or USB4 dock, enclosure or NVMe case
plugged into the computer reaches Windows: its PCI Express tunnel comes up,
and the devices behind it (an NVMe drive, a dock's own USB controller, a
network adapter) appear in Device Manager under their own drivers, with no
Refresh. One unplugged leaves. On an Intel Thunderbolt controller whose
firmware runs its own connection manager, `xhcicm.sys` approves each device
for it; on a USB4 host whose firmware runs none, `xhcicm.sys` is the
connection manager and builds the tunnels itself. A device already
tunnelled when Windows starts (by the BIOS's pre-boot support) keeps its
tunnel and works.

Before you plug anything in, check [the BIOS settings](#thunderbolt-and-usb4-the-bios-settings).

### Which controllers

`xhcicm.sys` claims these host controllers by PCI id:

| Generation | PCI ids (`8086:` unless named) | Connection manager | Device Manager |
|---|---|---|---|
| Falcon Ridge (Thunderbolt 2) | `156A`, `156C` | the firmware's | `xHCI98 TB2 Controller (Intel Falcon Ridge)` |
| Alpine Ridge (Thunderbolt 3) | `15BF` (JHL6240), `1575`, `1577`, `15D2`, `15D9` | the firmware's | `xHCI98 TB3 Controller (Intel Alpine Ridge)` |
| Titan Ridge (Thunderbolt 3) | `15E8`, `15EB` | the firmware's | `xHCI98 TB3 Controller (Intel Titan Ridge)` |
| Ice Lake (integrated) | `8A0D`, `8A17` | the firmware's | `xHCI98 TB3 Controller (Intel Ice Lake)` |
| Maple Ridge | `1134`, `1137` | the firmware's while it runs, else `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Maple Ridge)` |
| Tiger Lake, Alder Lake, Raptor Lake, Meteor Lake (integrated) | `9A1B`, `9A1D`, `9A1F`, `9A21`; `463E`, `466D`; `A73E`, `A76D`; `7EB2`, `7EC2`, `7EC3` | the firmware's while it runs, else `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Tiger Lake)`, `(Intel Alder Lake)`, `(Intel Raptor Lake)`, `(Intel Meteor Lake)` |
| Lunar Lake, Panther Lake, Wildcat Lake (integrated) | `A833`, `A834`; `E333`, `E334`, `E433`, `E434`; `4D33` | `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Lunar Lake)`, `(Intel Panther Lake)`, `(Intel Wildcat Lake)` |
| Barlow Ridge | `5781`, `5784` | `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Barlow Ridge)` |
| AMD Pink Sardine | `1022:1668`, `1022:1669` | `xhcicm.sys`'s | `xHCI98 USB4 Router (AMD Pink Sardine)` |
| ASMedia ASM4242 | `1B21:2425`, by class | `xhcicm.sys`'s | `xHCI98 USB4 Router (ASMedia ASM4242)` |
| Any other USB4 host (PCI class `0C0340`) | by class | `xhcicm.sys`'s | `xHCI98 USB4 Router` |

Not claimed: Apple machines and Apple's controllers, the USB-only
Thunderbolt functions (`15DC`, `15DD`, `15DE`), and the older Light Ridge,
Cactus Ridge, Redwood Ridge and Win Ridge parts. Thunderbolt 3 docks and
enclosures work on every host above, and a USB4 device on a Thunderbolt 3
host works as a Thunderbolt 3 device would. DisplayPort tunnels are not
supported. A machine where the driver starts and does nothing: `XhciCmMode`
`0` turns it off without uninstalling it, and a report is welcome.

### Approval, and what it lets a device do

A Thunderbolt 3 device gets its PCI Express tunnel only when the operating
system approves it. Windows 98 to XP have no Thunderbolt control panel, and
this package adds none, so `xhcicm.sys` decides by one registry value,
`XhciCmApprove`, in the Thunderbolt controller's driver key (under the USB4
connection manager the same value decides whether a new device gets its
PCI Express tunnel):

| Value | What the driver does |
|---|---|
| `1` (the default) | Approves every device as soon as it is announced: plug it in and it gets its tunnel. |
| `0` | Approves nothing. A device that already has its tunnel (one the BIOS tunnelled at power-on, or one approved before the driver was disabled and enabled) keeps it. A new device is logged and left without a tunnel. |

**Read this before you leave it at `1`.** A device with a PCI Express
tunnel can read and write all of the machine's memory: none of these
systems has an IOMMU that this driver or Windows uses to fence a device
off. At `1`, anything plugged into the Thunderbolt port gets that access,
whatever it is and whoever plugged it in. That is the state every device
tunnelled by the BIOS's pre-boot support is already in on these systems,
and it is what the owner chose as the default; set `XhciCmApprove` to `0`
on a machine where the port is within reach of people you do not trust.

**A new install runs its first start with the defaults:** a device
connected before the restart is approved even if you chose `XhciCmApprove`
`0` in Setup. Where that matters, keep Thunderbolt and USB4 devices
unplugged until Windows has restarted.

**Nothing is remembered.** The driver keeps no list of devices, so there is
no "approve only devices I have seen before". After a re-plug, or a restart
without pre-boot support, a device is approved again by the rule above.
The Ice Lake to Meteor Lake integrated controllers have no approval message
under their firmware's connection manager: a device works on them only if
the firmware tunnelled it by itself, whatever `XhciCmApprove` says.

### Removing a Thunderbolt or USB4 device safely

Close the files open on the device first, as for any removable disk. Then:

| Windows | How |
|---|---|
| 98 SE, ME | The xHCI98 USB Tree, or `INSTALL /console /eject`. Windows' own dialogs do not list such a device (98 SE as shipped has none; ME's, and NUSB 3.3's on 98 SE, list USB storage only). |
| 2000 | "Unplug or Eject Hardware": stop it, then unplug. Unplugged without a stop, it is removed all the same and Windows shows "Unsafe Removal of Device". |
| XP, Vista, 7 | "Safely Remove Hardware": stop it, then unplug. Device Manager shows it at Code 47 until it is unplugged. |

- **On 98 SE and ME** the tool asks Windows whether the device can go (a
  file open on it refuses, and nothing changes). For an NVMe drive on the
  bundled Stor98 that was plugged in after Windows started, it then asks
  Stor98 to stop the drive cleanly (flush and shut down), waits up to about
  14 seconds, removes the device and says whether it can be unplugged. An
  NVMe drive the bundled Stor98 does not run, or one that was already
  connected when Windows started (the computer's firmware set up its
  connection), is not ejected: "The drive cannot be asked to stop cleanly
  (Stor98 is not running it, or it was connected before Windows started),
  so it is not ejected from here. Shut down Windows before you unplug it."
  A drive that does
  not confirm its stop stays stopped and is not removed: "Blocked: the drive
  did not stop; shut down Windows before you unplug it". Until the unplug,
  Device Manager's Refresh can start the device again.
- **A drive unplugged on 98 SE or ME while a program has a file open on
  it:** Windows removes its old entry only once the file is closed; the
  driver keeps asking, never forcing. Until then the USB Tree shows "A
  removal is in progress", and the drive plugged back in appears only once
  the old entry has gone.
- **A dock** is listed under its own PCI switch's name, a generic PCI
  bridge; stopping it stops everything in the dock.
- **The tunnel after a stop.** Under a firmware connection manager it stays
  up until the unplug. Under the USB4 connection manager it is closed once
  the stop is confirmed (on 2000 only by an administrator's `INSTALL
  /console /eject` or the USB Tree run as one); otherwise it stays up until
  the unplug, and the tools say so. Where the driver does not know which of
  the host's ports the device is on, the eject says "The PCIe tunnel stays
  up until the device is unplugged ...".
- **A device missing from Windows' list (2000 to 7)**, on its first plug or
  after a disable and enable or a driver update: the safe-removal mark
  service (`xhcimark`, run by Windows) normally keeps it listed. Otherwise
  stop it in the USB Tree run as an administrator ("Stop as
  administrator..."), or close every program using it, unplug it and plug it
  in again; Windows lists it from then on. Pulled out without a stop, it is
  a surprise removal: writes Windows still held can be lost.
- On 2000 and XP the tools need an administrator; on Vista and 7 they ask.
  `INSTALL /console /devices` lists what is connected; `INSTALL /console
  /eject 2` ejects device 2 of that list, and `INSTALL /console /eject
  id=<instance id> /yes` does it with no question (`tools\xhciinst\readme.txt`
  has the exit codes).

### NVMe drives behind Thunderbolt or USB4 (Stor98)

This download bundles Stor98 `3.5.0.0`.

<!-- The cut replaces the line above with "This download bundles Stor98
`<version>`." or "This download bundles no Stor98."
(scripts\package\make-release.ps1 holds it to what the cut bundles). -->

An NVMe drive in a Thunderbolt or USB4 enclosure needs an NVMe driver.
`xhcicm.sys` brings the drive's tunnel up and down; the driver below runs
the drive.

| Windows | The driver of a tunnelled NVMe drive | Why |
|---|---|---|
| 98 SE, ME, 2000, XP, Vista, 8 | Stor98 | no NVMe driver of its own |
| 7 | Stor98 | its `stornvme` comes only with Microsoft's update KB2990941 |
| 8.1 | Stor98, put on the drive by Setup or, for a drive plugged in later, by the safe-removal mark service; an internal drive keeps `stornvme` | 8.1's `stornvme` holds a write for good when the drive is unplugged without Safely Remove |
| 10 | Windows' own `stornvme`; Stor98 is opt-in per drive | it ends an unplug cleanly |

Use Safely Remove before unplugging a drive, on every system.

**Stor98 is not for internal NVMe drives: do not install it on one.**
Setup puts it only on drives behind a Thunderbolt or USB4 port. On 2000 to
8 its INF matches only the id the connection manager gives such a drive
(`PCI\XHCI98_TUNNEL&CC_010802`, among the drive's compatible ids), so
Windows never offers it for an internal drive. On 8.1 and 10 Windows' own
`stornvme` keeps an internal drive. On 98 SE and ME Windows may offer it
for an internal NVMe drive that has no driver: decline it. An internal
drive that already runs Stor98 keeps it, and the uninstall leaves it there.

- **Stor98's own guide** is `stor98-readme.txt` in each `stor98\` folder,
  with its licence (BSD-3-Clause) and notice: its settings, its limits and
  its install. Read it before installing.
- **98 SE and ME:** Setup updates Stor98 as a file, at the next start.
  **Disconnect your external NVMe drives before a Stor98 update.** Setup
  also puts `stor98.mpd` in place before the first tunnel drive is plugged,
  so Windows does not ask for the "Stor98 Storage Controller Disk".
  `S98VSD.VXD`, installed with Stor98 ("Large transfers for Stor98"), lets
  reads and writes of up to 2 MiB reach Stor98 whole instead of in 63 KiB
  pieces (`/nos98vsd` leaves it out).
- **2000 and XP, the first plug of an enclosure with a switch inside**
  (most real enclosures): the first Found New Hardware wizard finds no
  driver; finish it either way. The safe-removal mark service then restarts
  the enclosure, and a second wizard offers "Stor98 Storage Controller for
  NVMe SSDs": follow it to the end. Later plugs need nothing. Once Stor98
  runs one tunnelled drive, a newly plugged one may start on it with no
  wizard, named "PCI Device".
- **Vista to 8, a drive on a port the package does not filter:** the mark
  service puts Stor98 on it within seconds. With `XhciCmSafeEject` `0` there
  is no mark service: on 2000 and XP unplug and plug the enclosure again; on
  Vista to 8 use Update Driver.
- **By hand:** Update Driver on the drive's NVMe controller, Have Disk,
  `98-me\stor98\` (pick `stor98.inf`), `nt-x86\stor98\` or `nt-x64\stor98\`.
- **Stor98's settings** all start with `Stor` (on 2000 to 10 in
  `DriverParameter`); Setup's and the USB Tree's Stor98 tab set them
  (Stor98's readme, "Settings").

### Thunderbolt networking

Two computers joined by one Thunderbolt or USB4 cable can share a network
connection, file sharing included, in the frame format of Linux's
Thunderbolt networking: one link, not through a dock. It is off unless you
tick "Thunderbolt networking" on Setup's Thunderbolt and USB4 page
(`/tbnet`; an update keeps the choice, and the uninstall removes the
adapter). The adapter is "xHCI98 Thunderbolt Networking"; the USB Tree's
networking node shows each link's state, the other computer, the speed and
the counters.

- On 98 SE and ME install it through `SETUP.EXE`; a Have Disk install of the
  adapter needs `xhcinet.sys` copied to the Windows `SYSTEM` folder first.
- For speed on 98 SE and ME, see the two tips in [tuning.md](tuning.md):
  the TCP receive window, and the controller's interrupt.

## Settings

The easiest way to change a setting is the xHCI98 USB Tree's View >
Settings...; Setup's Settings and "Advanced..." set them at install, and
`INSTALL /set Name=Value` from a command line. Change the advanced ones
only if these notes or a maintainer advise it.
**[tuning.md](tuning.md) has every value, its range and what it does.**

- Each value is a `DWORD` in the controller's driver (software) key, the
  subkey `NNNN` whose `DriverDesc` names the controller (the USB Tree shows
  it as the controller's "Driver key"):

  | Driver | Windows | Key |
  |---|---|---|
  | `xhci98.sys` | 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
  | `xhci98.sys` | 2000 to 10 | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |
  | `xhcicm.sys` | 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\System\NNNN` |
  | `xhcicm.sys` | 2000 to 10 | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E97D-E325-11CE-BFC1-08002BE10318}\NNNN` |

- **One value for every controller:** the same value in
  `HKLM\System\CurrentControlSet\Services\USB` wins over every controller's
  own. Setup and the USB Tree write there.
- The install writes every value at its default where it is missing; an
  update keeps a value you changed. To go back to the default, set it again,
  or delete the value and update the driver.
- A change takes effect when the driver next starts: restart Windows (or,
  where tuning.md says so, disable and enable the controller).

The values you are most likely to want:

| Value | What it does | Default |
|---|---|---|
| `XhciLogVerbosity` | The driver's log level, `0` to `4` ([Reporting a problem](#reporting-a-problem)) | `2` |
| `XhciInterruptMode` | How the controller's events arrive: `0` automatic, `1` the interrupt line, `2` polling, `3` a message-signalled interrupt only (Vista and 7) | `0` |
| `XhciSelectiveSuspend` | `1` lets idle devices sleep when Windows asks; hubs never sleep | `0` |
| `XhciForceBulkOnly` | `1` uses Bulk-Only instead of UAS | `0` |
| `XhciMaxTransferKB` | The largest UAS command, `64` to `2048` KiB (`64` for a drive that fails at larger ones) | `1024` |
| `XhciImodInterval250ns` | The interrupt moderation interval, in 250 ns units: lower is faster storage and more interrupts; raise it towards `4000` if audio stutters under storage load | `160` |
| `XhciIntelPortSwitch` | The Intel port switchover ([above](#intel-7--8--and-9-series-chipsets-the-port-switchover)) | `0` (Setup writes `1` at a first install on these boards) |
| `XhciResumeKeep` | Devices on root ports kept across a resume that starts the controller afresh | `1` |
| `XhciCmMode` | Thunderbolt and USB4: `0` off, `1` automatic | `1` |
| `XhciCmApprove` | `1` approves every device, `0` none ([Approval](#approval-and-what-it-lets-a-device-do)) | `1` |
| `XhciCmNet` | Thunderbolt networking | `0` |

## Reporting a problem

The driver keeps a small log of what happened on the bus inside itself,
and the xHCI98 USB Tree reads it off the running machine, with no
administrator rights. It records by default (`XhciLogVerbosity` `2`: the
log with no internal addresses), at a small cost: about 1 to 2% at large
transfers and up to about 5% at small ones; `0` turns it off.

To send a report:

```
  1.  View > Logging level...: check xhci98 reads 2 (set 2 and restart if not)
  2.  make the problem happen again
  3.  File > Save Report...: the tree, both drivers' logs and the snapshot
  4.  attach the .TXT it wrote to the bug report
```

- `XHCITR /report <file>` writes the same report from a prompt, and
  `XHCITR /setlevel xhci98=2` sets the level (on 2000 to 7 as an
  administrator). Quick save writes the report and the snapshot in one go.
- **When a maintainer asks for the snapshot's files:** File > Save
  Snapshot... or `XHCITR /snapshot` writes four files for each controller
  and two for each Thunderbolt or USB4 controller, named from the date and
  time; its last line names the files to attach: attach them all
  (`xhcitr\README.TXT` has the names, the options and the exit codes).
- **With a Thunderbolt or USB4 problem on 2000 to 10**, also send the
  safe-removal mark service's log,
  `%SystemRoot%\System32\LogFiles\xhci98\XHCIMARK.LOG`.
- **With an install problem**, send `XHCIINST.LOG` from the root of the
  system drive.
- **For a controller question**, the DOS report `QUALREP.BAT`
  ([XHCIQUAL](#xhciqual-checking-a-machines-controller-optional)).
- The report and the snapshot name the system (Windows version, processor,
  memory, make, model and BIOS where Windows records them) but never the
  computer's name, your user name, the domain or a serial number, so they
  can be pasted into a public issue. If your firmware's strings were
  customised, read the top of the file first. `/nosysinfo` leaves the system
  out.
- The log cannot capture a crash: a machine that has stopped is not running
  for anything to read.

> **Set the level back to 2 (or 0) once you have sent a capture taken at
> level 3 or 4.** While the level is above 0, anyone using the machine can
> read the driver's diagnostic state through the tool, and at level 4 that
> includes internal addresses in the raw dump. From `3.5.0.0` the tool shows
> a level 4 log as it is, memory addresses included, and a report saved at
> level 4 holds them in its logs and its snapshot.

> **Do not run DebugView on Windows 98 on real hardware.** Under the `1.x`
> miniport, plugging in a device while it captured crashed a ThinkPad E460
> three times over. The `2.x` driver prints only from its own thread rather
> than from interrupt context, which is what that crash was traced to, but
> the warning stands.

`XhciLogDebugView` `1` (and `XhciCmLogDebugView` for `xhcicm.sys`) also
sends the log to DebugView; use it only when asked.

## Known limitations

### Windows 98 SE and ME

- **A USB disk left plugged in when the computer starts can hold Windows at
  startup for minutes, or stop it.** Plug USB disks in after Windows has
  started. To help find a fix, you can send an EDD report from your
  computer ([EDD reports](edd-reports.md)).
- **Rarely, a start stops** with "A fatal exception 0E has occurred at
  019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn the
  computer off and on again; the next start works.
- **The free and non-free drivers left unticked are copied, not staged.**
  Setup adds to Windows' driver search path (`DevicePath`) only the folders
  of the packages ticked on its Free and Non-free USB drivers pages, and
  keeps that path within the system's limit (past 280 characters 98 SE's
  hardware wizard finds no driver at all, past 259 ME's stops responding),
  greying a box that would pass it. When the wizard asks for a driver,
  choose "Specify a location" and point it at the package's folder,
  `USBDRV\FREE\<class>\<package>\<folder>` (or `USBDRV\NONFREE\...`) in the
  install folder.
- **A USB Audio Class 2.0 device needs the free `uac2` driver before its
  first plug.** Otherwise Windows' own USB audio driver, which knows only
  USB Audio 1.0, takes it, and Windows can stop with a fatal exception 0E in
  `USBAUDIO` (seen with a Sound Blaster X4). Tick `uac2` on Setup's Free USB
  drivers page (the `-free` download) before plugging the device in. If
  Windows already stopped: unplug the device and restart (in Safe Mode if
  Windows stops at the start), then in Device Manager open its audio entry,
  Update Driver, "Display a list of all the drivers in a specific
  location", Have Disk, `C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me`, and
  restart with the device plugged in.
- **The free audio and MIDI drivers may ask for Windows' own `.cat`
  files** (`wdmaudio.cat`, `ks.cat` and others) from the Windows CD. Point it
  at the CD's `WIN98` folder, or click Skip File for the `.cat` files: they
  only hold signatures, which 98 SE and ME do not check.
- **The xHCI98 drivers show no version on Windows 98's Driver tab**, only
  the file date; the four-part version is under Driver File Details.

### Windows 98 SE

- **No USB storage without NUSB's storage files**; Setup adds NUSB 3.3's
  ([USB storage](#windows-98-se-and-me-the-stock-install-and-storage)).
- **The first USB audio device plugged in after a boot can freeze
  Windows** (a sound card, a headset, a USB MIDI or audio interface, a webcam
  with a microphone), whatever its driver: Windows' own USB stack does the
  same. The taskbar clock stops and only a restart ends it. **Plug the device
  in before switching on and leave it plugged in until shutdown**; if Windows
  freezes at a plug, restart with the device plugged in. For a webcam whose
  microphone you do not need, usb-wdm-free's `NOAUD98.INF` gives the
  microphone no driver, and the camera keeps working. USBView walked while a
  USB audio device is attached can freeze the machine too.
- **Stop audio playback before unplugging a USB audio device.** Unplugged
  while Windows' own USB audio driver plays it, the device can freeze
  Windows.
- **A device that holds a shared IRQ with no working driver makes Windows
  crawl or freeze once this driver is installed** (ME is expected to do the
  same). Windows keeps such a line masked while no driver uses it; once one
  does, the device's interrupt storms. Seen with a system NVMe drive the BIOS
  still ran ("MS-DOS compatibility mode") on an IRQ shared with the xHCI
  controllers. Find it in Device Manager (Computer, Properties, View
  Resources, Interrupt request): give that device a working driver or remove
  it; or, if no other working driver uses the IRQ, disable the xHCI98
  controllers on it, or set `XhciInterruptMode` `2` (polling) and restart.
- **A device at a new location brings the Add New Hardware wizard, which
  holds that port until it is answered. Finish the wizard before unplugging
  the device.** A device unplugged while its wizard is open makes Windows
  look at no port again until the wizard is closed; keys typed into the
  abandoned wizard wedged the machine. Windows' own USB stack does the same.
- **Remove USB disks before Stand by.** With a USB disk that was attached
  when the computer started (Windows uses it in MS-DOS compatibility mode),
  the resume stops before the desktop returns, as under NUSB's USB 2.0
  driver. A disk plugged in after Windows started does not.
- **A keyboard's media keys:** volume and mute work after a restart; the
  others (play, next, browser and mail keys) need the keyboard manufacturer's
  software.
- On Windows 98 SE, with XhciDmaStaging 1 the computer can stop responding
  for several minutes at the end of long file copies and then go on by
  itself; leave it at 0.

### Windows ME

- **Plugging and unplugging a USB mouse or another HID device rapidly, over
  and over, can stop the machine**: the clock stops, typing does nothing,
  only a restart ends it. The wait is in ME's own `hidusb.sys`, and ME's own
  USB stack reaches it too. Plugs a few seconds apart, each left until the
  device works, do not.
- **Do not unplug a device while Windows is installing it**: ME's device
  manager stops responding, as on Microsoft's own USB stack.
- **Finish copies to a USB disk before standby or hibernation.** ME stops
  responding when a standby or hibernation begins while a copy to a USB
  stick on the xHCI controller is running.
- **Shut Down can hang with a USB disk not opened since the start.** Open it
  once (a `dir` is enough), or remove it, before shutting down.
- **Shut Down can hang after the controller was disabled and enabled** in
  the same session: save your work before shutting down; if it hangs, switch
  the computer off.
- **A UAS drive plugged in before any ordinary USB stick shows Code 2.**
  Plug in an ordinary stick once, then unplug the UAS drive and plug it back
  in; no Remove and no restart are needed. Setup gives this step when it
  finds such a drive.
- On Windows ME, with XhciDmaStaging set to 1, if a USB controller stops
  answering while Windows is starting and a device on it was plugged in at
  power-on, Windows ME may not finish starting; unplug the device or the
  dock and start again.

### Windows 2000, XP and later

- **2000, XP and XP x64: a USB 3 controller the BIOS gives these systems no
  interrupt for** (some HP laptops of 2012 with the Intel 7 Series
  controller). The driver offers Windows the line the chipset routes the
  controller to, on the listed computers by default (`XhciIntelNoRouteIrq`
  `1`), or starts it without one and polls it, which uses a little more
  processor time. On Windows 2000 on the listed computers the controller is
  always polled. **On a computer whose BIOS turns the controller on before
  Windows starts, Windows 2000 with an ACPI computer type still stops at
  start (STOP 0x000000A5): install it there as "Standard PC" (F7 in text-mode
  Setup).** On the listed computers `XhciIntelNoRouteIrq` `0` brings the stop
  back under an ACPI computer type; if `0` was set after the last start that
  reached the desktop, "Last Known Good Configuration" at F8 undoes it,
  otherwise only "Standard PC" starts Windows 2000 there. On these computers
  the BIOS leaves the USB 3 ports on the USB 2.0 controllers under XP and XP
  x64: `XhciIntelPortSwitch` must be `1` (Setup sets it at a first install;
  after an install by hand or an update it may be `0`) ([tuning.md](tuning.md#an-interrupt-where-the-firmware-gives-none-xhciintelnorouteirq)).
- **2000 from the F6 floppy needs three files from its own CD**, and **XP
  from the F6 floppy may need a PS/2 keyboard**
  ([the F6 floppy](#installing-windows-2000-or-xp-or-using-the-recovery-console-the-f6-floppy)).
- **32-bit Vista and 7: the install asks for a restart**, and the controller
  does not start until it is done (also after a reinstall when Windows does
  not ask).
- **32-bit 2000 and XP: the finer timer** (`XhciPollFineTimer` `1`, off by
  default) starts only at the next boot after the install.
- **Vista x64 and 7 x64 need test-signing mode**, or driver signature
  enforcement disabled at every start; otherwise the controller sits at Code
  39 on Vista, Code 52 on 7, and nothing on it works
  ([test-signing mode](#64-bit-vista-to-10-test-signing-mode)). On 8, 8.1 and
  10 Secure Boot must be off too.
- **Vista and 7: the controller's Advanced tab shows no bandwidth.**
- **2000 to 8.1: a USB Audio 2.0 or 3.0 device that already shows as "USB
  Audio Device" keeps Windows' own USB Audio 1.0 driver**: Windows does not
  choose again for a device it already installed. Unplug the device, restart
  Windows (reset the computer if the shutdown hangs), run
  `cmd /c "set devmgr_show_nonpresent_devices=1&& devmgmt.msc"`, choose View,
  Show hidden devices, update the greyed "USB Audio Device" to `uac2`, then
  plug the device in.

### Every system

- **A device the controller faults on is unplugged and plugged in again by
  the driver:** a file copy to a drive fails part way, and a drive should be
  checked as after any surprise removal; after three on one port the device
  stays gone until it is unplugged
  ([When a device keeps failing](#when-a-device-or-the-controller-keeps-failing)).
- **After the driver closes off a controller it could not prove stopped,
  do not disable the controller (that hangs), and save your work: Windows
  may not be able to shut down or restart afterwards, so if it does not
  finish, switch the computer off at the power button.** Only when the
  controller stopped answering altogether, or a recovery could not stop it,
  and the driver could not prove it had stopped writing to memory (the
  snapshot reads "CONTAINED" and "PINNED"): a later stop of the controller -
  disabling it, or updating or removing its driver - waits for ever on what
  it kept. `XhciDmaStaging` `1` lets such a stop finish
  ([tuning.md](tuning.md)).
- **After a resume that starts the controller afresh, some devices are
  still found again:** a hub and everything behind it, a Bulk-Only drive
  (but for the one `XhciResumeKeepBot` keeps), a drive with no serial
  number or one that could not finish its requests
  before the sleep, a drive whose medium cannot be checked after the wake, a
  device with an audio or video stream open, and a device selective suspend
  held. **A file open on such a drive across the sleep is lost, and cached
  writes with it.**
- **Intel 7-, 8- and 9-series with the switchover on: a device on a blue
  connector is disconnected and found again when the controller starts or
  stops.** Do not have a drive busy there at that moment.
- **Intel 7-series (`8086:1E31`): too many periodic devices at once (audio,
  mice, keyboards, hubs) may lose data instead of being refused**: the
  controller accepts schedules it cannot serve, and this driver relies on
  the controller's own admission.
- **A device with more than one configuration is split by its first one**
  where the split rules allow it, so a manufacturer's driver that takes the
  whole device no longer binds. A list of devices to keep whole can be added
  if one is reported.
- **Selective suspend is off unless you turn it on, and hubs, the root hub
  and the controller are never suspended.**
- **A SuperSpeed device declaring more than 510 mA reads 510 mA** on the
  Power tab; the snapshot has the exact figure.
- **Stor98: a drive with 4 KiB sectors partitioned in one view cannot be read
  in the other.** 98 SE to 7 see it with 512-byte sectors, 8 to 10 with its
  own 4096-byte ones; a drive partitioned on one side sees its partitions and
  files as damaged on the other. Back up first. To move a drive freely,
  reformat it to 512-byte sectors with its manufacturer's tool, or partition
  and format it again on the system that will use it (Stor98's README, "Known
  limits"). **Stor98's other limits** are in `stor98-readme.txt`.
- **Stor98 on 98 SE and ME leaves a drive the BIOS has set up to the BIOS**
  (it then runs in MS-DOS compatibility mode and Stor98's entry shows Code
  10), unless `StorTakeFirmware` is set on 98 SE ([tuning.md](tuning.md)).
  Such a drive behind a Thunderbolt or USB4 port cannot be stopped safely:
  the xHCI98 USB Tree and `INSTALL /console /eject` refuse to stop the port
  ("A drive behind this port runs through the BIOS (MS-DOS compatibility
  mode) and cannot be stopped safely."), and so they do when they cannot
  tell whether such a drive is there. Shut Windows down before you unplug
  it.

### Thunderbolt and USB4: known limitations

- **Standby and hibernation with a Thunderbolt or USB4 drive attached are
  supported on Windows 2000 and later where the drive was connected when
  Windows started or the computer's firmware sets up its USB4 ports again
  at the wake** (on Windows 7 to 10, a drive plugged in after Windows
  started: the next item). On Windows 98 SE and ME, stop and
  unplug the drive first; a drive that was connected when Windows started
  cannot be stopped, so shut down instead. On Windows 98 SE, if a drive on
  a dock stays plugged in during standby, it disconnects when the computer
  wakes and comes back a few seconds later. Before standby, close all files
  on that drive. If a file is still open, the drive may not come back until you
  restart Windows. For important data, eject the drive before standby. On a
  computer whose firmware does not restore the Thunderbolt or USB4
  connection at resume, stop the drive before standby or hibernation.
- **On Windows 7 to 10, standby or hibernation disconnects a Thunderbolt or
  USB4 drive that was plugged in after Windows started**, as if it had been
  unplugged, on a computer whose firmware does not set up its USB4 ports
  again at the wake; any file being written to it is lost. Stop the drive
  before sleeping (Safely Remove, or the xHCI98 USB Tree); after the wake,
  unplug it and plug it in again.
- **Windows ME (and rarely 98 SE): with a Thunderbolt dock or chain attached
  at power-on, the first start after the install can stop with a fatal
  exception 0E.**
- **Approval is automatic by default, and a tunnelled device can reach all
  of memory**: set `XhciCmApprove` to `0` where the port is within reach of
  people you do not trust ([Approval](#approval-and-what-it-lets-a-device-do)).
- **Stop a dock before unplugging it when a keyboard/mouse, tablet, network
  adapter or similar device is on the dock's own USB controller** (on 98 SE
  and ME, and on 2000 to 7 under a firmware connection manager). Stop it in
  the xHCI98 USB Tree or with `INSTALL /console /eject`; Windows' own Safely
  Remove Hardware may not list such a dock. Otherwise Windows may never
  finish removing the dock: it is not found again when plugged back, and
  Windows does not shut down; on Vista and 7, plugging it in again can stop
  the computer responding.
- **2000 to 7: ejecting a dock, or an enclosure with its own PCIe switch,
  stops it safely, but its connection stays up until you physically
  disconnect it**, where the host does not say which port leads to it, or
  where ACPI's bus filter stays on the ejected bridge (seen on 7; the same on
  2000, XP and Vista). Unplug it.
- **98 SE and ME: close every file on a Thunderbolt or USB4 drive, and every
  window showing it, before you unplug it** - or use Safely Remove first,
  which refuses while a file is open. Closing a program that still had a file
  open on an unplugged drive (an MS-DOS Prompt whose copy failed, an Explorer
  window on the drive) can, rarely, make Windows 98 SE's own mouse driver
  (`VMOUSE.VXD`) stop every timed wait in Windows: the drive does not come
  back, no further plug is seen, and on 98 SE the desktop can freeze (the
  pointer still moves). Only a restart recovers.
- **98 SE and ME: a device plugged in after the start needs an interrupt
  from the BIOS** (the ThinkPad P14s Gen 1 gives one). If it stays at Code
  29, attach it at power-on with pre-boot support on (a drive attached at
  power-on is not stopped from the xHCI98 USB Tree: below). Two start
  without one, polled (`XhciCmNoIrqStart`, on by default): an NVMe drive on
  the bundled Stor98 that Setup installed or staged, and a dock's second and
  further USB controllers. While a hardware wizard is left open, the
  Thunderbolt driver's other work on the tunnel ports (plug, removal) waits
  too.
- **98 SE and ME: a Thunderbolt or USB4 drive that was already connected
  when Windows started (the computer's firmware set up its connection)
  cannot be stopped** from the xHCI98 USB Tree or with `INSTALL /console
  /eject`: the stop is refused. Shut down Windows before you unplug it. A
  drive plugged in after Windows started is stopped as usual.
- **98 SE and ME: after Safely Remove of a device with its own PCIe switch,
  unplug it before using it again.** Scan for hardware changes without an
  unplug leaves the drive at Code 29 until it is unplugged and plugged in
  again, or Windows restarts.
- **98 SE and ME, USB4 connection manager: a removal leaves the tunnel up
  until the unplug**, so a Refresh can find the device again; unplug it.
- **98 SE and ME: once the controller has started, `xhcicm.sys` stays loaded
  until Windows restarts**: a disable, update or removal takes full effect at
  the next start.
- **Windows 98 SE: answer No to the restart when an enclosure is first
  plugged in.** When an enclosure is first plugged in after Windows 98 SE has
  started, Windows asks to restart after it installs each of the enclosure's
  PCI bridges. Answer No. If you answered Yes, Windows may skip the
  enclosure's bridge from then on and the drive does not appear. To undo it,
  run asd from Start, Run. Tick "Enumerating a device => PCI standard
  PCI-to-PCI bridge". Click OK. Restart Windows.
- **Windows 2000: the first time a Thunderbolt dock is plugged in, Windows
  asks to restart for each of its PCI bridges**; until the restart some of
  the dock's devices do not start.
- **Windows 2000: the first install of the filter takes the Thunderbolt
  switch's other devices away until the restart** (its upstream port shows
  Code 12 meanwhile). Setup asks for that restart; do it before using the
  controller.
- **2000, XP and XP x64: the first plug of a dock or an enclosure with a
  switch** can stop and start its devices once, a few seconds after the
  plug, when the safe-removal mark service restarts it after its Found New
  Hardware windows are answered. If a drive behind it is already in use, the
  restart is refused: unplug it safely and plug it in again. A device added
  later behind a port that has no I/O range (a network adapter behind a
  chained dock) is placed after the dock is unplugged and plugged in again.
- **Windows 2000 with the "Standard PC" computer type: an NVMe drive plugged
  in behind a tunnel after the start works, but runs polled.** Drives
  attached at power-on, internal NVMe drives and USB drives are not affected
  ([issue 19](../issues/19-w2k-standard-pc-hot-plug-interrupt.md)).
- **2000 to 7: a device tunnelled before the driver started** (by the BIOS's
  pre-boot support), or connected while the package was installed, is
  offered for safe removal only after a restart or a re-plug; the xHCI98 USB
  Tree and `INSTALL /console /eject` stop it before that, as an
  administrator.
- **A USB4 host that stops answering stays stopped until the controller is
  disabled and enabled, or Windows restarts**: if the platform powers it off
  or resets it without telling Windows, the driver reports every device
  behind it gone and leaves it alone. Under a firmware connection manager it
  resumes when the controller answers again.
- **A dual-mode NVMe enclosure (Thunderbolt / USB4, and USB) may come up as
  a USB (UAS) disk instead.** Unplug it and plug it in again to get the
  Thunderbolt or USB4 connection.
- **32-bit Windows 10 before version 2004, on a board that leaves the USB4
  ports' memory windows above 4 GB** (some do even with "Above 4G Decoding"
  off): XHCIPFW is not offered there, and the devices behind such ports
  cannot be used.
- **32-bit Windows 8 and 8.1: a PCI root bus that names XHCIPFW.SYS's filter
  with its file gone stops Windows at the start.** Setup takes the filter off
  every root bus before it removes the file. If the file was deleted or
  damaged by other means, System Restore may recover such a start on 8;
  `tools\xhciinst\readme.txt` gives the manual route from the recovery
  environment's registry editor.

## Licensing

This driver's own source is under the GNU General Public License, version 2
or later (`GPL-2.0-or-later`); see `LICENSE`. The full third-party material and provenance
record is `docs/contributing/legal-provenance.md`.

`xhci98.sys`, `xhciuas.sys`, `xhcicm.sys` and their INFs are this
project's own work. A download that bundles Stor98 carries it built from
this project's source tree but under its own licence (BSD-3-Clause), with
its licence and notice beside it ("NVMe drives behind Thunderbolt or
USB4"). The `usbd.sys` and `usbui.dll` the install needs are not in the
download: they are Windows' own and are copied by Windows from your own
installation source. The base and `-free` downloads' only Microsoft files
are NUSB 3.3's seven Windows 98 SE storage files ("Windows 98 SE and
ME: the stock install, and storage"; the `-nonfree` download adds
more, below), unmodified, in a folder of their own with a
`NOTICE.md` saying whose they are; they are not this project's work and are
under no licence of this project's, and no licence to redistribute them has
been established. A download built without them carries no Microsoft file,
and its `readme.txt` says so. On Windows 98 SE, `xhciuas.inf` names
`USBNTMAP.SYS` as a filter on the UAS device.

A release has up to three downloads: `xhci98-<version>.zip`;
`xhci98-<version>-free.zip`, which adds the usb-wdm-free USB class
drivers in `USBDRV\FREE\` (experimental: they may not work with every
device and can make Windows unstable; their problems go to that
project) and the CamCheck webcam preview tool in
`USBDRV\FREE\tools\camcheck\`; and `xhci98-<version>-nonfree.zip`, which
adds other manufacturers' drivers from usb-wdm-nonfree in `USBDRV\NONFREE\`, not
this project's work (NUSB 3.3's storage files are in
`USBDRV\NUSB33\` of every download that carries them). Download 3 has the
manufacturers' Ethernet, HID, serial and audio drivers. Wi-Fi
drivers are not in this download. Get them from the
[usb-wdm-nonfree](https://github.com/yeokm1/usb-wdm-nonfree) release.
`USBDRV\NONFREE\` holds more Microsoft files, the KMDF co-installers
ASIX and Davicom ship with their drivers (`WdfCoInstaller01007.dll` and
`WdfCoInstaller01009.dll`, in several folders),
unmodified, with no licence to redistribute them established either.
It also carries TASCAM's US-122 driver 3.24 for 98 SE and ME
(`USBDRV\NONFREE\audio\us122\w98se-me\`), which TASCAM no longer
offers. It carries no licence of its own, and no licence to
redistribute it has been established (`legal-provenance.md` section 5).
And it carries Microsoft's USB smart card reader driver for 2000 and XP
(`USBDRV\NONFREE\smartcard\usbccid\`), unmodified from the Microsoft
Update Catalog. Its cabinets carry no licence, and no licence to
redistribute it has been established either.
The installer offers each set the download carries on a page of its
own after Components, unticked: "Free USB drivers" and "Non-free USB
drivers". Ticked, the set is copied to the install folder
and nothing is installed, and a box per driver package, grouped by kind
and unticked, lets Windows use that package's drivers at a
device's first plug (on 98 SE and ME while `DevicePath` stays within the
system's limit, "Known limitations"). A
set the download does not carry has no page; the Finish page names the
download and the address that have it. The uninstall removes what it
placed.

## Trademarks

Xbox is a trademark of Microsoft Corporation. It names the controllers the
free driver set's `xpad98` game pad driver is for; neither this project
nor that driver is Microsoft's, and no Microsoft name or logo is part of
any product name here.
