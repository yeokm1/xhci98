# xhci98 - Release notes of past releases

The per-release sections of `docs/using/release-notes.md` as they stood at
`3.4.3.0` (2026-11-10), moved here unchanged when the release notes were
shortened (owner, 2026-11-12). They describe each release as it was
published, with the readings of the time; where a later release changed
something, the current release notes and `releases/history.md` are right
and this file is not. Section names quoted below are the release notes'
names of that time.

## What `3.4.3.0` changes

The full list: the `3.4.3.0` entry of `releases/history.md`.

### USB
- Windows 2000: USB audio plays again. `3.4.2.0` refused the request
  with which Windows 2000's own USB audio driver chooses its stream,
  because the request's header gave a length of 0, so no sound played
  ("Your audio hardware cannot play files like the current file";
  GitHub issue 32, `docs/issues/30-w2k-usbaudio-select-interface-header.md`).
  A request whose header states a length too short for it is now served
  and counted, as Windows' own USB stack serves it.
- A USB 3 device whose SuperSpeed link keeps failing (three resets
  within a minute, then a fourth failure), at a root port or behind a
  USB 3 hub, is moved to the USB 2.0 side of the same connector or hub,
  where it runs at High Speed; it was reset again and again
  (`XhciSsFlapFallback`, on by default; `0` leaves the device off until
  it is plugged in again).
- `XhciDmaStaging`, `0` by default: `1` passes every transfer's data
  through memory of the driver's own, so that a disable, update or
  removal of a controller that has stopped answering completes instead
  of waiting, at the cost of one more copy of the data.
  `XhciStagePoolKiB` sets the size of that memory
  (`docs/using/tuning.md`).
- An audio or video request whose last packet failed part-way ends at
  once, where it could wait for its cancellation; a USB 2.0 endpoint
  whose maximum packet size its speed does not list is opened, where it
  was refused.
- The USB devices answer Windows' bus information request as Windows'
  own USB stack does.

### Thunderbolt and USB4
- Standby and hibernation with a Thunderbolt or USB4 drive attached are
  supported on Windows 2000 and later: the USB4 connection manager puts
  the devices to sleep in order and builds their connections again at
  resume (`XhciCmSleepEntry`, on by default). On 98 SE and ME, stop and
  unplug the drive first ("Thunderbolt and USB4: known limitations").
- 98 SE and ME: a Thunderbolt dock's second and further USB controllers,
  which Windows leaves at Code 29 for want of an interrupt, now start
  without one, `xhci98.sys` polling them (`XhciCmNoIrqStart`), from the
  dock's first plug.
- 98 SE and ME: `XhciCmRingInterrupt` `1` works on these systems too,
  with `XhciPfwNhiLine` `0`: the controller's interrupt reports a plug,
  an unplug and network traffic at once (off by default; "Thunderbolt
  networking speed on 98 SE and ME: the controller's interrupt",
  `docs/using/tuning.md`).
- USB4 hosts whose tunnels xHCI98 builds: on 98 SE and ME a device in a
  daisy chain (behind another device) is set up when it is plugged in,
  removed when it is unplugged, and marked removable; on 2000 to 10
  stopping it closes its own connection.
- USB4 hosts whose tunnels xHCI98 builds, where the host does not tell
  the driver which of its ports leads where (the ASMedia ASM4242): for a
  device already attached when Windows starts, `XhciCmInferAtStart` `1`
  lets the driver work out its port when only one such device is
  attached and two separate readings agree, so that safely removing it
  closes its connection; `0`, the default, leaves that connection up
  until the device is unplugged. The port of a tunnel found up at start
  is learned only from the tunnel's own far end, so a function that
  answers late is never taken for the device.
- When a tunnel stays up after an eject because the driver does not know
  the device's port, the eject says so: "The PCIe tunnel stays up until
  the device is unplugged: the Thunderbolt driver does not know which of
  its ports the device is on. It learns that when a device is plugged in
  while Windows runs."
- Windows 8.1: a tunnelled NVMe drive keeps its Thunderbolt id, and so
  Stor98, across a disable, a driver update or another restart of its
  own devices; on 8.1 Stor98 matches a drive by that id alone.

### Networking
- 98 SE and ME with the controller's interrupt on: Thunderbolt
  networking no longer stops Windows with a fatal exception 0E inside
  its TCP/IP (MSTCP) while sending, and data coming in no longer stalls.
  The network driver now hands Windows what it receives and the sends
  it completes from an NDIS timer, as these systems expect.
- With the controller's interrupt on, network traffic is reported at
  once (`XhciCmNetInterrupt`, on by default); the driver still looks at
  every clock tick, and only that if the interrupt fails.
- Thunderbolt networking with a Windows 11 computer: traffic passes
  where the link came up and carried nothing, and the link comes back
  after the cable is plugged in again at the Windows end. Each new peer
  is told to read this computer's properties again, as Linux does.
- Thunderbolt networking on 98 SE and ME: two tips in
  `docs/using/tuning.md`. "Thunderbolt networking speed on 98 SE and
  ME: the TCP receive window" sets the systems' own TCP receive window
  (`DefaultRcvWindow` `64240`), which can take data coming in from about
  10 to about 100 Mbit/s; it is 98 SE and ME's own setting and applies
  to all their networking, and the tip says how to undo it. "Thunderbolt
  networking speed on 98 SE and ME: the controller's interrupt"
  (`XhciCmRingInterrupt` `1` with `XhciPfwNhiLine` `0`) makes it about
  three times faster in both directions, where the BIOS gives the
  controller an interrupt no other device holds.

### Storage
- Stor98 on 98 SE: `StorTakeFirmware` `1`, off by default, takes an NVMe
  controller the BIOS has set up, which Stor98 otherwise leaves to the
  BIOS (its drive then runs in MS-DOS compatibility mode and Stor98's
  entry shows Code 10); `stortakefirmware=1` in one controller's
  `AdapterSettings` takes that one alone. Taking it can hang Windows'
  start or corrupt data if the BIOS still uses the drive. Ignored on ME
  (`docs/using/tuning.md`; Stor98's README, "Take a controller the BIOS
  set up").

### Installer and package
- A reinstall or update at the same version replaces every installed
  file of this package whose bytes differ from the package's, on every
  system, as it replaces an older file; a newer version is still kept
  unless `/force` is given. `/list` says "Keep (same version)" or
  "Replace files (same version)".
- 98 SE and ME: after a Stor98 update, and on 98 SE after the storage
  drivers' copy with a UAS disk at Code 2, Setup offers "Restart Windows
  now" and ends with "Done. Please restart Windows now.", as on every
  other system, where it asked you to shut down and start Windows again.
  Disconnect your external NVMe drives before a Stor98 update, as before.
- 32-bit Windows 10, version 2004 or later: Setup offers `XHCIPFW.SYS`,
  which closes PCI bridges' windows above 4 GB, as on 8 and 8.1. On 8,
  8.1 and 10 it acts only at a full start: restart Windows after
  installing or updating it (with fast startup on, Shut down and power
  on is not a full start).
- Setup's Advanced lists and the USB Tree's Settings window show
  `XhciSsFlapFallback` and Stor98's `StorTakeFirmware` (98 SE; greyed on
  ME), and `XhciCmRingInterrupt` on every system.
- Setup's refusal while Windows is installing something names the
  program to run again, `SETUP.EXE` or `INSTALL.BAT`; on 98 SE and ME
  `INSTALL.BAT` waits for Setup's window and returns its exit code.
- The text-mode Setup (F6) entry for Windows 2000 also serves 32-bit XP:
  "xHCI98 USB 3 Controller (2000/XP, USB keyboard/mouse, files from CD)".
- The USB Tree: USB drive notices, off unless you tick "USB drive
  notices" on the Settings window's xHCI98 USB Tree tab, say when a USB
  drive is connected or removed. A Thunderbolt networking node under the
  Thunderbolt or USB4 controller shows each link's state, the other computer, the speed, the
  addresses and the counters, also in the saved report and the
  snapshot. `/eject` with the ID of a device inside a Thunderbolt or
  USB4 device (its drive, say) stops that whole device.
- XHCIPERF, the developer diagnostic in the source tree: on 98 SE and ME,
  which cannot cancel a request sent, Stop and closing the window wait
  for the disk to answer it.

## What `3.4.2.0` changes

The full list: the `3.4.2.0` entry of `releases/history.md`.

