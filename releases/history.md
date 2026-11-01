# Release history

One entry per version, newest first, in the shape of the notes published
with each release on GitHub: what is new, changed and fixed, installing
and updating, the known limitations and the downloads. The detailed
record of each change is in the roadmap and the run sheets
(`docs/contributing/`).

`scripts\package\make-release.ps1` requires an entry for the version being
cut and puts that entry into the release's own `readme.txt`, with a
one-line list of the earlier releases. So a release cannot be published
without a changelog entry. Up to `3.3.0.0` each readme carried the whole
history; those cut folders stay as they were written.

(`readme.txt`, not `README.md`: the generated guide is plain text at 78
columns because it is read on the target machine, in Windows 98 Notepad or DOS
EDIT, where a `.md` file renders as nothing and its markup is just noise.)

## 3.4.0.0 - 2026-10-28

### New

- **Windows 2000 and 32-bit XP: Thunderbolt 3 and USB4 docks plugged in
  while Windows runs.** A dock, or an enclosure with a PCI Express switch
  of its own, now gets room for every port in it, so the devices behind
  them (an NVMe drive, a USB controller, a network adapter) can start.
  Before, only one of its ports got room. `XhciCmBridgeReq` under the installer's "Advanced..." turns it
  off.
- **Devices kept across standby and hibernation:** where the USB
  controller has to be started afresh at the resume (every VIA controller,
  and any controller that cannot save its state), a device on one of the
  computer's own USB ports is kept as it was, with its drive letter and
  its open files, instead of being removed and found again. A UAS drive is
  kept while it is in use too. This is for a user's report of "Delayed
  Write Failed" on a UAS drive behind a VIA VL805 after standby.
  `XhciResumeKeep` `0` turns it off.
- **XHCIPFW on 32-bit Windows 8 and 8.1:** now offered there too, as a
  filter of the PCI root bus (the service `xhcipff`). It is still not
  offered on Windows 10.
- **USB devices with more than one configuration** that xHCI98 can split
  into their parts are now set up in their first configuration. So a USB
  Audio 3.0 device's USB Audio 2.0 part there gets the free `uac2`, where
  before the device had no driver.
- **xHCI98 USB Tree:** the window's title shows its version, as
  `xHCI98 USB Tree 3.4.0.0`.

### Changed

- **USB Audio 2.0 and 3.0 devices (Windows 2000 to 8.1):** xHCI98 no
  longer offers such a device to Windows' own USB Audio 1.0 driver ("USB
  Audio Device"), which cannot run it and, once installed (read on XP),
  keeps Device Manager, the Update Driver wizard and shutdown waiting
  until a reset.
  Windows now asks for a driver (the free download's `uac2`), unless one
  of Windows' own lines names that device by its ids.
- **Windows 98 SE and ME: more of the free and non-free drivers can be
  ticked** for Windows' hardware wizard to find by itself, as long as the
  wizard's search path stays short enough (280 characters on 98 SE, 259
  on ME). The Free USB drivers page also lists the drivers that are only
  copied, and says where to point the wizard for them.
- **Windows 98 SE and ME: the installer asks you to close the hardware
  wizard only when one is open** (on English Windows; other languages
  still ask once).
- **The installer's Components page** is evenly spaced on every system.

### Fixed

- **Windows 98 SE:** an install on a computer with two USB 3 controllers no
  longer asks for the "xhci98 xHCI Host Controller Disk" at the second
  one.
- **Windows 98 SE:** a power cut during the install's file copy no longer
  loses the record of NUSB's storage files, so a later uninstall removes
  them.
- **Thunderbolt 3 / USB4:** under Driver Verifier, the connection manager
  could stop Windows at the start (Windows 7) or when it was unloaded by an
  uninstall (Windows 8 and 8.1).
- **Smaller fixes** from a review of the whole code: the USB driver, the
  UAS driver, Stor98, the Thunderbolt 3 / USB4 driver and the installer.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\`; the ones you tick are also added to Windows' driver
  search path. When Windows asks for a driver for another such device,
  point it at that device's folder there.
- **Windows 8 and 8.1 with XHCIPFW:** if Windows no longer starts after
  `xhcipff.sys` was deleted or damaged, use Windows' System Restore, or the
  steps in `tools\xhciinst\readme.txt`.

### Known limitations

- **All systems**
  - Standby and hibernation with a Thunderbolt or USB4 device attached are
    not supported: use Safely Remove and unplug the device first.
  - After a standby or hibernation that starts the USB controller afresh,
    a hub and the devices behind it, a Bulk-Only drive, a drive with no
    serial number or one that could not finish its work before the sleep,
    a card reader or other removable drive with a medium in it, a drive
    that does not report its own identity, and an audio or video device
    in use are still removed and found again.
  - An internal NVMe drive already on Stor98 (put there by hand) keeps it.
  - A maker's driver that takes a whole USB device with more than one
    configuration no longer binds to it under xHCI98 where xHCI98 now
    splits that device.
- **Windows 2000 to 8.1**
  - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
    Device": unplug it, restart Windows (reset the computer if the
    shutdown hangs), run `cmd /c "set devmgr_show_nonpresent_devices=1&&
    devmgmt.msc"`, choose View, Show hidden devices, update the greyed
    "USB Audio Device" to `uac2`, then plug the device in.
- **Windows 2000 and XP**
  - **Docks:** the first time a dock or an enclosure with a switch is
    plugged in, its devices may stop and start again once, a few seconds
    after it is plugged in (after its Found New Hardware windows are
    answered), while xHCI98 sets up its PCI Express ports. If a drive
    behind it is already in use, unplug it safely and plug it in again
    instead. A device added later behind a port that has no I/O range (a
    network adapter behind a chained dock) is placed after the dock is
    unplugged and plugged in again.
  - **Stop a dock before unplugging it** (Safely Remove Hardware, Unplug or
    Eject Hardware on 2000, or the xHCI98 USB Tree) when a keyboard, mouse,
    tablet or similar device is on the dock's own USB controller, unless
    xHCI98 builds the computer's USB4 tunnels itself (a USB4 host whose
    firmware has no connection manager), where the unplug is normally
    handled. Otherwise Windows may never finish
    removing the dock: it is not found again when plugged back, and Windows
    does not shut down.
  - **Updated from 3.2.0.0:** an internal NVMe drive added after the update
    can start on Stor98: 3.2.0.0 left the key
    `CriticalDeviceDatabase\pci#cc_010802` with Service `stor98`. Delete
    that key with Regedit as an administrator, then restart (the release
    notes, "Known limitations").