### USB
- 2000, XP and XP x64: a USB 3 controller the BIOS gives no interrupt
  (some HP laptops of 2012 with the Intel 7 Series controller) is
  offered the line the chipset routes it to, on the listed computers by
  default (`XhciIntelNoRouteIrq`), or polled, where XP stopped it at
  Code 12; on Windows 2000 on the listed computers it is polled, where an
  ACPI computer type stopped at start with STOP 0x000000A5 ("Known
  limitations").
- ME: unplugging a USB audio device while Windows' own USB audio driver
  plays it no longer stops Windows (98 SE: "Known limitations").
- An endpoint stop the controller undoes is repeated, and where it does
  not hold the device alone is reconnected: a UAS drive's refused SMART
  read could leave its endpoint waiting for good, its volume lost and
  Windows frozen.
- A larger event ring (`XhciEventRingTrbs`, 1,024 entries) and eight
  requests in flight on an audio or video stream, where there were four;
  the USB Tree's snapshot times each such stream.
- 98 SE and ME: a device unplugged just before Windows starts its driver
  is answered until Windows removes it.
- A port whose device reports a cold attach and never trains is reset
  three times, then left.
- The snapshot notes the BIOS handoff's USB legacy-support registers.

### Thunderbolt and USB4
- `XhciCmPreOsCleanup` `2`: a second start of the driver within two
  minutes in one boot keeps the tunnel its first start rebuilt, where it
  is wholly the driver's own; a chained device is still cleaned.
- The USB Tree names a Thunderbolt 1 or 2 router as such, and says when
  the ASM4242 handover was not sent for a firmware build it does not
  know (`XhciCmAsm4242Structural` `0`).
- Improved Thunderbolt networking compatibility.

### Storage
- Stor98: 4 KiB-sector drives work on every system, with 512-byte
  sectors on 98 SE to 7 and their own 4096-byte sectors on 8 to 10
  ("Known limitations").
- Stor98: VERIFY reads the drive; READ CAPACITY(16)'s short form is
  answered, so a drive above 2 TiB has its size on 7.
- Stor98 on 98 SE and ME leaves a drive the BIOS set up to the BIOS
  also with its memory access off; on 2000 to 10 a restart of Stor98 on
  a drive no longer clears its queues before the drive is seen stopped.
- UAS, per drive, off (`0`) by default: `XhciUasNoAtaPassThrough` `1`
  answers ATA and SMART requests in the driver, and `XhciUasFastReset`
  `1` resets a drive whose commands keep timing out without progress at
  the second timeout (`docs/using/tuning.md`).
- A drive that offers UAS and Bulk-Only, used as Bulk-Only (on a
  controller that cannot stream, or with `XhciForceBulkOnly`), has a
  Device Manager entry of its own for that, which Windows installs with
  its own Bulk-Only driver, so it no longer stops at Code 10 where its
  entry was installed with the UAS driver (`XhciBulkOnlyOwnId`,
  "SuperSpeed and UAS").

### Installer and package
- XP x64 and Server 2003 x64: Setup copies `usbui.dll` from Windows' own
  files when it is missing, for the Advanced and Power tabs; answer
  Windows' "Windows Logo testing" box with Continue Anyway.
- 98 SE: the uninstall also removes Windows Setup's records of the INFs
  it placed and the removed USB disks' entries.
- The Free USB drivers page's notice, the Progress page's wait line and
  "Shut down now" reworded; `/uninstall /list` changes nothing.
- The USB Tree: Save Firmware Tables takes the DSDT from Windows'
  registry copy on XP x64 and never saves `MSDM` or `SLIC` (the
  computer's Windows licence data); times on a processor whose
  time-stamp counter is not invariant are marked approximate;
  `XHCITR.EXE` on 64-bit Windows starts `XHCITR64.EXE`; the snapshot
  lists each UAS drive's last failed commands; a UAS drive at Code 10 on
  a controller that cannot stream has a note in its details ("SuperSpeed
  and UAS").
- The USB Tree's Settings window (View > Settings..., or the icon's
  menu) shows and sets the drivers' registry settings as Setup offers
  them, for every controller or one; `XHCITR /set` does the same from a
  command line. Quick save, on the icon's menu or an optional hotkey,
  saves the report and the snapshot with no questions ("The xHCI98 USB
  Tree").
- XHCIQUAL: `--edd` (see "EDD reports", `docs/using/edd-reports.md`);
  no USB 2 ports a caveat, not a disqualification.

### Other drivers
- usb-wdm-free 0.1.9: see its release notes
  (https://github.com/yeokm1/usb-wdm-free/releases).
- usb-wdm-nonfree v1.3.0: Microsoft's USB smart card reader driver
  (usbccid) for 2000 and XP, copied only; point Windows' hardware wizard
  at its folder.

## What `3.4.1.0` changes

The full list: the `3.4.1.0` entry of `releases/history.md`.

### USB
- 98 SE and ME: the USB Tree's snapshot names a device that holds a
  controller's shared IRQ at its start (its PCI location and ids).
- 2000 to 7: one stream error no transfer was waiting for now
  recovers the UAS disk's endpoint; if that fails, only the disk is
  reconnected, never every device on its controller (issues 22 and 24).
- A command the controller never answers is named in the snapshot.

### Thunderbolt and USB4
- 98 SE and ME: on an ASMedia ASM4242 USB4 host whose firmware hands
  USB4 over only through ACPI `_OSC`, the driver sends that handover itself
  when it knows the firmware (`XhciCmAsm4242Handover`).
- 98 SE and ME: the driver stops writing to the registry when Windows
  announces a shutdown or standby.
- 2000, XP and XP x64: the ASM4242 handover too, with
  `XhciCmAsm4242Reads` `1`.
- Eject: the confirmation names the drive; "It is now safe to
  disconnect the device."
- The USB Tree names AMD's other USB4 hosts.

### Storage
- 98 SE and ME: Stor98 runs only the drive it was loaded for. It took
  every NVMe drive before, Windows' own NVMe system disk included, and
  the start could stop at "Initing stor98.mpd".
- 98 SE and ME: a drive the BIOS has set up stays the BIOS's. Its Stor98
  entry shows Code 10.
- Stor98 polls a drive whose interrupts stop arriving, until the drive
  next starts (`StorIntWatchdog`, `storintwatchdog=`), also on an IRQ
  shared with another device.

### Installer and package
- 98 SE and ME: an ASM4242 USB4 host is found while Windows has not yet
  listed its router, and the Thunderbolt and USB4 box is ticked.
- Settings... on the Components page holds the USB settings. The
  Thunderbolt and USB4 page has its own, for Thunderbolt / USB4 and
  Stor98.
- The Progress page's bar moves with the work done.
- The installer's texts reworded: every page, Help, the console and its
  readme. Stor98's Advanced shows `StorMaxTransferKB` up to 2048 KiB on
  98 SE and ME.
- The USB Tree's Save Snapshot asks only for a folder, then says where
  the files went.
- 8, 8.1 and 10: the UAS disk driver is no longer installed.
- Updating on 2000 to 7 updates UAS disks that are not plugged in;
  on 98 SE and ME it no longer asks for the driver disk.
- Setup's log lines carry the time.

### Other drivers
- usb-wdm-nonfree 1.2.0: TASCAM's US-122 audio and MIDI interface on 98
  SE and ME.
- usb-wdm-free 0.1.8: `uac2` pops far less under CPU load, never
  turns a device's volume above 0 dB (`Uac2VolAboveZero`), and
  names each device by its own product name.

## What `3.4.0.0` changes

The full list: the `3.4.0.0` entry of `releases/history.md`.

### USB
- Devices on the computer's own ports kept across a resume that restarts
  the controller (`XhciResumeKeep`).
- A device that comes back with its serial number unreadable is no
  longer taken for a new one: if retries fail, it stays out until
  plugged in again.
- USB Audio 2.0 and 3.0 kept from Windows' USB Audio 1.0 driver (2000 to
  8.1); devices with several configurations split where eligible.
- Memory integrity's rules met on 8 to 10; smaller fixes throughout.
- 98 SE: a device with no working driver holding a shared IRQ slows or
  freezes Windows ("Known limitations").
- 98 SE and ME: rarely a start stops with "A fatal exception 0E has
  occurred at 019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170"
  (ME); turn the computer off and on.

### Thunderbolt and USB4
- 2000, XP and XP x64: every port of a hot-plugged dock placed
  (`XhciCmBridgeReq`); a first NVMe enclosure brings two wizards
  ("Thunderbolt and USB4: known limitations").
- Docks with an input device on their USB controller removed cleanly on
  2000 and XP under a USB4 host xHCI98 runs; elsewhere stop them first.
- Daisy chains behind such a host; on 2000's Standard PC HAL a drive
  chained after startup gets Code 10.
- A dock plugged in after startup needs bus numbers the BIOS reserves
  for the ports; without them, Code 31 on 7 and nothing behind the port
  on XP or 98 SE.
- USB4 version 2 routers run as version 2, Gen 4 links and their
  recovery included (`XhciCmUsb4V2`, `XhciCmGen4LinkRecovery`).
- Vista and 7 x86: a dock's USB controller starts at its first plug
  without a restart; `XhciCmRingInterrupt`; the ASM4242 named by its
  INF.
- A drive attached during the install is listed in Safely Remove after a
  restart or replug ("What `3.1.0.0` changes").
- XHCIPFW on 32-bit 8 and 8.1 (the service `xhcipff`); the installer
  readme's recovery steps reach the registry editor from the setup DVD's
  Repair your computer.
- 10 x64: waking from Sleep no longer stops Windows with 0x9F.

### Networking
- Thunderbolt networking (new): two computers on one Thunderbolt or USB4
  cable, in Linux's frame format; one link, not through a dock.
- SETUP's Thunderbolt and USB4 page has a Thunderbolt networking box,
  unticked by default (`/tbnet`, `/notbnet`); an update keeps the choice
  and the uninstall removes the adapter. The driver is in each folder's
  `cm\tbnet\`. On 98 SE the adapter's first install may ask for
  `xhcinet.sys`: type `C:\XHCI98`.

### Storage
- UAS drives answer pass-through and SMART (2000 to 10) and stop on
  their own in Safely Remove (2000 to 7); Stor98 on 8.1 fixed.
- A UAS drive behind a SuperSpeed hub is kept across the hub port's warm
  reset.

### Installer and package
- One folder per Windows family (`98-me\`, `nt-x86\`, `nt-x64\`,
  `INTEGRATE.TXT`); one build, `/debug` gone.
- A Thunderbolt and USB4 page, full Advanced lists, Help and F1. 98 SE
  and ME: more packages staged; a controller "not reached in this run"
  needs a restart and another run.
- The install writes each setting the drivers read at its default, only
  where it is missing; on 98 SE and ME the uninstall removes Stor98's
  settings, so a later install starts clean.
- The USB Tree's title shows its version.
- The `nt-x64\` folder goes into a Windows PE or Setup `boot.wim` with
  DISM, as well as `install.wim`; on XP x64 with no other USB driver, the
  controller has no Advanced tab and the root hub no Power tab.

### Other drivers
- usb-wdm-free 0.1.7: `usbprn` (printing, 98 SE and ME), `uac2` 0.1.6.0,
  `usbcom` 0.1.5.0 (FTDI, Prolific, Silicon Labs bridges); `rtl815x`
  beside the non-free one on XP to 7.

## What `3.3.0.0` changes

What `3.3.0.0` changed from `3.2.0.0` (roadmap-usb4 Phase 44; its entry in
`releases/history.md` is the short list). Every item was read in QEMU
virtual machines, the Thunderbolt 3 and USB4 ones on the `usb4-emu`
emulator's hosts and enclosure, unless it says otherwise; the records are
`docs/contributing/runs/run-44.md`, under each task's number. Every system
`3.2.0.0` supports is supported.

- **The connection manager is set back to start on demand** (roadmap-usb4
  tasks 44.32 and 44.12; design record 23 section 5.14). Windows makes it
  a boot driver when it puts its filter on the controller's PCI ports, and
  the installer sets it back at the end of each run (the safe-removal mark
  service after an install the installer did not run). On 64-bit Vista to
  10 a start without test-signing mode then leaves the Thunderbolt and
  USB4 devices at Code 52 (Code 39 on Vista) and the rest of the install as
  it was, where under `3.2.0.0` such a start stopped 64-bit 8 in Automatic
  Repair and rolled the install back on 7 x64. It stays a boot driver where
  Windows starts from a disk behind a port it filters. Keep test-signing
  mode on all the same ("What this is"). On Vista x64 and 7 x64 the
  installer now installs the Thunderbolt 3 / USB4 support only in
  test-signing mode, as on 64-bit 8 to 10.
- **Stor98 for drives behind a tunnel only** (task 44.3; design record
  31). The connection manager gives such a drive an id of its own, and on
  2000 to 8 Stor98's INF matches only that id, so Windows never offers
  Stor98 for an internal NVMe drive. On 8.1 and 10 Windows' own `stornvme`
  keeps an internal drive. On 98 SE and ME Windows may still offer it for
  an internal drive with no driver: decline it. An internal drive that
  already runs Stor98 keeps it ("NVMe drives behind Thunderbolt or USB4").
- **Stor98's settings named alike on every system**, each name starting
  with Stor: on 2000 to 10 the `DriverParameter` keys are renamed, and 98
  SE and ME gain `StorGoneMs`, `StorWriteIdleMs` and `StorHold9xMs`. A key
  of an old name is ignored after the update: rename it and keep its value
  ("NVMe drives behind Thunderbolt or USB4" lists them all).
- **Windows 8, 8.1 and 10: Secure Boot and Intel's Thunderbolt driver**
  (tasks 44.25 and 44.28). Where Secure Boot is on, the installer refuses
  a run with the USB drivers, and on 32-bit Windows one with the
  Thunderbolt 3 / USB4 support, before any change; on 64-bit 10 it says
  when memory integrity is on. Where Intel's Thunderbolt driver runs the
  controller, the Thunderbolt 3 / USB4 support is unticked; ticked, it
  replaces Intel's driver after a warning, and the uninstall gives the
  controller back to it. Read on a UEFI guest of 10 x64 with Secure Boot
  on, and with Intel's DCH Thunderbolt driver on the emulated controller.
- **Windows 8, 8.1 and 10: the uninstall of the USB drivers in one run**
  (tasks 44.22 and 44.24), read on 8 x86, 8.1 x86 and 10 x86 and x64; the
  installer no longer says it may need a restart and a second run.
- **The ASMedia ASM4242 USB4 host** (tasks 44.39 to 44.42): the
  connection manager finds the PCI ports its tunnels arrive on, so a
  tunnelled drive there is treated as one behind any other USB4 host.
  Read on the emulator's ASM4242 host; not yet read on real hardware.
  `XhciCmAsmediaPcie7Credits` is a bench switch for that host, absent by
  default ("The connection manager's registry values").
- **XHCIPFW** (tasks 44.49 and 44.72; design record 32): a small part of
  the install that runs at Windows start, `XHCIPFW.VXD` on 98 SE and ME
  and `XHCIPFW.SYS` on 32-bit 2000 to 7. Where the firmware leaves the
  USB4 ports' windows above 4 GB it closes them, so Windows finds the
  devices behind them. It is a choice under the Thunderbolt 3 / USB4 box,
  ticked with it, and can be unticked. It is not offered on 32-bit 8, 8.1
  and 10 ("Thunderbolt and USB4: known limitations"). Read on the
  emulator's ASM4242 layout.
- **A tunnel this driver built, built again after a hibernation** (task
  44.26; design record 22 section 9.4), also when Windows had restarted
  the controller's driver in between; the drive was missing after the
  resume. Standby or hibernation with a tunnelled device attached is still
  not supported.
- **98 SE and ME: a tunnelled NVMe drive with no interrupt starts** (task
  44.30; design record 28 section 12), Stor98 polling it, when the
  installer installed or staged the bundled Stor98 (`XhciCmNoIrqStart`,
  on by default). The install now writes `XhciCmBridgeNumbering` and
  `XhciCmNoIrqStart` at their default `1` where they are missing (task
  44.46); `INSTALL /set` sets both.
- **Windows 2000 with the Standard PC HAL: a tunnelled NVMe drive plugged
  after the start gets an interrupt** from the connection manager and
  starts on Stor98, which runs it polled (task 44.53;
  [issue 19](../issues/19-w2k-standard-pc-hot-plug-interrupt.md)); it was
  left at Code 10.
- **98 SE and ME, USB4: a tunnelled drive that drops off with the whole
  USB4 controller comes back** with its files once the controller returns,
  with no restart (task 44.54; design record 21 section 6.1.2). While the
  controller is away the drive stays listed and its reads and writes fail.
  Read on the emulated ASM4242 host.
- **Fixed: Windows 98 SE and ME could hang at Shut Down or Restart** with
  a USB4 enclosure attached and the connection manager running (tasks
  44.43 and 44.47; design record 21 section 5.6).
- **Fixed: on 98 SE and ME Stor98 looked for its drive on the first 16 PCI
  buses only**; it now looks on every bus, as a drive behind a USB4 port can
  be (on the emulator's ASM4242 those ports have buses 19h to 78h).
- **Fixed: on Windows 2000 and XP the first plug of an enclosure never
  seen before could leave its drive without a driver** after its first
  Found New Hardware wizard (task 44.52); the safe-removal mark service's
  restart of the enclosure now sets the drive's install to run again, so
  Windows offers Stor98 for it.
- **Fixed: on Windows 2000 a controller's restart, disable or update no
  longer fails with Code 31** until a restart when Windows' own EHCI
  driver, or a second controller on this driver, starts after it (task
  44.81; [issue 20](../issues/20-w2k-hcd-link-collision-code-31.md)). The controller's name on Windows 2000 and later is now
  `\DosDevices\XHCI98HC<k>`, with an `HCD<m>` alias on 2000, XP and XP x64
  ("Known limitations" has what an update from `3.2.0.0` may still show).
- **Fixed: on Windows 2000 the update over `3.2.0.0` with the
  Thunderbolt 3 / USB4 support could stop** with "XHCIMARK.EXE could not
  be put into ... (error 5)" (task 44.76); the installer now waits for the
  old service to end.
- **Fixed: on 98 SE a leftover `xhci98.tmp` could replace `xhci98.sys`**
  at the start after a controller was added (task 44.13); the installer
  deletes such a leftover. `INSTALL.BAT` refusing too many arguments exits
  with errorlevel 1 on 98 SE and ME (task 44.4).
- **The installer's pages** (tasks 44.7 and 44.10): the Other USB
  drivers page is two pages, "Free USB drivers" and "Non-free USB drivers",
  each shown only when the download carries that set, and the Welcome page
  shows more of the device list. On 98 SE the free `uac2` and `usbmidi`,
  and on ME `uac2`, can again be ticked for Windows to use at a device's
  first plug (task 44.11). On 98 SE and ME the installer puts `stor98.mpd`
  in place before the first tunnel drive is plugged, so Windows no longer
  asks for the "Stor98 Storage Controller Disk". An install of the
  connection manager alone is named "xHCI98 Thunderbolt 3 / USB4 connection
  manager" in Add/Remove Programs on 98 SE and ME (task 44.60). Updating
  from `3.2.0.0` with an ASM4242 controller, the first run leaves Stor98 on
  a tunnel drive it already runs and says to run the installer again after
  the restart (task 44.77).
- **Names: "USB 3", not "USB 3.x"** (tasks 44.5, 44.55 and 44.61): "xHCI98
  USB 3 Root Hub", "xHCI98 USB 3 Hub", the service, the F6 floppy's list,
  the USB Tree's port counts and the installer's title, pages and
  Add/Remove Programs entry ("xHCI98 USB 3 drivers"). On 98 SE and ME the
  controller's Properties window names its maker and product, and Device
  Manager names many more controllers ("Controller names in Device
  Manager").
- **The xHCI98 USB Tree** (tasks 44.8, 44.9, 44.35, 44.62 and 44.80; "The
  xHCI98 USB Tree"): a USB4 device on a Thunderbolt 3 controller reads
  "USB4 v1.0 device, running as Thunderbolt 3"; the status bar says when a
  driver update waits for a restart; a USB storage device shows whether it
  runs UAS or Bulk-Only and why; a new item, "PCI and bridge
  diagnostics"; the "can run faster" notice no longer asks for more than
  the computer gives; and a new option, "Watch Windows' own USB ports
  too", off by default. XHCITR `3.2.0.0` cannot take a snapshot from this
  driver on Vista and later; use this release's.
- **Stor98 reports the SCSI status GOOD** for a request it answers itself
  successfully (task 44.58); on Windows ME a program using ASPI could read
  a target status of 05h after another such program had run.
- **USB4 hosts with a drive attached at power-on whose host names no tunnel
  port to it**: the connection manager no longer reads the tunnel ports
  every 200 ms for as long as Windows runs; it tries for a minute and then
  stops (task 44.57).
- **The other USB drivers** (tasks 44.34, 44.44, 44.68, 44.70, 44.71 and
  44.73): the `-free` download carries usb-wdm-free 0.1.4 and the
  `-nonfree` download usb-wdm-nonfree 1.1.0's Ethernet, HID and serial
  drivers, without its Wi-Fi drivers ("Licensing" and
  `releases/history.md` list what each adds).
- **The licence**: this project's own source is under GPL-2.0-or-later
  (task 44.38); every release up to `3.2.0.0` was GPL-2.0-only. Stor98
  stays BSD-3-Clause.
- **`XHCICLN` dropped** (task 44.82): the cleanup tool is no longer in the
  download; the downloads from `2.4.0.0` to `3.2.0.0` still carry it.
- **Known limitation, kept: the rare stall on 98 SE and ME** after closing
  a program that had a file open on an unplugged Thunderbolt or USB4 drive
  was read to be Windows 98 SE's own mouse driver, `VMOUSE.VXD` (task
  44.29; "Thunderbolt and USB4: known limitations").

## What `3.1.0.0` changes

What `3.1.0.0` changed from `3.0.0.0`, as written when it was cut. Later
releases changed some of it, and where they did the item says so or "NVMe
drives behind Thunderbolt or USB4" and "Targets and their standing" have
the current state.

- **The x64 package's filter INF is in the main folder.** In
  `release-x64\` and `debug-x64\`, `xhcicmf.inf` now sits beside
  `xhcicm.inf` and `xhcicm.sys`, with no `nt\` subdirectory; the x86
  package keeps `nt\` (never point Windows 98 or ME at it).
- **`XHCISNAP` removed.** `XHCISNAP.EXE`, `SNAPON.BAT`, `SNAPGET.BAT`
  and `SNAPOFF.BAT` are no longer in the package (`tools\xhcisnap\` is
  gone), and the xHCI98 USB Tree (`XHCITR.EXE`, `XHCITR64.EXE`) does
  what they did ("The log, and how to send one"):
  - The field report, `SNAPGET.BAT` (`XHCISNAP -report`): File > Save
    Report... or `XHCITR /report <file>`, whose report now ends with a
    "Snapshot" part for each xHCI controller `xhci98.sys` drives - the
    device report the `.DEV` held and the decoded dump the `.TXT` held,
    the dump left out at level 4, where it can hold internal addresses.
    That one file is the bug report. The `.DEV`, `.TXT`, `.BIN` and
    `.PSC` files themselves: File > Save Snapshot... or
    `XHCITR /snapshot [NAME]`, with the same names and exit codes.
  - `SNAPON.BAT`, `SNAPOFF.BAT` and `XHCISNAP -verbosity`, `-on`,
    `-disable`: View > Logging level... or `XHCITR /setlevel xhci98=<0-4>`
    (both in `3.0.0.0` already), then a restart.
  - `XHCISNAP -dump` and `-o NAME`: `XHCITR /dump [/o NAME]`.
  - `XHCISNAP -probe`: `XHCITR /probe`.

  `xhci98.sys` is unchanged by it: Windows' own property tabs and the
  tool read it through the same door. Where a section below names
  `XHCISNAP`'s report, read the snapshot in the tool's report: the same
  text, from the same code.
- **The Thunderbolt 3 / USB4 support is experimental, and never chosen
  for you.** Its box on the Components page reads "Thunderbolt 3 / USB4
  support (experimental)" and is unticked whether or not a controller is
  found; the line under it says whether one was found, and its name.
  Tick it on purpose; the console asks with Enter for N, `/yes` leaves
  it out, and `/thunderbolt` includes it. The connection manager alone
  (USB drivers unticked, and Windows 8 and 8.1) is unchanged. An update
  over an install that has the Thunderbolt driver leaves that driver as
  it is unless you tick the box.
- **Stor98 comes with the Thunderbolt 3 / USB4 support.** Ticking the
  Thunderbolt box ticks the "Stor98 (hot-pluggable NVMe driver)" box under
  it, which you can untick;
  in the console `/thunderbolt` includes Stor98 too (asked, Enter for Y;
  unasked with `/yes`), and `/nostor98` leaves it out.
- **Windows 98 SE and ME: Stor98 is updated as a file.** An older
  `stor98.mpd` is no longer replaced by reinstalling the drive while
  Stor98 runs it, which stopped Windows 98 SE with "fatal exception 0E"
  when an update ran with a Thunderbolt or USB4 enclosure attached. The
  installer writes the new file beside the old one and has Windows swap
  them at the next start (`WININIT.INI`), refreshes the copy of
  `stor98.inf` Windows keeps, and asks you to shut down and start
  Windows again. It does so whether or not the enclosure is plugged in,
  so an enclosure that was unplugged during the update no longer keeps
  the old Stor98. At Install the window first asks "Please disconnect
  all external NVMe drives before proceeding", OK or Cancel (Cancel stays
  on the Summary page); the console asks the same, Enter for N, which
  leaves Stor98 for a later run.
  A drive first plugged after the start binds to the new Stor98. And an
  install the helper could not finish is now reported as unfinished,
  with steps, rather than as "nothing to do".
- **No Stor98 record without a Stor98 change.** A run that leaves a
  Stor98 installed by hand alone no longer writes
  `HKLM\Software\xhci98\Stor98`; its `Version` is written only when the
  run installed or updated the bundled Stor98. The uninstall removes
  that key, and `Software\xhci98` when nothing else is in it, also when
  Stor98 itself stays.
- **A warning before Thunderbolt 3 and USB4 are turned off.** When a run
  will write `XhciCmMode` `0`, the Summary page and the console say
  "Thunderbolt 3 and USB4 will be turned off (XhciCmMode 0)".
- **The bundled Stor98 goes on tunnel drives only.** The installer
  installs Stor98 only when it is chosen (its box, ticked with the
  Thunderbolt 3 / USB4 one) and only with
  the Thunderbolt 3 / USB4 support in the same run, and then only on NVMe
  drives behind a Thunderbolt or USB4 tunnel, as the connection manager
  reports them; the installer selects tunnel drives only. An internal NVMe
  drive is left as it is, and so is one whose place cannot be read; the
  installer says which and why. Stor98 is not for internal NVMe drives:
  do not install it on one. The rest of this item held up to `3.2.0.0`;
  from `3.3.0.0` Stor98's INF for 2000 to 8 matches only the id the
  connection manager gives a drive behind a tunnel ("NVMe drives behind
  Thunderbolt or USB4 (new in `3.0.0.0`)" has the current state). On 2000
  to 7 Stor98's INF matches every
  NVMe controller (`PCI\CC_010802`), so Windows may still offer Stor98
  for an internal NVMe drive that has no driver, later: decline it there.
  On Vista x64 and 7 x64 with this package's test certificate trusted, Windows puts the
  staged Stor98 on such a drive at the next start without asking (read in
  a virtual machine), as on 8 below.
  Windows 8 has no NVMe driver of its own, and there Windows itself puts
  the staged Stor98 on an internal NVMe drive that has no driver: with
  this package's test certificate trusted it does so without a prompt,
  and otherwise through the "can't verify the publisher" prompt (not
  read). The drive then runs on Stor98.
  On 8.1 and 10 it names that id as a compatible id only, which Windows'
  own NVMe driver (`stornvme`) outranks, so there Windows never chooses
  Stor98 by itself: it goes only on a drive the installer or you put it
  on. Where a
  Stor98 the installer did not put on a tunnel drive is installed (by
  hand, on an internal drive), the installer changes none of its
  settings, and when that Stor98 is another release than the bundled one
  it changes nothing of Stor98 at all, since every Stor98 drive shares
  one driver file. The uninstall removes the bundled Stor98 also from a
  tunnel drive Windows bound to it after the install (an enclosure first
  plugged after the restart), when Stor98 was not there before this
  package's installer. While a drive the installer did not put Stor98 on
  uses it, such as an internal drive Windows 8 gave it, the uninstall
  leaves Stor98 in place, its service and INF with it, and the tunnel
  drives keep it too.
- **The Thunderbolt 3 / USB4 connection manager alone.** The installer
  can install `xhcicm.sys` (with its tunnel-port filter and, on NT, the
  safe-removal mark service) without `xhci98.sys` and `xhciuas.sys`, for
  a computer whose USB 3 controllers already run another driver: on
  Windows 2000 to 7 untick the USB drivers on the Components page. On
  Windows 98 SE and ME the USB drivers stay
  required, since those systems have no USB 3 driver of their own. The
  connection manager needs nothing of `xhci98.sys` on NT: a tunnel's
  devices are driven by whatever driver Windows has for them, the USB
  controller of a dock and a USB4 port's USB 3 devices by the computer's
  own USB 3 driver (design record 21 section 3.5). A run without the USB
  drivers leaves an installed `xhci98.sys` as it is; the uninstall
  removes everything, and Add/Remove Programs shows one entry, named for
  what is installed. `tools\xhciinst\readme.txt`, "The connection manager
  alone".
- **The installer opens from `SETUP.EXE`.** To install or update,
  double-click `SETUP.EXE` at the top of the package (to uninstall, Remove
  in Add/Remove Programs, or `SETUP /uninstall`): it opens
  the installer's window with no console window behind it, and on Vista
  and 7 Windows asks once, for the window. `INSTALL.BAT` still works and
  is kept for the command line and scripts (`INSTALL /console ...`); run
  with no arguments it opens the same window.
- **Windows 8 and 8.1: the connection manager alone.** This item is as
  `3.1.0.0` shipped it: from `3.2.0.0` the USB drivers are an opt-in
  install on 8, 8.1 and 10, Windows 10 is admitted, and the bundled Stor98
  is offered there ("Targets and their standing"). The installer now
  starts on Windows 8 and 8.1 (it reads the version through
  `RtlGetVersion`) and installs the connection manager alone there,
  without offering the USB drivers: Windows' own `usbxhci.sys` runs the
  USB controllers. Windows 8 and 8.1 were not then a target of this
  package's USB drivers and had never been run with it: the connection manager
  installs there through the same INF sections as on Vista and 7,
  untested. On 64-bit 8 and 8.1 it loads only in test-signing mode, which
  those systems let you turn on only with Secure Boot off, or on a start
  with driver signature enforcement disabled; it is not Microsoft-signed.
  The bundled Stor98 was not offered there. Windows 10 and later were still
  refused (Windows 11 has a USB4 connection manager of its own).
- **Stor98 is built with the package, at its version: `3.1.0.0`.** The
  bundled NVMe driver is now built from this source with the USB drivers
  (its source has been in this repository since 2026-10-19) and carries
  the package's version and date, like every other file of the download:
  `stor98.mpd`, both `stor98.sys` and `stor98.inf` say `3.1.0.0`, dated
  10/19/2026. Its last release on its own was Stor98 `0.4.0.0`, bundled in
  `3.0.0.0`; `3.1.0.0` is the next version after it, so the installer
  updates a `0.4.0.0` (or `0.3.0.x`) it finds, as it updates any older
  Stor98. What changed in Stor98 since `0.4.0.0` is in its readme's
  release history (`stor98-readme.txt` points to it). Its link-state
  interface with `xhcicm.sys` (interface 4) is unchanged.
- **One signature for every x64 driver.** The 64-bit `stor98.sys` is now
  test-signed with the same certificate as `xhci98.sys`, `xhciuas.sys`
  and `xhcicm.sys`, in the same step: `xhci98-test.cer` covers them all.
  `stor98-test.cer`, the certificate of Stor98's own that signed it up
  to `0.4.0.0`, is no longer in the download. Test-signing mode on Vista
  x64 and 7 x64 loads them all, as before.
- **Stor98 in the published tree.** Stor98's files are now part of each
  `release-` directory as published, like the USB drivers, rather than
  added to the download alone.
- **Known limitation: a drive attached during the install.** On Windows
  2000 to 7, a Thunderbolt or USB4 drive that is already attached when the
  package is installed or updated is not listed in Safely Remove Hardware
  until Windows restarts or the drive is plugged in again (`3.0.0.0` did
  the same).
- **Known limitation: the eject under the USB4 connection manager on 2000
  to 7.** On a Windows 2000 to 7 computer with ACPI, where the package's
  own USB4 connection manager runs, ejecting a device that connects
  through a PCI bridge (a dock, or an enclosure with a switch in it) stops
  it in Windows, but the tunnel to it stays up until you unplug it. Seen
  on Windows 7; the cause, ACPI's bus filter left on the ejected bridge,
  is the same on 2000, XP and Vista.

## What `3.0.0.0` adds

`3.0.0.0` adds a third driver, `xhcicm.sys`, for Thunderbolt and USB4
controllers, an install tool and a graphical tool, the xHCI98 USB Tree.
`xhci98.sys` and `xhciuas.sys` carry
`2.4.0.0`'s code under the new version number.

- **Thunderbolt 3 and USB4, with `xhcicm.sys`.** A Thunderbolt or USB4
  dock, enclosure or NVMe case plugged into the machine reaches Windows:
  its PCI Express tunnel is set up, and the devices behind it (an NVMe
  drive, a dock's own USB controller, a network adapter) appear in Device
  Manager and run under their own drivers. One driver, two ways of doing
  it, chosen per controller at its start: on an Intel Thunderbolt
  controller whose firmware runs its own connection manager (Thunderbolt
  2 and 3, and the USB4-generation Intel controllers while their firmware
  does), `xhcicm.sys` is that firmware's client and approves each device;
  on a USB4 host whose firmware runs none (AMD, ASMedia, Intel Lunar Lake
  and later, Barlow Ridge), `xhcicm.sys` is the connection manager itself
  and builds the tunnels. "Which controllers", below, lists them; every
  one but a single Alpine Ridge part is untested, and it says what that
  means.
- **No Refresh.** When a tunnel comes up or goes down, `xhcicm.sys` makes
  Windows look at the ports behind it at once: through Windows 98 SE's and
  ME's configuration manager, and on Windows 2000 to 7 through a small
  filter on those PCI ports. A device plugged in appears without Scan for
  hardware changes, and one unplugged leaves.
- **Safe removal.** A device behind a tunnel is offered in Windows 2000's
  "Unplug or Eject Hardware" and XP's, Vista's and 7's "Safely Remove
  Hardware"; on every system, `INSTALL /eject` stops it too ("Removing a
  Thunderbolt or USB4 device safely").
- **`INSTALL.BAT`**, with `XHCIINST.EXE` and `XHCIIN64.EXE`: installs,
  updates and uninstalls every driver of the package in one run on
  Windows 2000 to 7, and stages it with the steps left printed on 98 SE and
  ME ("Installing, updating and uninstalling with INSTALL.BAT").
  INSTALL.BAT is beta in 3.0.0.0: if it fails or you prefer, install
  through Device Manager as before ("Installing").
- **A device viewer and eject in the same tool**: `INSTALL /devices` lists
  the Thunderbolt and USB4 devices connected now and the Windows devices
  behind each, and `INSTALL /eject` stops one, on every system (design
  record 24).
- **The xHCI98 USB Tree** (`XHCITR.EXE`, `XHCITR64.EXE` on 64-bit
  Windows): a window with every USB controller, hub and device and every
  Thunderbolt and USB4 controller and device, with their details and
  descriptors, and an icon in the notification area that starts at login
  and removes a USB storage device or a Thunderbolt or USB4 device safely,
  with no administrator rights ("The xHCI98 USB Tree", below; design record 25). `xhcicm.sys`
  gains a read-only query device for it.
- **PCI details Windows does not show**, in the same tool: the interrupt
  pin and line, MSI and MSI-X, the PCI Express link speed and width, the
  BARs and a bridge's buses and windows, for each xHCI controller this
  driver runs and for each Thunderbolt or USB4 controller, its tunnel
  ports and the devices behind them. The drivers only read; `xhci98.sys`
  gains a read-only query request for it and for its log (design record
  13 section 8.12).
- **The log records by default**: `XhciLogVerbosity` is `2` on a fresh
  install, so a log exists before anyone asks for one; an update keeps the
  value already there ("The log, and how to send one").
- **Device Manager names by generation**: "Thunderbolt and USB4 in Device
  Manager", below.
- **A link-state channel for Stor98**: `xhcicm.sys` publishes the
  channel of Stor98's interface 4 on every target, Windows 98 SE to 7,
  and states on it which NVMe drive is behind a tunnel; Stor98 `0.4.0.0`
  reads it on all of them ("NVMe drives behind a tunnel").
- **Stor98 in the download**: the package carries a release of Stor98,
  the NVMe driver for Windows 98 SE to 7, as its own project released
  it (which release: "NVMe drives behind Thunderbolt or USB4", under
  "Installing").
- **`XHCISNAP` to be deprecated.** The xHCI98 USB Tree now shows both
  drivers' logs and sets their logging level. `XHCISNAP.EXE` and its
  `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` still ship in this release
  and work as before; they will be deprecated in a subsequent release.

### Where `3.0.0.0` was read

Every reading below was taken in QEMU virtual machines on development
host A, on the emulator of the `usb4-emu` project: its model of the
ThinkPad P14s Gen 1's Alpine Ridge controller (from a capture of that
machine), its Titan Ridge model, its USB4 host models (Alder Lake's, a
minimal one, and a stand-in for ASMedia's ASM4242), and its model of a
real USB4 NVMe enclosure (the ORICO enclosure's ASMedia ASM2464, from a
capture). The NVMe driver was Stor98. The one real machine
is the owner's ThinkPad P14s Gen 1 under Windows 98 SE ("Untested hosts"
above); **no other real Thunderbolt or USB4 machine has run this
release**. `docs/contributing/runs/run-39.md` and `run-40.md`
have every leg.

- **Windows 98 SE:** under the firmware connection manager, the install,
  a device approved and its NVMe drive found with no Refresh and read and
  written through Stor98, unplug and re-plug, a device tunnelled at
  boot left alone, nothing attached at boot, the controller absent with
  Thunderbolt BIOS Assist on, disable and enable, `XhciCmApprove` `0` and
  `XhciCmMode` `0`, a plug and an unplug while an Add New Hardware wizard
  was open, and `INSTALL /devices` and `INSTALL /eject` (an eject refused
  while a file was open, then accepted). Under the USB4 connection
  manager, the install, hot plug, unplug and re-plug, a cold boot with
  the device attached, reads and writes on the ASM2464 enclosure model,
  the USB3 tunnel, and the host interface taken away under the driver,
  which then stopped touching it.
- **Windows ME:** the install through `INSTALL.BAT`, and under both
  connection managers a plug, unplug and re-plug with the NVMe drive
  found with no Refresh and its data intact; Stand by and resume under
  both. ME's "Unplug or Eject Hardware" does not list the drive (98 SE
  as shipped has no such applet): the USB Tree and `INSTALL /eject` remove
  it.
- **Windows 2000:** the firmware connection manager's legs read on 98 SE,
  the open wizard aside, and the Titan Ridge model,
  safe removal through "Unplug or Eject Hardware", the USB4 connection
  manager's legs (a cold device, hot plug, unplug during I/O, pre-OS
  tunnels kept and cleaned, the USB3 tunnel, disable and enable, the
  policy values, two ports, a daisy chain left alone, the driver turned
  off) on three host models, the host interface taken away, and a
  multiprocessor guest under Driver Verifier. Two readings failed on the
  emulated platform, not in the driver: a re-plug after Thunderbolt BIOS
  Assist powered the controller off (the emulator resets the controller's
  bridges without telling Windows), and a second port on the ASM4242
  stand-in (Windows 2000 left that bridge at Code 12).
- **Windows XP, XP x64, Vista (32-bit) and 7 (32-bit):** the install with
  the filter through `INSTALL.BAT`, and under both connection managers a
  plug, unplug and re-plug with the NVMe drive found with no Refresh;
  under the firmware connection manager also safe removal through
  "Safely Remove Hardware", with no dialog after a plain unplug. These
  legs ran the `release` package (XP x64's unsigned, which it loads);
  most 98 SE, ME and 2000 legs ran the `qemu` flavour of `xhcicm.sys`,
  which logs.
- **Not yet read: Windows Vista x64 and 7 x64** with a Thunderbolt or
  USB4 controller. They load only a signed driver, and these legs wait
  for the test-signed package. Suspend and resume were read on ME only.
- **`INSTALL.BAT`:** a first install and an update from `2.4.0.0` on each
  of Windows 98 SE, ME, 2000, XP, XP x64, and Vista and 7 in both
  architectures (Vista x64 and 7 x64 with an unsigned package, started
  with F8), and on several of them an uninstall and an update from
  `2.3.1.0`.

## What `2.4.0.0` adds

- **The silent install on Windows 98 SE and ME, off by default.** A new
  value, `XhciSilentInstall`, under the controller's driver key: at `1`, a
  device whose class is in the list at
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB\SilentClasses`
  installs with no Add New Hardware wizard; at anything else, or absent, the
  driver behaves as before. The install writes it at `0`, and an update
  keeps a `1` already there. The download's
  `tools\silent\` directory carries the list, `SILENT.REG` (hubs, boot
  keyboards and mice, Bulk-Only storage; UAS is not in it), `SILENT.VBS`,
  and `silent-on.bat` and `silent-off.bat`, which turn it on and off in one
  double-click; restart Windows after either. With no wizard, 98 SE asks
  for a missing driver file from its CD (read for a mouse and a
  keyboard). Never on 2000 and later. SweetLow
  provided the information on how the silent install is decided and the
  list. Read in QEMU virtual machines: on 98 SE a mouse, keyboard, stick
  and hub, each a device of its own, installed with no wizard and worked
  (the mouse and the keyboard after the CD prompt above), and so did a USB
  audio composite device matched by an entry added by hand; with the
  switch off, or no list, a new mouse brought the wizard as before. A hub
  needs this package's INF from `2.1.0.0` or later: under an older one its
  devnode is left with no driver. ME needs none of it
  for those: it installed a mouse, keyboard, stick and hub with no wizard
  with the switch off. On ME the switch matters only for a class ME would
  ask about (the audio device). A composite keyboard, mouse or stick was
  not read on either (no virtual device offers one), nor real hardware.
- **Controllers below xHCI 1.0 start.** `2.3.1.0` refused a controller
  whose HCIVERSION is below 1.0 (Code 10, "This device cannot start"),
  which is where the first-generation NEC/Renesas uPD720200 and uPD720200A
  (`1033:0194`) and ASMedia's ASM1042 stopped: they are xHCI 0.96 parts.
  From `2.4.0.0` such a controller starts, driven as an xHCI 1.0 one but
  for the fields Linux writes differently on 0.96 parts. A new value,
  `XhciAccept096`, written by the install at `1`, turns it off at `0`
  ("XhciAccept096", under "Registry settings"). Read in QEMU virtual
  machines, whose controller is 1.0, so no 0.96 part has run it
  ("Untested ground").
- **A finer system timer while polling, on Windows 2000 and later, off
  by default.** A polled controller was looked at no more often than
  Windows' clock ticked (10 to 15 ms on Windows 2000, XP and 7 in QEMU),
  however short `XhciPollActiveMs` was. With a new value,
  `XhciPollFineTimer`, set to `1`, the driver asks Windows for a clock of
  `XhciPollActiveMs` while it polls at that period, and gives it back one
  idle period after, when the controller sleeps, and when it stops. The
  value is `0`, off, by default: in QEMU virtual machines the finer clock
  slowed the guest's own (below, and "Untested ground"). Never on
  Windows 98 SE and ME, which have no such call ("XhciPollFineTimer: a
  finer system timer while polling").
- **The 32-bit helper is now `xhcihlpr.sys`, and 2000 and XP get it too.**
  It replaces `2.3.x`'s `xhcimsi.sys`: on 32-bit Vista and 7 it still
  connects the message-signalled interrupt, and on 32-bit 2000, XP, Vista
  and 7 it carries the finer timer. On 2000 and XP the install asks for no
  restart, and the timer, when turned on, starts at the next boot; on
  Vista and 7 an update deletes `xhcimsi.sys` at once and asks for a
  restart, after which its service is gone ("Updating from 2.y.y.y"). To remove it after an uninstall, `sc delete
  xhcihlpr`, or on Windows 2000 its key ("Windows Vista and 7:
  message-signalled interrupts").
- The finer timer has been read only in QEMU virtual machines without
  hardware acceleration, polling (`XhciInterruptMode` `2`), on 7 x64,
  7 x86 and Vista x86: while it was held the guest's clock ran at a tenth
  to seven tenths of real time and USB keyboard input was lost; with it
  off, or under an interrupt, the clock kept time and nothing was lost.
  So it ships off. It has not been read on real hardware ("Untested
  ground").
- **The root hub, the hubs and the composite devices are named under
  `USB\`.** Their device ids move from `XHCI98\ROOT_HUB`, `XHCI98\HUB`,
  `XHCI98\HUB30` and `XHCI98\COMPOSITE` (each hub and composite id with its
  `&VID_`, `&PID_` and `&REV_` forms) to `USB\XHCI98_ROOT_HUB`,
  `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE`: a device
  id's first part names the bus that found the device, and there is no
  `XHCI98` bus (SweetLow, GitHub issue 17). Their entries move from
  `Enum\XHCI98` to `Enum\USB` in the registry. The names stay private to
  this project, so no Windows or vendor INF binds them, and the root hub's
  install no longer copies `xhci98.sys` (the controller's does). **An
  update from `2.3.x` or earlier finds each root hub, hub and composite
  device once more, and asks on XP, XP x64 and 98 SE** ("Updating from
  2.y.y.y", "From 2.3.x or earlier"). The download's `tools\xhcicln\`
  carries `XHCICLN.EXE` and `XHCICL64.EXE`, an optional tool that lists the
  old entries an update leaves behind and, on Windows 2000 to 7, removes
  them; on 98 SE and ME it lists only. The downloads up to `3.2.0.0`
  carry it. It was dropped in `3.3.0.0`.
- **Linux's controller quirks, fourteen of them, each behind a value of
  its own.** Linux's xHCI driver works around faults of particular
  controllers, named by PCI vendor and device ID; this release takes
  fourteen of those workarounds, each a new value under the controller's
  driver key with `0` off, `1` Linux's controllers (the default for all but
  `XhciShutdownReset`, which is off) and, where it is generic, `2` every
  controller, and `XhciIntervalCap` now also covers the ATI Navi10's USB-C
  controller (`1002:7316`). The install writes each at its default, and an
  update adds them. See "Linux's controller quirks".
- **Endpoint rings are kept until the controller has let go of them.**
  When a driver switches an interface to another alternate setting (an
  audio device starting or stopping, for one) and the switch fails part
  way, the driver used to hand the old endpoints' transfer rings back for
  reuse while the controller could still be using them. It now keeps every
  ring until a command proves the controller is done with it, the rule
  Linux follows; a switch the controller refuses leaves the interface
  working at its old setting; and a device that refuses the switch has
  its old setting put back. A device whose interface has only one setting
  may refuse the request outright, as USB allows: that is now treated as
  success, the endpoints' toggles reset by clearing their halts, as Linux
  does. This also bounds the endpoints the driver ever asks of the
  controller to the 64 that Intel's 7-series (Panther Point) controller
  can hold. Not read on real hardware.
- **hidusbf on a composite device's own entry.** Under Windows' own USB
  stack, a composite device - a keyboard with media keys, a keyboard and
  mouse receiver - selects its configuration once, on its "USB Composite
  Device" entry, so hidusbf's "Process Parent" sets the polling rate of
  every function there at once. Under this driver that entry
  (`USB\XHCI98_COMPOSITE`) took no such request until now, so the filter
  there did nothing; only the filter on a function's own entry worked. The
  driver's composite entry now selects the configuration through its own
  entry at its start, as Windows' does, and each function takes its
  polling rate from it, unless a filter on the function's own entry sets
  another, which wins. A change of the rate reaches a function when it
  next starts: at once on 2000 and later, where restarting the composite
  entry restarts its functions; on 98 SE and ME restart the function's
  entry too, or replug the device. SweetLow found it (GitHub issue 17).
  The composite entry's own selection was read in QEMU virtual machines
  with two USB audio devices, one with a serial number and one without:
  each selection made once, the configuration unchanged, both functions
  bound, a tone played whole from each, and the composite entry's and the
  controller's disable and enable recovering, on ME, 2000, 32-bit XP,
  XP x64 and 7 x64, and on 98 SE the functions bound (USB audio does not
  play there in QEMU). hidusbf on a composite entry is read on no virtual
  machine, as QEMU offers no keyboard-and-mouse composite device; it is
  owed to a real composite device after the release ("Untested ground";
  see "SweetLow's hidusbf").

## What `2.3.1.0` changed

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
  from the release package on Windows 98 SE, ME, 2000, XP and 7 x64,
  each shutting down twice with a mouse, a stick, a UAS disk and a hub
  with audio attached.
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
  "XHCIQUAL: checking a machine's controller (optional)").
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
  and what Linux, FreeBSD and Haiku do. On a VL805 it is reported working
  on `2.3.0.0` by the owner, not yet read in detail. It
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
| Windows 98 under NUSB: stopping the controller (disable, uninstall, upgrade) crashed the machine, in NUSB's `usbport.sys` | `2.0.0.0` does not use `usbport.sys`. Disable, enable, remove and rescan of the `2.0.0.0` controller completed under NUSB 3.3 and under SweetLow's stack in Windows 98 SE virtual machines. |
| Windows 98: fast, repeated plug and unplug could freeze the machine | Gone in every reading: the hub churn that froze Windows 98 under `1.2.0.0` at 12 to 18 hubs ran 120 of 120 with the guest responsive, and the device matrix's hub churn passed on Windows 98 SE and 2000 on the final build |
| Windows 7 (32-bit, on a ThinkPad E460): disabling the controller hung | Gone under `2.0.0.0`. It belonged to the miniport under Microsoft's `usbport.sys`; under `2.0.0.0` it did not occur in five disable and enable cycles each on Windows 7 x86 and x64 |
| Windows 2000: a USB audio device unplugged during playback was never fully removed | Gone: the removal arrived within about a second, 7 times out of 7 |
| The controller never went to sleep | The driver idles nothing it is not asked to; see the selective-suspend entry in "Known limitations" |
| Every root-port device reported to Windows as High Speed, and its consequences: root-port polling in 1, 2 and 4 ms bands, a Full-Speed audio device on a root port silent from Windows XP on, a USB 1.1 hub on a root port crashing Vista and 7, and the Advanced tab's bandwidth figures | The cause is gone: there is no `usbport.sys` to report to, and every device is given its true speed, read on every device row of the virtual-machine matrix on both primary targets. A Low-Speed mouse is polled every 8 ms at a root port and behind a hub, on real hardware under Windows 98 SE. A Full-Speed hub on Vista and 7 enumerated with devices behind it, and a Full-Speed audio device on a root port bound on XP, Vista and 7 and played in real time on XP x64, in virtual machines |
| Windows 98 SE: USB audio could stutter while a drive was read at full speed | Not re-measured under `2.0.0.0` |
| Windows 2000: a newer package over an older one was refused; disabling the controller with an audio device attached asked for a restart | The upgrade from `1.2.0.0` installs in place on Windows 2000. The disable with an audio device attached was not re-measured |
| Windows 98: a driver that failed while starting the controller stopped the machine with a protection error | Not re-measured: no failing start was provoked under `2.0.0.0` |
| On a controller without Force Save Context, a wake from standby rebuilt the bus | Under `2.0.0.0`: untested, the power handlers had not run. Since `2.3.0.0` (read in QEMU, which has no Force Save Context): hibernate and resume on XP, Vista and 7, and standby and hibernate on ME, idle, rebuild the bus through the controller's reinitialization and, up to `3.3.0.0`, every device is found again (from `3.4.0.0` a device on a root port is kept, "XhciResumeKeep"); with a copy running on a stick on XP, Vista and 7, the copy's requests fail for about 10 s while the stick is found again, then work. Open: Windows ME hangs on a standby or hibernate with a copy running on an xHCI stick. Windows 98 SE's standby resumed with a USB mouse at a root port or behind a hub, and with a USB disk plugged in after Windows started, and stopped with a USB disk attached when the computer started (the BIOS presents it, and Windows uses it in MS-DOS compatibility mode), as it does under NUSB's EHCI driver; QEMU's Windows 98 SE does not resume reliably with no USB controller either (the 98 SE Stand by entry in "Known limitations"). Standby on the other systems, and with devices suspended, is not yet read |
| The virtual High-Speed hub switch and its costs. It also did nothing unless `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` were set beside `XhciVirtualHSHub`, since `1.2.0.0` has no default for either (reported on GitHub issue 4, 2026-10-03) | No switch: all three values have no effect (see "Registry settings") |

## Updating from older releases

The release notes' update steps for releases before `3.0.0.0`, as they stood at
`3.4.3.0` (under "Installing"; moved here 2026-11-13, owner: the release notes
keep only how to update now).

### Updating from 2.y.y.y

From `3.0.0.0` the installer (`SETUP.EXE` from `3.1.0.0`) does what
follows on Windows 2000 to 7 and prepares it on 98 SE and ME ("Installing, updating and uninstalling with
INSTALL.BAT"); this section is the route by hand, and what each system
does either way.

**From `2.3.x` or earlier to `2.4.0.0` or later, read "From 2.3.x or
earlier" (below) first**: that update finds each root hub, hub and
composite device again, so update the controller only, and on Windows XP,
XP x64 and 98 SE have a keyboard and mouse that do not go through this
driver before starting. Otherwise:

Install a newer `2.y.y.y` over an older one with Update Driver on "xHCI98
USB 3.x eXtensible Host Controller", then on "xHCI98 USB 3.x Root Hub", both
pointed at the `release-` directory. The steps are the same from every
earlier `2.y.y.y`. The registry values already there are kept and the
missing ones added ("Registry settings"); an update over `2.1.1.0` adds the
three values new in `2.2.0.0` the same way. What follows is what an update
of `2.1.0.0` over `2.0.0.0` does. Neither release uses NUSB's or SweetLow's
port driver, so nothing needs renaming first. On each system:

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
- **Windows XP, XP x64, Vista and 7**: XP, XP x64 and 32-bit Vista show
  their unsigned-driver warning once; on every one the update takes effect
  at once, and no restart is asked for.
- **Windows Vista and 7: use Have Disk.** Update Driver Software, Browse my
  computer for driver software with the folder typed in answers that the
  best driver software is already installed and keeps the earlier release.
  Choose "Let me pick from a list of device drivers on my computer", Have
  Disk, and the `release-` directory instead.
- **32-bit Vista and 7: restart when asked.** Through Have Disk, both asked
  for a restart, for `xhcimsi.sys`, and until it the earlier release kept
  running with the controller at no problem code ("You need to restart
  your computer before the changes you made to this device will take
  effect"); after it, `2.3.1.0` and its helper ran.

**The root hub's Driver tab keeps showing `2.0.0.0`** (on Windows 98 SE and
ME, its date) after the update, although the root hub runs the same new
`xhci98.sys` as the controller. To have it show `2.1.0.0`, run Update Driver
on "xHCI98 USB 3.x Root Hub" too, pointed at the same directory. On 32-bit
Vista and on Windows 7 x64 letting Windows search found it; on 32-bit
Windows 7 the search answered that the best driver was already installed,
and "Let me pick from a list of device drivers on my computer" with the
`2.1.0.0` entry did it; on Vista x64 a command-line update of the root hub
did it. On the other systems use Have Disk.

When updating from `2.0.0.0`, each device is found once more as new hardware, exactly
once, and Windows installs it again from the same drivers: a stick under its
serial number, which Windows now knows it by instead of its port, and the
other devices under a new id. On the NT systems this happened with nothing
to answer; on Windows 98 SE the Add New Hardware wizard ran for each and
asked for the Windows 98 SE CD again for `hidclass.sys`, and Windows ME
found each once as well. Nothing was
found again after that. A setting kept on such a device's own entry, such as
hidusbf's polling rate, has to be applied again on the new entry.

**On Vista and 7, Have Disk does not take a build whose INF is identical to
the installed one** (the same `DriverVer`): Windows reuses the package
already in its driver store and keeps the old `xhci98.sys`. Replace the
`.sys` by hand, or use a build with a newer `DriverVer`.

**An update keeps the Intel port switchover as it was.** Since `2.3.0.0` a
new install writes `XhciIntelPortSwitch` at `0`, off; `2.1.1.0` and
`2.2.0.0` wrote `1`, and an update never overwrites a value that is already
there, so a machine updated from either keeps the switchover on. To take the
new default, run `portsw-off.bat` (or `portsw.vbs 0`) from the download's
`tools\portsw\` directory (in `2.3.1.0`, `portsw\` at the top of the
download; in `2.3.0.0`, the flavour directory) and restart Windows
("Intel 7-, 8- and 9-series chipsets: the port switchover").

**An update from `2.2.0.0` adds the eight values new in `2.3.0.0`** at
their defaults and keeps the rest ("Registry settings"). On 32-bit Vista
and 7 it also installs `xhcimsi.sys` and asks for a restart ("Windows
Vista and 7: message-signalled interrupts").

**An update to `2.4.0.0` adds `XhciPollFineTimer`** at `0` (off) and keeps
the rest. On 32-bit Vista and 7 it installs `xhcihlpr.sys`, marks the
`xhcimsi` service of `2.3.0.0` or `2.3.1.0` for deletion, deletes the
`xhcimsi.sys` file at once, during the update, and asks for a restart; the
controller's update itself waits for that restart, and after it the
`xhcimsi` service is gone, no error is logged for it, and `xhcihlpr.sys`
connects the message. On 32-bit Windows 2000 and XP it installs
`xhcihlpr.sys` and asks for no restart of its own; the helper runs from the
next boot, and the finer timer ("XhciPollFineTimer: a finer system timer
while polling"), when set to `1`, starts then.

**Deleting the driver keeps the older packages.** On Vista and 7,
uninstalling the controller in Device Manager with "Delete the driver
software for this device" ticked removes only the package in use. Every
older `xhci98` package installed on that machine stays in Windows' driver
store, and the next time Windows finds the controller it may install one of
them. To remove them all, list the store from an administrator's prompt with
`pnputil -e`, and remove each `oemNN.inf` it lists with the provider "Yeo
Kheng Meng" and the class "Universal Serial Bus controllers" with `pnputil
-d oemNN.inf`. The same applies before going back to an older version
("Windows Vista and 7: message-signalled interrupts").

#### From 2.3.x or earlier

From `2.4.0.0` the root hub, every hub and every composite device are named
under `USB\` instead of `XHCI98\` ("What `2.4.0.0` adds"). Windows keeps an
entry per device id, so this update costs something once, on every system.
What follows is what an update from `2.3.1.0` does on Windows 98 SE (under
NUSB and under SweetLow's stack), ME, 2000, 32-bit XP, XP x64, and Vista and
7 in both architectures (design record 13 section 10.13 is the design):

- **Each root hub, hub and composite device is found again as new
  hardware, once**, and installed from this package under its new name. Its
  old entry stays behind, not present: nothing loads for it, it costs
  nothing at run time, and Device Manager hides it (on Windows 2000 to 7
  it shows, greyed, after `set DEVMGR_SHOW_NONPRESENT_DEVICES=1` at a
  prompt, `devmgmt.msc` from that prompt and View, Show hidden devices; the
  old and the new carry the same names). Update the controller only: the
  root hub is a new one and installs by itself from the new package, so
  the root hub's Driver tab paragraph above does not apply.
- **The devices beneath follow.** A device or a composite device's function
  with a usable serial number (most sticks and disks) keeps its entry, its
  driver and its settings. One without (most mice and keyboards, at a root
  port or behind a hub) is found again too, because Windows names it after
  its parent, and on Windows 98 SE the Add New Hardware wizard runs for each
  and may ask for the Windows 98 SE CD. What was kept on the old entry is
  not carried over: a setting a tool wrote there (hidusbf's polling rate), a
  friendly name, and for a USB serial adapter possibly its COM number (the
  old entry may keep the number it had, and the new one take the next free
  one). Apply them again afterwards.
- **Before updating on Windows XP, XP x64 or 98 SE, have a keyboard and
  mouse that do not go through this driver**: PS/2, a laptop's built-in
  ones, or a USB controller another driver runs. XP and XP x64 ask Found New
  Hardware and the unsigned-driver question for the new root hub, each hub
  and each composite device; until those are answered, the USB keyboard and
  mouse behind the new root hub do nothing, so they cannot answer them. A
  second controller running this driver does not help: its root hub is new
  too. On 98 SE the USB keyboard and mouse do nothing while the update
  wizard's last page ("Windows has finished installing an updated driver")
  is up, so its Finish needs another keyboard or mouse; at Finish they work
  again, on the earlier release, until the restart. After the restart, 98
  SE's Add New Hardware wizard for a mouse without a serial number, found
  again, cannot be answered with that mouse. The update itself asked only
  for the Windows 98 SE CD (`usbd.sys`), and no restart.
- **Windows ME** asked for nothing: the controller shows a problem until
  the restart (as above), and after it one wizard came, for the USB audio
  device without a serial number.
- **Windows XP** shows "will not work until you restart" at the end of the
  controller's update, and then a System Settings Change box offering a
  restart, although `2.4.0.0` already runs; on XP `XHCICLN` already found
  every check passed before that restart. **64-bit Vista and 7** show the
  same "will not work until you restart", and `2.4.0.0` runs at once there
  too. **32-bit Vista and 7** keep the earlier release running until the
  restart they ask for, as above.
- **Windows 2000: reinstalling after the update.** An uninstall of the
  controller and a scan for hardware changes after the update installed
  `2.3.1.0` again from the earlier package Windows 2000 still held
  (`oem0.inf`): it ignores the dates of unsigned drivers. Use Have Disk
  with this release's `release-x86\` directory again, as for the update.
- **Two controllers** (32-bit XP): one controller's update made both run
  `2.4.0.0` at once, as both run the one `xhci98.sys`; the second
  controller's driver key kept `DriverVersion` `2.3.1.0`, the version its
  Driver tab shows, until it was uninstalled and found again.
- **Windows 98 SE and ME: connect no new hub or composite device between
  the update and the restart.** The earlier release runs until then and
  would give it an old name, which the new package no longer binds.
- **The old entries stay, and nothing needs them removed.** After the
  restart they are not present and do no harm. The cleanup tool that
  could remove them,
  `XHCICLN`, was dropped in `3.3.0.0`. The downloads from `2.4.0.0` to
  `3.2.0.0` still carry it in `tools\xhcicln\`, with its `readme.txt`.
  The installer's uninstall takes them with the rest of the driver (on
  98 SE and ME through the steps it prints).
- **Going back to `2.3.x`** brings each old entry back with its driver,
  and the new ones go non-present.

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one
installed, Windows 98 SE's "Search for a better driver" keeps the copy it
already has. Instead, Update Driver on "xHCI98 USB Attached SCSI Storage",
choose "Display a list of all the drivers in a specific location", then Have
Disk, and point it at the package directory.

#### The 1.2.0.0 virtual-hub values have no effect

`XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, which a
`1.2.0.0` install wrote, are not read by any `2.x` release and its INFs
write none of them. A value left behind by a `1.2.0.0` install has no effect. The driver
reports every device at its true speed with no virtual hub in the way, so
there is nothing for them to switch. Delete them if you want them gone.

#### Stor98's settings renamed in `3.3.0.0`

From the release notes' "NVMe drives behind Thunderbolt or USB4 (new in
`3.0.0.0`)", as at `3.4.3.0`:

From `3.3.0.0` every Stor98 setting is named alike on every system, each
name starting with Stor: on Windows 2000 to 10 the `DriverParameter` keys
`maxtransferkb=`, `writethrough=`, `pollmode=`, `pollbusyus=`,
`pollidlems=`, `pollspinus=`, `gone=` and `idle=` are renamed
`stormaxtransferkb=`, `storwritethrough=`, `storpollmode=`,
`storpollbusyus=`, `storpollidlems=`, `storpollspinus=`, `storgonems=` and
`storwriteidlems=`, and Windows 98 SE and ME gain `StorGoneMs` and
`StorWriteIdleMs`, with the same meanings and ranges. A key of an old name
is ignored after the update, and the setting takes its default: rename it
in `DriverParameter`, keeping its value (Stor98's readme, "Settings").
Windows 98 SE and ME also gain `StorHold9xMs`, how long a request waits
for room in Stor98 before it is answered busy: 4000 ms by default,
1000 to 10000 ms, a value outside that refused and the default kept. It
has no `DriverParameter` key, since on Windows 2000 to 10 Stor98 holds a
request by its own timeout; `SETUP.EXE` and the wizard list it with the
other Stor98 settings (under "Advanced...", greyed on 2000 to 10).

#### Going back to a release before `2.3.0.0` on Vista or 7

From the release notes' "Windows Vista and 7: message-signalled
interrupts (new in `2.3.0.0`)", as at `3.4.3.0`:

- **Going back to an earlier version** on Vista or 7: the request for a
  message stays in the hardware key, and releases before `2.3.0.0` do not
  know what to do with a message. Before installing an older package,
  uninstall the controller in Device Manager with "Delete the driver
  software for this device" ticked, or set `MSISupported` to `0` first. The
  tick removes only the package in use; older ones stay in the driver store
  ("Updating from 2.y.y.y", "Deleting the driver keeps the older packages").

## Known limitations dropped from the release notes

Dropped from "Known limitations" on 2026-11-12 (owner), kept here as they
stood.

- **Windows 2000 updated from `3.2.0.0`: an internal NVMe drive added later
  can start on Stor98**, through a Critical Device Database key `3.2.0.0`
  left (`pci#cc_010802`, Service `stor98`) that later updates do not remove.
  Delete
  `HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase\pci#cc_010802`
  with Regedit as an administrator when its `Service` is `stor98`, then
  restart. XP may do the same. A fresh install does not write that key.