- **Windows 98 SE and ME**
  - **Stor98 and internal NVMe drives:** these systems have no NVMe driver
    of their own, and they look for a driver before the Thunderbolt 3 / USB4
    driver can give a drive its id. So Stor98 still matches every NVMe drive
    there, and Windows may offer it for an internal drive with no driver:
    decline it. The installer puts Stor98 on tunnelled drives only.
  - **Docks:** stop a dock in the xHCI98 USB Tree before unplugging it
    when a keyboard, mouse, tablet or similar device is on the dock's own
    USB controller.
  - **First plug of a Thunderbolt or USB4 enclosure (98 SE):** when Windows
    asks to restart after installing the enclosure's PCI bridges, answer No.
    If you answered Yes and the drive does not appear, run `asd` from Start,
    Run, tick "Enumerating a device => PCI standard PCI-to-PCI bridge",
    click OK and restart.
  - **USB audio (98 SE):** the first USB audio device plugged in after a
    boot can freeze Windows, also with Windows' own drivers. Plug the device
    in before switching on and leave it plugged in. For a webcam whose
    microphone you don't need, usb-wdm-free's `NOAUD98.INF` avoids it.
  - **Free USB audio and MIDI drivers:** Windows may ask for `wdmaudio.cat`,
    `ks.cat` and other files of its own from the Windows CD. Point it at the
    CD's `WIN98` folder, or click Skip File for the `.cat` files.
  - **A shared IRQ held by another device (98 SE; ME expected the same):**
    Windows crawls or freezes once xHCI98 is installed when a device with
    no working driver (an NVMe drive the BIOS still runs, or one whose
    driver failed) shares an xHCI98 controller's IRQ. Give that device a
    working driver or remove it; or, if no other working driver uses that
    IRQ, disable the xHCI98 controllers on it, or set `XhciInterruptMode`
    to `2` and restart.
- **Windows 8.1 and 10**
  - Stor98 still matches by the NVMe class there. Windows' own `stornvme`
    outranks it, so an internal drive keeps `stornvme`. Stor98 goes only on
    a drive the installer or you put it on.
- **32-bit Windows 10**
  - XHCIPFW is not offered: there it cannot start in time. On a board that
    leaves the USB4 ports' windows above 4 GB, 10 cannot use the devices
    behind those ports.

### Choose from 3 downloads

Each zip holds everything in the one before it.
- **`xhci98-3.4.0.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4), Stor98, the installer and the tools. It also has NUSB 3.3's seven
  Windows 98 SE storage files in `USBDRV\NUSB33\`, offered by the installer
  on 98 SE if you don't already have NUSB. If you have NUSB already
  installed, it will be kept.
- **`xhci98-3.4.0.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) in `USBDRV\FREE\`. **These are
  experimental:** they may not work with every device and can make
  Windows unstable. Please report their problems to that project.
- **`xhci98-3.4.0.0-nonfree.zip`:** also adds a pack of other
  manufacturers' USB Ethernet, HID and serial drivers from usb-wdm-nonfree
  (https://github.com/yeokm1/usb-wdm-nonfree) in `USBDRV\NONFREE\`.

All free/non-free drivers are optional and installed only if you select
them.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.3.0.0 - 2026-10-28

### New

- **More controller names in Device Manager:** AMD 400 to 800 Series
  chipsets and Zeppelin to Strix Halo processors, Intel 200 to 800 Series
  chipsets and later processors and others.
- **Thunderbolt 3 / USB4:** Tunnelled NVMe drives are more reliable on 98
  SE, ME and 2000.
- **XHCIPFW (98 SE, ME, 2K, XP - 7 32-bit):** a small start-up part some
  USB4 computers need so Windows finds the devices behind their USB4 ports.
  It is ticked with the Thunderbolt 3 / USB4 box and can be unticked. Not
  offered on 8, 8.1 and 10.
- **xHCI98 USB Tree**
  - A USB storage device shows whether it runs UAS or Bulk-Only, and why.
  - The status bar says when a driver update waits for a restart.
  - New item "PCI and bridge diagnostics", also in Save Report.
  - **The "can run faster" notice and the red tray icon** no longer ask for
    more than this computer gives. A 10 Gbit/s device on a computer whose
    USB 3 ports reach only 5 Gbit/s is no longer listed and no longer keeps
    the icon red.

### Changed

- **Vista x64 and 7 x64:** the installer installs the Thunderbolt 3 / USB4
  support only in test-signing mode, as on 64-bit 8 to 10.
- **Stor98 is only for NVMe drives behind a Thunderbolt or USB4 port.** On
  2000 to 8, Stor98 only installs on tunneled NVMe drives so Windows never
  offers it for an internal NVMe drive. On 8.1, 10, 98 SE and ME it still
  matches by the NVMe class (see Known limitations).
- **Stor98 registry settings renamed:** each name now starts with `Stor` on
  every system.
- **The installer:** "Other USB drivers" is now two pages, "Free USB
  drivers" and "Non-free USB drivers".
- **XHCICLN is no longer shipped.** The old devices left behind by an update
  from 2.3.x stay hidden and harmless. Earlier downloads still have the
  tool.

### Fixed

- **Windows 2000:** disabling and re-enabling an xHCI98 controller, or
  updating its driver, could leave it at Code 31 until a restart.
- **Windows 98 SE and ME:**
  - Windows no longer asks for the "Stor98 Storage Controller Disk" when it
    installs a tunnel drive.
  - `uac2` and `usbmidi` can again be ticked for a device's first plug, so a
    USB Audio Class 2.0 device no longer gets Windows' own audio driver,
    which stopped Windows with a fatal exception 0E.
  - a leftover `xhci98.tmp` could replace `xhci98.sys` after a controller
    was added.
- **Thunderbolt 3 / USB4:** several fixes for first plug, hibernation and
  shut down.
- **Stor98:** requests it answers itself now report SCSI status GOOD

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\` but not added to Windows' driver search path. When
  Windows asks for a driver for such a device, point it at that device's
  folder there.

### Known limitations

- **All systems**
  - Standby and hibernation with a Thunderbolt or USB4 device attached are
    not supported: use Safely Remove and unplug the device first.
  - An internal NVMe drive already on Stor98 (put there by hand) keeps it.
- **Windows 98 SE and ME**
  - **Stor98 and internal NVMe drives:** these systems have no NVMe driver
    of their own, and they look for a driver before the Thunderbolt 3 / USB4
    driver can give a drive its id. So Stor98 still matches every NVMe drive
    there, and Windows may offer it for an internal drive with no driver:
    decline it. The installer puts Stor98 on tunnelled drives only.
  - **First plug of a Thunderbolt or USB4 enclosure (98 SE):** when Windows
    asks to restart after installing the enclosure's PCI bridges, answer No.
    If you answered Yes and the drive does not appear, run `asd` from Start,
    Run, tick "Enumerating a device => PCI standard PCI-to-PCI bridge",
    click OK and restart.
  - **USB audio (98 SE):** the first USB audio device plugged in after a
    boot can freeze Windows, also with Windows' own drivers. Plug the device
    in before switching on and leave it plugged in. For a webcam whose
    microphone you don't need, usb-wdm-free's `NOAUD98.INF` avoids it.
  - **Free USB audio and MIDI drivers:** Windows may ask for `wdmaudio.cat`,
    `ks.cat` and other files of its own from the Windows CD. Point it at the
    CD's `WIN98` folder, or click Skip File for the `.cat` files.
- **Windows 2000 updated from 3.2.0.0**
  - An internal NVMe drive added after the update can start on Stor98:
    3.2.0.0 left the key
    `CriticalDeviceDatabase\pci#cc_010802` with Service `stor98`. Delete
    that key with Regedit as an administrator, then restart (the release
    notes, "Known limitations"). XP may do the same.
- **Windows 8.1 and 10**
  - Stor98 still matches by the NVMe class there. Windows' own `stornvme`
    outranks it, so an internal drive keeps `stornvme` which is acceptable
    on those systems. Stor98 goes only on a drive the installer or you put
    it on.

### Choose from 3 downloads

Each zip holds everything in the one before it.
- **`xhci98-3.3.0.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4), Stor98, the installer and the tools. It also has NUSB 3.3's seven
  Windows 98 SE storage files in `USBDRV\NUSB33\`, offered by the installer
  on 98 SE if you don't already have NUSB. If you have NUSB already
  installed, it will be kept.
- **`xhci98-3.3.0.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) 0.1.4 in `USBDRV\FREE\`. **These
  are experimental:** they may not work with every device and can make
  Windows unstable. Please report their problems to that project. New in
  this release:
  - `uac2`: General UAC 2.0 support, MIDI, appear in Windows Volume Control.
  - **Ethernet:**
    - `ax88179` for USB Gigabit Ethernet adapters with an ASIX AX88179 or
      AX88178A chip (98 SE, ME and 2000).
    - `rtl815x` adds the Realtek RTL8153D, RTL8156, RTL8156B, RTL8157 and
      RTL8159, and now also runs on XP to 7, 32-bit and x64.
  - **USB MIDI:** devices play again.
- **`xhci98-3.3.0.0-nonfree.zip`:** also adds a pack of other manufacturers'
  USB Ethernet, HID and serial drivers from usb-wdm-nonfree
  (https://github.com/yeokm1/usb-wdm-nonfree) 1.1.0 in `USBDRV\NONFREE\`.
  New in this release:
  - **More USB Ethernet adapters:** ASIX AX88172, AX88172A, AX88178,
    AX88772B and AX88772C, MosChip MCS7830, Realtek RTL8150 and Davicom
    DM96xx.

All free/non-free drivers are optional and installed only if you select
them.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.2.0.0 - 2026-10-24

### Choose from 3 downloads

Each zip holds everything in the one before it.
- **`xhci98-3.2.0.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4), Stor98, the installer and the tools. It also has NUSB 3.3's seven
  Windows 98 SE storage files in `USBDRV\NUSB33\`, offered by the installer
  on 98 SE if you don't already have NUSB. If you have NUSB already
  installed, it will be kept.
- **`xhci98-3.2.0.0-free.zip`:** adds the free USB class drivers (audio,
  webcam, Ethernet etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) in `USBDRV\FREE\`, with the
  CamCheck webcam preview tool. **These are experimental:** they may not
  work with every device and can make Windows unstable. Please report their
  problems to that project.
- **`xhci98-3.2.0.0-nonfree.zip`:** also adds a pack of other manufacturers'
  USB drivers from usb-wdm-nonfree
  (https://github.com/yeokm1/usb-wdm-nonfree) in `USBDRV\NONFREE\`.

All free/non-free drivers are optional and installed only if you select
them.

### New

- **TB/USB4 Support up to Windows 10,**: For 8, 8.1 and 10, the USB drivers
  are an opt-in install (Windows' own USB 3 driver stays the default).
- **Windows 98 SE and ME:** a new, optional `S98VSD.VXD` lets reads and
  writes of up to 2 MiB chunks for much faster NVMe transfers of about 1
  GB/s.
- **Signed catalogs for the x64 drivers:** each x64 driver now has a
  test-signed catalog, which 64-bit 8, 8.1 and 10 need to install it in
  test-signing mode.
- **Controller names in Device Manager:** each controller name includes the
  chipset, e.g. `xHCI98 USB 3 Controller (Renesas uPD720202)` or `xHCI98 TB3
  Controller (Intel Titan Ridge)`.
- **xHCI98 USB Tree alerts**
  - **"A USB device can run faster" balloon** when:
    - a USB 3 device is on a USB 2.0 port or hub, or has fallen back to USB
      2.0 speed;
    - a 10 Gbit/s device runs at 5 Gbit/s (also on computers whose ports
      only reach 5 Gbit/s);
    - a High Speed (USB 2.0) device runs at Full Speed behind a USB 1.1 hub.
  - **Red tray icon** when something needs you:
    - a device from the "can run faster" list (except a 10 Gbit/s device at
      5 Gbit/s);
    - a Thunderbolt or USB4 device waiting for approval;
    - a controller that stopped answering (a Thunderbolt or USB4 controller
      lost or failed, or `xhci98.sys` stopped after a controller fault).

### Changed

- **Updated installer:** new options and choices for drivers.
- **Stor98 defaults:** on a drive behind a TB/USB4 tunnel, Stor98 is the
  default on 98 SE to 8.1. On Windows 10, Windows' own `stornvme` stays and
  Stor98 is an opt-in.

### Fixed

- **Hibernation and Fast Startup with the Thunderbolt 3 / USB4 driver
  installed:** Windows 8, 8.1 and 10 no longer stop with bug check 0x9F on
  resume, and Windows 2000 to 7 no longer take several minutes to resume.
- **USB driver (`xhci98.sys`) reliability:**
  - a transfer still in flight to a device that was just unplugged no longer
    resets the whole controller (which disconnected every other USB device
    too).
  - cancelling a transfer now finds the right place on the ring, even on
    controllers that skip an expected event, so it no longer corrupts the
    next transfer.
  - a cancelled transfer queued behind another no longer leaves the transfer
    ring corrupted.
  - devices with more than four endpoints (e.g. an Xbox 360 pad) are no
    longer refused.
  - High Speed isochronous devices (webcams, USB Audio 2.0) get a transfer
    ring of their own.
- **Windows ME:** the first shutdown after the install no longer hangs.
- **`INSTALL /eject`** now closes the tunnel on a native PCIe hot-plug port.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\` but not added to Windows' driver search path. When
  Windows asks for a driver for such a device, point it at that device's
  folder there.

### Known limitations

- **Standby and hibernation with a Thunderbolt or USB4 device attached are
  not supported:** use Safely Remove and unplug the device first.
- **Windows 98 SE:** the first USB audio device plugged in after a boot can
  freeze Windows. This includes a webcam with a microphone, and it also
  happens with Windows' own drivers. Plug the device in before switching on
  and leave it plugged in. For a webcam whose microphone you don't need,
  usb-wdm-free's `NOAUD98.INF` avoids it.
- **Windows 98 SE and ME, UAC 2.0 devices** install the free UAC 2.0 driver
  by hand, or Windows' own USB audio driver takes it and stops with a fatal
  exception 0E in `USBAUDIO`. Unplug the audio device and restart (Safe Mode
  if needed), then in Device Manager use Update Driver -> "Display a list of
  all the drivers in a specific location" -> Have Disk ->
  `C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me`, and restart with it plugged
  in.
- **Windows 98 SE and ME, free audio and MIDI drivers:** Windows may ask for
  `wdmaudio.cat`, `ks.cat` and other files of its own from the Windows CD.
  Point it at the CD's `WIN98` folder, or click Skip File for the `.cat`
  files: they only hold signatures, which 98 SE and ME don't check.
- **Windows 98 SE and ME:** close every file and window on a Thunderbolt or
  USB4 drive before unplugging it, or use Safely Remove first.
- **Uninstall on 8.1 and 10:** it may need a restart and a second run to
  finish.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.1.0.0 - 2026-10-19

### New

- **`SETUP.EXE` (beta):** GUI installer for installing/updating. Device
  Manager install is still possible.
- **Uninstall from Add/Remove Programs** (Programs and Features on Vista and
  7), in one step.
- **Stor98 is now part of xhci98:** The installer puts it only on NVMe
  drives behind a Thunderbolt or USB4 tunnel, never on an internal drive.
  **Please do not use this for your internal NVME drives.**
- **One test signature for every x64 driver,** `stor98-test.cer` is gone.

### Changed

- **Registry values can be set machine-wide**: every `Xhci*` value of
  `xhci98.sys` and `XhciCm*` value of `xhcicm.sys` is also read from
  `HKLM\System\CurrentControlSet\Services\USB`, and a value there applies to
  every controller.
- **`XhciFastPollFsLs` `4` interval changes** : an interrupt endpoint's
  `bInterval` of 128 to 143 sets the controller's interval directly, from
  128 (125 us, 8000 Hz) to 143 (4.096 s).
- **Goodbye to `XHCISNAP`:** the xHCI98 USB Tree does what it did. File ->
  Save Report / Snapshot
- **x64 package:** the Thunderbolt filter (auto-rescan) INF `xhcicmf.inf`
  now sits in `release-x64\` itself; the `nt\` folder is gone. The x86
  package keeps `nt\`.

### Fixed

- **Windows 98 SE and ME, after a Stand by:** plugging in a second USB4 NVMe
  enclosure no longer drops the first one's drive.
- **Windows 2000 to 7:** a Thunderbolt or USB4 drive stays in Safely Remove
  Hardware after a disable and enable or a driver update. The safe-removal
  service follows `XhciCmSafeEject`.
- **Uninstall on 2000 to 7:** it no longer leaves an entry that kept the
  Thunderbolt controller at Code 19.
- **Windows 98 SE and ME uninstall:** no stale "OEM Name" entries are left
  behind.
- **Stor98 on 98 SE and ME:** it copies a request whose buffer is not
  physically contiguous instead of failing it.

### Updating

- **Use `SETUP.EXE`**. Registry values are kept. To keep Thunderbolt 3 /
  USB4 support, tick its box.
- **Windows 98 SE and ME with an NVMe enclosure:** tick "Please disconnect
  all external NVMe drives before proceeding" and unplug them. Stor98 is
  replaced at the next start. Afterwards, shut down and start Windows again
  rather than restarting.
- **Vista x64 and 7 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on.

### Known limitations

- **Standby and hibernation with a Thunderbolt or USB4 device attached are
  not supported:** stop and unplug the device first.
- **A drive attached during the install or update** appears in Safely Remove
  Hardware only after a restart or a replug.
- **Windows 2000 to 7 with a USB4 host:** ejecting a dock, or an enclosure
  with its own PCIe switch, stops it safely, but its connection stays up
  until you unplug it.
- **Windows 98 SE and ME:** the first time an NVMe enclosure is plugged in,
  Windows' wizard may ask for the "Stor98 Storage Controller Disk". Point it
  at the driver install folder.
- **A dual-mode NVMe enclosure** (USB4 or Thunderbolt, and USB) may come up
  as a USB (UAS) disk instead. Unplug it and plug it in again to get the
  Thunderbolt or USB4 connection.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that may not be
accounted for due to the large xHCI/USB 3/4/TB specification and numerous
host controller implementations and devices. **USB4/TB/NVME features are
even more highly experimental so use at your own risk!**

Appreciate all issue reports!

## 3.0.0.0 - 2026-10-17

Adds experimental **Thunderbolt 3 and USB4** support through a third driver,
`xhcicm.sys` and `stor98.mpd` hot-pluggable NVME driver. Use those at your
own risk.

### New

- **Thunderbolt 3 and USB4 (`xhcicm.sys`, experimental):** docks, and NVMe
  enclosures reach Windows 98 SE to 7. On Intel Thunderbolt controllers it
  works with the firmware's connection manager; on USB4 hosts without one
  (AMD, ASMedia, Intel Lunar Lake and later), it is the connection manager.
- **Stor98 (`stor98.mpd`, experimental):** the NVMe driver for Windows 98 SE
  to 7, a separate project (https://github.com/yeokm1/stor98), shipped
  unchanged with its own licence and readme.
- **`INSTALL.BAT` (beta):** installs, updates and uninstalls every driver in
  one run on 2000 to 7, and stages the files with the remaining steps
  printed on 98 SE and ME. Device Manager install still works as before if
  INSTALL.BAT does not work for you.
- **xHCI98 USB Tree (`XHCITR.EXE` / `XHCITR64.EXE`):** every USB and
  Thunderbolt/USB4 controller and device with details, a tray icon to safely
  remove USB storage and Thunderbolt devices (also on 98 SE and ME),
  resources and PCI configuration, both drivers' logs, and a Logging level
  setting.

### Changed

- Both drivers now log at level 2 by default on a fresh install (a tiny
  speed cost); an update keeps your existing value. Set `0` in the USB
  Tree's View > Logging level to turn it off.
- `XHCISNAP` and its batch files still ship but will be deprecated in a
  subsequent release; the USB Tree replaces them.

### Updating

- Run `INSTALL.BAT`, or update "xHCI98 USB 3.x eXtensible Host Controller"
  and then its Root Hub through Device Manager as before, then UAS. Registry
  values are kept.
- Thunderbolt/USB4 machines: answer Y to "Install Thunderbolt 3 / USB4
  support?" (or `INSTALL /thunderbolt`). For an NVMe drive, install Stor98
  from `release-x86\` or `release-x64\` with Have Disk on the NVMe
  controller the first time it appears.
- Windows 98 SE and ME: restart after updating, even if Windows does not
  ask.
- Vista x64 and 7 x64: the x64 drivers are test-signed, so test-signing mode
  must be on.

### Known limitations

- The BIOS must boot in legacy/CSM mode and leave the Thunderbolt controller
  on: turn "Thunderbolt BIOS Assist" off on 98 SE and ME.
- Windows 98 SE and ME: after Safely Remove of a device with its own PCI
  Express switch, unplug it before using it again; a Device Manager Refresh
  alone leaves it at Code 29.
- Windows 98 SE and ME: May only be able to handle one NVME drive at a time.
  Plugging in a second may drop the first. (To be fixed in another release)
  Win 2K and above not affected.
- Approval is automatic by default and not remembered; a tunnelled device
  can reach all memory (as on any Thunderbolt system without IOMMU
  protection).

Companion blog post: https://yeokhengmeng.com/2026/10/xhci98-usb3-usb4/

## 2.4.0.0 - 2026-10-14

### New

- **xHCI 0.96 controllers are now accepted**, such as the first-generation
  NEC/Renesas uPD720200/200A (`1033:0194`). `XhciAccept096` = `0` restores
  the old 1.0 floor.
- **Controller quirks handling from Linux:** 14 new registry values
  (`XhciMsiAvoid`, `XhciStreamsOff`, `XhciResumeReinit`, ...) for known
  problems on Fresco Logic, ASMedia, AMD, TI, Etron, VIA VL805 and some
  Intel controllers.
- **Windows 2000 can now be installed from the F6 floppy.** Copy `USBD.SY_`,
  `HIDCLASS.SY_` and `HIDPARSE.SY_` from your Windows 2000 CD's `I386`
  folder to the floppy, then pick the new entry **"xHCI98 USB 3.x Host
  Controller (32-bit Windows 2000, files from CD)"**. These files are not
  included in the download for licensing reasons.
- **hidusbf's polling-rate override now works when set on a composite
  device's entry** (#17).
- **Cleanup tool `XHCICLN.EXE` / `XHCICL64.EXE`** in `tools\xhcicln\`. It
  lists, and on Windows 2000 to 7 removes, leftover entries from earlier
  versions (see "Updating"). On 98 SE / ME it only lists.
- **Field-report batch files:** `SNAPON.BAT`, `SNAPGET.BAT` and
  `SNAPOFF.BAT` in `tools\xhcisnap\`, plus one in `tools\xhciqual\` for
  MS-DOS. Run one and attach its output instead of following a list.
- **Windows 98 SE / ME silent install** of keyboards, mice, hubs and USB
  sticks, opt-in: `XhciSilentInstall` (written as `0`, off) is switched on
  by `silent-on.bat` in `tools\silent\`. Thanks to SweetLow for how the
  silent install is decided and for the class list.
- **Finer system timer while polling** (`XhciPollFineTimer`), **off by
  default**.

### Changed

- **The driver's own device IDs moved from `XHCI98\...` to
  `USB\XHCI98_...`** (#17). After updating, the root hub, hubs and composite
  devices appear as new entries. **On 98 SE, XP and XP x64, USB keyboard and
  mouse may not work during the update**, so keep a PS/2 one handy (see
  "Updating").
- **The tools are now in a `tools\` folder** (`xhciqual\`, `xhcisnap\`,
  `portsw\`, `silent\`, `xhcicln\`).
- **The 32-bit helper `xhcimsi.sys` is now `xhcihlpr.sys`** and is also
  installed on Windows 2000 and XP, where no restart is forced.

### Fixed

- **Endpoint memory is now held until the controller confirms it has let
  go** when a device switches alternate settings (e.g. USB audio). A device
  that refuses that switch on an interface with only one setting is now
  handled as Linux does.
- **External hubs no longer register the "USB device" interface**, matching
  Windows' own hubs (#17).

### Updating

- Update only the **"xHCI98 USB 3.x eXtensible Host Controller"**, pointed
  at `release-x86\` or `release-x64\`. The root hub, hubs and composite
  devices are found again as new hardware and install by themselves from the
  same package. (Subsequent updates require updating both the Host
  Controller and Root Hub)
- You are recommended to update the **UAS** driver as well.
- **Updating from 2.3.x on Windows 98 SE, XP or XP x64:** keep a keyboard
  and mouse that don't go through this driver (PS/2, or another controller)
  at hand. USB input on this controller can be unavailable until the new
  `USB\XHCI98_` entries are installed.
- **Old `XHCI98\` entries** stay behind as hidden devices. Run `XHCICLN.EXE
  -remove` (`XHCICL64.EXE` on x64) after the restart to remove them.
- Your registry values are kept and the new ones added. On Windows 98 SE /
  ME, restart afterwards. On 32-bit Vista and 7, restart even if Windows
  doesn't ask.

### Known limitations (new)

- **Windows ME:** Shut Down can hang after the controller has been disabled
  and re-enabled in Device Manager (2.3.1.0 too).
- **Windows 2000 from the F6 floppy:** using the first (XP) entry still
  stops at the first GUI-mode boot (`STOP c000026c`) when a USB keyboard,
  mouse or stick was used in text mode. Use the Windows 2000 entry.

**This is a fairly new driver. It may have bugs or edge cases that may not
be accounted for due to the large xHCI/USB 3.x specification and numerous
host controller implementations and devices. Please report any issues if you
find any.**

## 2.3.1.0 - 2026-10-13

### Fixed

- **Fixed a port reset still running when the BIOS hands the controller to
  the driver.** The driver gave up on the port instead of waiting.
- **Shutdown:** once the controller is powered off for shutdown, new
  transfers, commands and port resets are refused.
- **Lost interrupts are recovered sooner:** an event left waiting when the
  interrupt is re-armed is handled at once instead of after up to 100 ms

### Changed

- **`XhciForceBulkOnly` now defaults to `1` (Bulk-Only) when the value is
  missing** for compatibility. Installed systems keep UAS, since the INF
  writes `0`.
- **Windows ME instructions changed to need no USB 2.0 stack.**
- **`portsw.vbs`, `portsw-on.bat` and `portsw-off.bat` are in one `portsw\`
  folder** at the top of the download.

### Updating

- Remember to update both **"xHCI98 USB 3.x eXtensible Host Controller"**
  and **"xHCI98 USB 3.x Root Hub"**, both pointed at `release-x86\` or
  `release-x64\`. You are recommended to update the **UAS** driver as well.
- Your registry values are kept and the new ones added. On Windows 98 SE /
  ME, restart afterwards.

**This is a fairly new driver. It may have bugs or edge cases that may not
be accounted for due to the large xHCI/USB 3.x specification and numerous
host controller implementations and devices. Please report any issues if you
find any.**

## 2.3.0.0 - 2026-10-12

### New

- **Polling for controllers with no working interrupt.** A controller with
  no interrupt pin or whose interrupt never arrives is now polled as a last
  resort. New keys `XhciInterruptMode`, `XhciPollIdleMs`,
  `XhciPollActiveMs`.
- **Message-signalled interrupts (MSI) on Vista and 7**, on x64 directly and
  on 32-bit through a small helper driver (`xhcimsi.sys`). The 32-bit Vista
  and 7 install asks for a restart.
- **Test-signed x64 drivers.** Vista x64 and 7 x64 load the driver in
  test-signing mode (`bcdedit /set testsigning on` once, then restart),
  instead of needing F8 at every start.
- **Configurable UAS transfers:** (`XhciMaxTransferKB`),
- **Selective suspend**, off by default (`XhciSelectiveSuspend` = `1` to
  enable).

### Fixed

- **Missing audio playback** on some audio devices.
- **Low- and Full-Speed devices behind the VIA VL805's built-in hub** were
  refused (#18). The fix was built without the hardware.
- **A UAS drive now recovers from a transaction error in place**, where
  before it was reset.
- **Interrupt routing repair for the Dell Latitude E7250**, whose firmware
  sends the interrupt to the wrong line (`XhciIntelIrqRoute`).
- **Composite devices** now get their own Device Manager entry.
- **USBView shows correct details** manufacturer, product, serial number and
  interface names.
- **Intel port switchover off by default** `XhciIntelPortSwitch = 0` on new
  installs. `portsw-on.bat` and `portsw-off.bat` turn it on or off.

### Updating

- Remember to update both **"xHCI98 USB 3.x eXtensible Host Controller"**
  and **"xHCI98 USB 3.x Root Hub"**, both pointed at `release-x86\` or
  `release-x64\`. UAS driver should be updated as well.
- Your registry values are kept and the new ones added. On Windows 98 SE /
  ME, restart afterwards.

**This is a fairly new driver. It may have bugs or edge cases that may not
be accounted for due to the large xHCI/USB 3.x specification and numerous
host controller implementations and devices. Please report any issues if you
find any.**

## 2.2.0.0 - 2026-10-07

### New

- **SuperSpeed now works on Intel Sunrise Point-LP (`8086:9D2F`)** - e.g.
  ThinkPad E460, HP EliteBook 850 G5. Previously USB 3 sticks were not seen
  and USB 3 hubs showed only their USB 2.0 half.
- **Devices recover from controller faults** instead of staying dead until
  replugged
- **New settings:** `XhciTolerance` (on by default; `0` turns the fault
  handling off), `XhciIntervalCap` and `XhciAvgTrbEsit` (for AMD
  controllers).
- **`XhciIntelPortSwitch` = `2`:** moves the switchable USB 3 connectors to
  this driver on any Intel controller, not only the six listed. At your own
  risk; leave it at `1` unless you know your chipset has the switch
  registers.
- **Better diagnostics:** `XHCISNAP` reports now say why a device was not
  enumerated and count every fault, so a report from a misbehaving machine
  names its own cause.

### Updating

- Remember to update both **"xHCI98 USB 3.x eXtensible Host Controller"**
  and **"xHCI98 USB 3.x Root Hub"**, both pointed at `release-x86\` or
  `release-x64\`.
- Your registry values are kept and the new ones added. On Windows 98 SE /
  ME, restart afterwards.

### Known limitations (new)

- Windows 2000: after a controller recovery, a mounted USB drive may come
  back at Code 31 until it is replugged.
- A device the controller keeps faulting on is removed after three tries;
  its port is held until the device is unplugged for a few seconds, or until
  the controller restarts.

**The architecture of this release is a drastic change from previous 1.x.x.x
USB 2.0 miniport releases. It may have bugs or edge cases that may not be
accounted for due to the large xHCI/USB 3.x specification and numerous host
controller implementations and devices. Please report any issues if you find
any.**

## 2.1.1.0 - 2026-10-06

### New
- **The install writes every registry value the driver reads.** All eight
  values are written at their defaults, so you can find and change them in
  the controller's driver key.
  - A value is written only where it is missing, so a value you changed
    survives an install or update.
  - A machine updated straight from 1.2.0.0 keeps that release's
    `XhciImodInterval250ns` of `500`. Set it to `160` by hand if you want.
- **Fixed: the Power tab's current for SuperSpeed devices.** Under 2.1.0.0
  it showed a quarter of the real draw. It now shows the device's own
  figure, up to 510 mA (anything above reads 510 mA).
  - `XHCISNAP` reports each SuperSpeed device's exact declared current
    (`dev.ss.maxpower.ma`, with `XhciLogVerbosity` raised).
- **Intel 7-, 8- and 9-series chipsets (Ivy Bridge to Broadwell, and
  C610/X99): the switchable ports now work.**
  - The driver moves the switchable connectors (usually the blue ones) from
    the USB 2.0 controller to itself at each start.
  - Before this, with the firmware on Auto, devices on those connectors were
    invisible to this driver.
  - xHCI device ids this is applied to: `1E31`, `8C31`, `9C31`, `8CB1`,
    `9CB1`, `8D31`.
  - To turn it off, set `XhciIntelPortSwitch` to `0`.

### Known limitations
- the SuperSpeed Power-tab figure is now capped at 510 mA due to Device
  Manager limitations.

**The architecture of this release is a drastic change from previous 1.x.x.x
USB 2.0 miniport releases. It may have bugs or edge cases that may not be
accounted for due to the large xHCI/USB 3.x specification and numerous host
controller implementations and devices. Please report any issues if you find
any.**

## 2.1.0.0 - 2026-10-05

### New

- External hubs appear in Device Manager with their devices beneath them
- Devices with a serial number keep their Device Manager entry when moved to
  another port
- Devices show their own product name instead of "USB Device" where no
  Windows INF names them
- `txtsetup.oem` in every driver directory: load the driver at the F6 prompt
  in Windows 2000/XP/XP x64 Setup and the Recovery Console
- SweetLow's hidusbf works under this driver, at root ports and behind hubs
- `XhciFastPollFsLs` (not set by default, experimental): polls
  Low/Full-Speed mice at 2000 to 8000 Hz with hidusbf

### Fixed

- Windows ME: re-enabling the controller with a USB mouse attached no longer
  hangs
- The root hub's Power tab shows each device's power in mA instead of
  "unknown"

**The architecture of this release is a drastic change from previous 1.x.x.x
USB 2.0 miniport releases. It may have bugs or edge cases that may not be
accounted for due to the large xHCI/USB 3.x specification and numerous host
controller implementations and devices. Please report any issues if you find
any.**

## 2.0.0.0 - 2026-10-04

- A new driver generation. xhci98.sys is now a complete USB host controller
  driver. It runs the controller, the root hub, every hub behind it and
  composite-device splitting itself, replacing Windows' usbport.sys,
  usbhub.sys and usbccgp.sys. Windows own class drivers (keyboard, mouse,
  storage, audio, network) sit on top unchanged.
- SuperSpeed (USB 3.x) devices and SuperSpeed hubs are supported, alongside
  High, Full and Low Speed devices.
- Every device runs at its true speed. The virtual High-Speed hub and the
  "everything is High Speed" reporting of 1.x are gone, along with the
  polling, audio and USB 1.1 hub problems they caused.
- UAS (USB Attached SCSI) storage through the new xhciuas.sys, with streams
  at SuperSpeed and streamless UAS at High Speed.
- UAS drives that also offer Bulk-Only can be forced to Bulk-Only with
  XhciForceBulkOnly.

**The architecture of this release is a drastic change from previous 1.x.x.x
USB 2.0 miniport releases. It may have bugs or edge cases that may not be
accounted for due to the large xHCI/USB 3.x specification and numerous host
controller implementations and devices. Please report any issues if you find
any.**

## 1.2.0.0 - 2026-10-02

This is the last release that follows the current miniport architecture. The
next release will be a full USB 3.0 Host Controller Driver to get proper USB
3.0 speeds.

- **New: virtual USB 2.0 hub (experimental, off by default).** When it's on,
  a Full- or Low-Speed device plugged into a root port is reported to
  Windows at its true speed instead of as High Speed. Only turn it on if you
  know what you're doing. It is controlled by `XhciVirtualHSHub` in the
  controller's driver key:
  - `0`: off (the default)
  - `1`: on when needed. A virtual hub appears above a Full- or Low-Speed
    device on a root port and goes away when the device is unplugged.
  - `2`: always on. Every USB 2.0 port has a virtual hub from start to stop.
- **With the virtual hub on:**
  - A mouse on a root port polls at the rate it asks for, and a Full-Speed
    audio device on a root port plays from Windows XP onward.
  - On Windows Vista and 7, a USB 1.1 hub on a root port no longer crashes
    the machine.
  - The virtual hub counts as one hub level, so you can chain one fewer
    external hub than usual.
- **Fixed:** a Low-Speed device behind a hub showed Code 10 under SweetLow's
  stack on Windows 98 at 250 Hz and faster (#4).
- **Fixed:** a High-Speed interrupt device behind a USB 2.0 hub could be
  polled every 125 us. It is now polled at the interval usbport sets for it.

## 1.1.1.0 - 2026-09-24

### New features

- The xHCI controller's properties in Device Manager now have an Advanced
  tab with "Disable USB error detection" checkbox and "Bandwidth Usage"
  button. (Suggested in this issue
  https://github.com/yeokm1/xhci98/issues/4)
- The install sets a shorter default interrupt moderation interval of 500
  (0.125ms). Transfer speeds should approach 33 MB/s which is near the
  practical maximum of USB 2.0. Previously this was hardcoded at 4000 (1ms)
  and only managed 18 MB/s.

### Known limitation

- USB Audio playback may more likely stutter with high USB traffic like file
  transfers at default moderation interval. If you prefer to prioritise
  audio quality, experiment with increasing the value or change it back to
  4000. You can change this with a registry key XhciImodInterval250ns.
  Details in tuning section of the main readme.

## 1.1.0.0 - 2026-09-18

Changes:
- Windows XP x64, Vista (x86/x64) and 7 (x86/x64) are now supported.
- Use `release-x86` for 32-bit Windows or `release-x64` for 64-bit Windows.
- The driver no longer changes a machine-wide registry to disable suspend.
  It does it internally via a `USB_MINIPORT_FLAGS_DISABLE_SS` flag to
  `usbport.sys`. You may delete the
  `HKLM\System\CurrentControlSet\Services\USB\DisableSelectiveSuspend` key
  if you have upgraded from a previous version.
- Several reliability fixes including one that could crash machines with
  more than one processor.
- Small fixes to `XHCIQUAL` and `XHCISNAP`.

Additional issues:
- **Windows Vista and 7: an old USB 1.1 hub plugged straight into the PC
  will crash Windows** once you use a mouse or keyboard behind it. Plug
  those devices directly into the PC, or use a USB 2.0 hub.
- **Windows XP and later: a USB sound card or headset plugged straight into
  the PC plays no sound**, even though Windows shows it playing. Plug it in
  through a USB 2.0 hub and it works. Windows 98 and 2000 are not affected.
- **Windows 7: disabling the USB controller in Device Manager can hang**,
  and so can the restart after it. This was seen on the one real machine
  tried, and the cause is not known yet.

## 1.0.2.0 - 2026-09-07

This is more of a robustness release. No change in main driver features.

- The install now supplies usbui.dll from Windows' own installation source
  only if absent. Will create extra property page in Device Manager.
- Bug fixes to driver regarding endpoint edge cases (More details in release
  history)

## 1.0.1.0 - 2026-09-04

- Added 32-bit Windows XP SP3 support for completeness. (Backported Win 8
  USB 3.0 drivers exist)
- On Windows 2000 and XP, the install now places usbport.sys, usbd.sys and
  usbhub.sys from the OS's own driver cache. Before this, a machine that
  never had another USB controller could install the driver but not load it.
- Idle suspend is now disabled on the NT path too as on Windows 98. XP's
  usbport idled the controller about thirty seconds after start and anything
  plugged in afterwards was invisible.
- **[Bugfix]**: One driver code change: XP's hub can re-create a device
  mid-enumeration through a second handle and then remove the first. The
  driver no longer reads that removal as the live pipe closing. Before, a
  mass-storage or composite device failed on its first attach on XP and
  worked on a replug. Windows 98 and 2000 never trigger it and read
  unchanged on the same binary but may silently have this fault too.

## 1.0.0.1 - 2026-09-02

This version is functionally identical to v1.0.0.0. If you already have
that, you can stay with that.

Only changes are the install procedure to do the following:

- Windows ME support
- System file dependencies like usbd.sys and usbhub.sys are now pulled from
  Windows install CD path instead of provided in this package
- Driver tested successfully against Sweetlow's USB stack
  (http://sweetlow.orgfree.com/download/usb20_win9x.zip) for Win 98SE and
  Win ME as mentioned in this issue
  https://github.com/yeokm1/xhci98/issues/1

## 1.0.0.0 - 2026-08-30

Initial release.

(Asset removed.)
