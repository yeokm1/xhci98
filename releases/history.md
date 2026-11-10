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

## 3.4.3.0 - 2026-11-10

### New

- **Standby and hibernation with a Thunderbolt or USB4 drive attached**
  are supported on Windows 2000 and later. The USB4 connection manager
  puts the devices to sleep in order and builds their connections again
  at resume (`XhciCmSleepEntry`, on by default). On Windows 98 SE and
  ME, stop and unplug the drive first ("Known limitations").
- **A USB 3 device whose SuperSpeed link keeps failing** (three resets
  within a minute, then a fourth failure), at a root port or behind a
  USB 3 hub, is moved to the USB 2.0 side of the same connector or hub,
  where it runs at High Speed, instead of being reset again and again.
  `XhciSsFlapFallback` `0` leaves such a device off until it is plugged
  in again.
- **`XhciDmaStaging`** (`0` by default): `1` passes every transfer's
  data through memory of the driver's own, so that a disable, update or
  removal of a controller that has stopped answering completes instead
  of waiting, at the cost of one more copy of the data.
  `XhciStagePoolKiB` sets the size of that memory
  (`docs/using/tuning.md`).
- **Windows 98 SE and ME: a Thunderbolt dock's second and further USB
  controllers**, which Windows leaves at Code 29 for want of an
  interrupt, start without one from the dock's first plug, `xhci98.sys`
  polling them (`XhciCmNoIrqStart`).
- **Windows 98 SE and ME: the Thunderbolt or USB4 controller's
  interrupt.** `XhciCmRingInterrupt` `1`, with `XhciPfwNhiLine` `0`,
  now works on these systems too: a plug, an unplug and network traffic
  are reported at once (off by default; "Thunderbolt networking speed on
  98 SE and ME: the controller's interrupt", `docs/using/tuning.md`).
  On every system network traffic is reported by that interrupt where
  it is on (`XhciCmNetInterrupt`, on by default).
- **Daisy chains on USB4 hosts whose tunnels xHCI98 builds:** on 98 SE
  and ME a device behind another device is set up when it is plugged
  in, removed when it is unplugged, and marked removable; on 2000 to 10
  stopping it closes its own connection.
- **A device already attached when Windows starts, on a USB4 host that
  does not tell the driver which of its ports leads where** (the
  ASMedia ASM4242): `XhciCmInferAtStart` `1` (off by default) lets the
  driver work out its port when only one such device is attached and
  two separate readings agree, so that safely removing it closes its
  connection.
- **Stor98 on Windows 98 SE: `StorTakeFirmware`** (`0` by default): `1`
  takes an NVMe controller the BIOS has set up, which Stor98 otherwise
  leaves to the BIOS; `stortakefirmware=1` in one controller's
  `AdapterSettings` takes that one alone. Taking it can hang Windows'
  start or corrupt data if the BIOS still uses the drive. Ignored on ME.
- **32-bit Windows 10, version 2004 or later: `XHCIPFW.SYS`**, which
  closes PCI bridges' windows above 4 GB, is offered as on 8 and 8.1.
- **The USB Tree: USB drive notices**, off unless you tick "USB drive
  notices" on the Settings window's xHCI98 USB Tree tab, say when a USB
  drive is connected or removed ("SanDisk Ultra (F:) is connected.").
- **The USB Tree: Thunderbolt networking.** A node under the Thunderbolt
  or USB4 controller shows each link's state, the other computer, the
  speed, the addresses and the counters, also in the saved report and
  the snapshot (where the addresses and the other computer's name are
  left out).
- **Two tips for Thunderbolt networking speed on 98 SE and ME**
  (`docs/using/tuning.md`): the systems' own TCP receive window
  (`DefaultRcvWindow` `64240`) can take data coming in from about 10 to
  about 100 Mbit/s; the controller's interrupt (above) makes the link
  about three times faster in both directions, where the BIOS gives the
  controller an interrupt no other device holds.

### Changed

- **The installer**
  - A reinstall or update at the same version replaces every installed
    file of this package whose bytes differ from the package's, on every
    system, as it replaces an older file. A newer version is still kept
    unless `/force` is given. `/list` says "Keep (same version)" or
    "Replace files (same version)".
  - Windows 98 SE and ME restart at the finish like every other system:
    after a Stor98 update, and on 98 SE after the storage drivers' copy
    with a UAS disk at Code 2, Setup offers "Restart Windows now" where
    it asked you to shut down and start Windows again. Disconnect your
    external NVMe drives before a Stor98 update, as before.
  - The Advanced lists show `XhciSsFlapFallback` and Stor98's
    `StorTakeFirmware` (98 SE; greyed on ME), and `XhciCmRingInterrupt`
    on every system.
  - The refusal while Windows is installing something names the program
    to run again, `SETUP.EXE` or `INSTALL.BAT`.
  - On 8, 8.1 and 10 `XHCIPFW.SYS` acts only at a full start: restart
    Windows after installing or updating it (with fast startup on, Shut
    down and power on is not a full start).
  - Windows 98 SE and ME: `INSTALL.BAT` waits for Setup's window and
    returns its exit code.
  - The text-mode Setup (F6) entry for Windows 2000 also serves 32-bit
    XP: "xHCI98 USB 3 Controller (2000/XP, USB keyboard/mouse, files
    from CD)".
- **The USB Tree**
  - The Settings window shows `XhciSsFlapFallback` and Stor98's
    `StorTakeFirmware`.
  - When a tunnel stays up after an eject because the driver does not
    know which port the device is on, the eject says so.
  - `/eject` with the ID of a device inside a Thunderbolt or USB4 device
    (its drive, say) stops that whole device.
  - A unit's details say "Marked removable." on every system.
- **Windows 8.1:** a tunnelled NVMe drive keeps its Thunderbolt id, and
  so Stor98, across a disable, a driver update or another restart of
  its own devices; on 8.1 Stor98 matches a drive by that id alone.
- **USB4 hosts whose tunnels xHCI98 builds:** the port of a tunnel found
  up at start is learned only from the tunnel's own far end, so a
  function that answers late is never taken for the device.
- **The USB devices** answer Windows' bus information request as
  Windows' own USB stack does.

### Fixed

- **Windows 2000: no USB audio played with 3.4.2.0** (GitHub issue 32).
  3.4.2.0 refused the request with which Windows 2000's own USB audio
  driver chooses its stream, because the request's header gave a length
  of 0 ("Your audio hardware cannot play files like the current file").
  A request whose header states a length too short for it is now served
  and counted, as Windows' own USB stack serves it.
- **Windows 98 SE and ME, Thunderbolt networking with the controller's
  interrupt on:** a fatal exception 0E inside Windows' TCP/IP (MSTCP)
  while sending, and data coming in that stalled. The network driver
  now hands Windows what it receives and the sends it completes from an
  NDIS timer, as these systems expect.
- **Thunderbolt networking with a Windows 11 computer:** the link came
  up and carried nothing, and after the cable was plugged in again at
  the Windows end the other computer was not found again. Both work.
- **An audio or video request whose last packet failed part-way** could
  wait for its cancellation; it ends at once.
- **A USB 2.0 endpoint whose maximum packet size its speed does not
  list** was refused; it is opened as declared, or at the speed's
  maximum.
- **A SuperSpeed hub port whose move to USB 2.0 failed** was left with
  nothing more reported; the move is tried again.
- **XHCIPERF** (the developer diagnostic in the source tree) on 98 SE
  and ME: Stop and closing the window wait for the disk to answer a
  request already sent, which these systems cannot cancel.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system: point it at `98-me\`, `nt-x86\` or
  `nt-x64\`.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\`; the ones you tick are also added to Windows' driver
  search path. When Windows asks for a driver for another such device,
  point it at that device's folder there. If the installer reports a
  controller "not reached in this run", restart Windows and run it
  again.

### Known limitations

- **All systems**
  - Standby and hibernation with a Thunderbolt or USB4 drive attached
    are supported on Windows 2000 and later. On Windows 98 SE and ME,
    stop and unplug the drive first. On a computer whose firmware does
    not restore the Thunderbolt or USB4 connection at resume, stop the
    drive before standby or hibernation.
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
  - A Thunderbolt or USB4 dock plugged in after Windows starts needs the
    BIOS to reserve bus numbers for those ports, as boards commonly do;
    without that the dock's switch stays at Code 31 on 7, and nothing
    appears behind the port on XP or 98 SE.
  - Thunderbolt networking: one link at a time; a computer behind a dock
    or another router is not reached.
  - A drive in a daisy chain, once stopped, keeps its connection until it
    is unplugged, except on 2000 to 10 with a USB4 host whose tunnels
    xHCI98 builds: unplug it after the stop.
  - A device attached when Windows started, on a USB4 host that does not
    tell the driver which of its ports leads where, keeps its connection
    after a stop until it is unplugged, unless `XhciCmInferAtStart` is
    `1`; the eject says so.
  - **Stor98, 4 KiB-sector drives:** a drive partitioned with 512-byte
    sectors (on 98 SE to 7) and one partitioned with 4096-byte sectors
    (on 8 to 10) are not interchangeable: the other view sees its
    partitions and files as damaged. Back up first, or reformat the
    drive itself to 512-byte sectors with its maker's tool.
- **Windows 2000 to 8.1**
  - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
    Device": unplug it, restart Windows (reset the computer if the
    shutdown hangs), run `cmd /c "set devmgr_show_nonpresent_devices=1&&
    devmgmt.msc"`, choose View, Show hidden devices, update the greyed
    "USB Audio Device" to `uac2`, then plug the device in.
- **Windows 2000, XP and XP x64**
  - **A USB 3 controller the BIOS gives these systems no interrupt for**
    (some HP laptops of 2012 with the Intel 7 Series USB 3 controller)
    is polled where Windows can give it no line, which uses a little
    more processor time; on Windows 2000 on the listed computers it is
    always polled. On a computer whose BIOS turns that controller on
    before Windows starts, Windows 2000 with an ACPI computer type still
    stops at start (STOP 0x000000A5): install Windows 2000 there as
    "Standard PC" (F7 in text-mode Setup). On the listed computers
    `XhciIntelNoRouteIrq` `0` brings that stop back under an ACPI
    computer type. If `0` was set after the last start that reached the
    desktop, "Last Known Good Configuration" at F8 undoes it; otherwise
    only "Standard PC" starts Windows 2000 there
    (`docs/using/tuning.md`).
  - **Docks:** the first time a dock or an enclosure with a switch is
    plugged in, its devices may stop and start again once, a few seconds
    after it is plugged in (after its Found New Hardware windows are
    answered), while xHCI98 sets up its PCI Express ports. If a drive
    behind it is already in use, unplug it safely and plug it in again
    instead. A device added later behind a port that has no I/O range (a
    network adapter behind a chained dock) is placed after the dock is
    unplugged and plugged in again.
  - **NVMe enclosures:** the first time one is plugged in, two Found New
    Hardware wizards appear. The first finds no driver (finish it either
    way); the second offers "Stor98 Storage Controller for NVMe SSDs":
    follow it to the end.
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
- **Windows 2000**
  - The first time a Thunderbolt dock is plugged in, Windows asks to
    restart for each of its PCI bridges; until the restart some of the
    dock's devices do not start.
  - With the Standard PC HAL, a drive in a daisy chain plugged in after
    startup gets no interrupt (Code 10); one chained at power-on starts.
- **Windows XP**
  - Updating asks one more "Windows Logo testing" question for each UAS
    disk that is not plugged in; `/yes` cannot answer it.
- **Windows Vista and 7**
  - Stop a dock before unplugging it, as on 2000 and XP, when a keyboard,
    mouse or similar device is on its own USB controller and the computer
    has a Thunderbolt 3 controller.
  - 32-bit: with safe removal off, a dock's own USB controller shows Code 1
    after its first plug until Windows is restarted once.
- **Windows 98 SE and ME**
  - **Windows ME (and rarely 98 SE): with a Thunderbolt dock or chain
    attached at power-on, the first start after the install can stop**
    with a fatal exception 0E. Setting `XhciCmReenum` to `0` avoids it,
    at the cost of no automatic re-enumeration on 9x: Windows then finds
    devices plugged in behind the Thunderbolt port only at Device
    Manager's Refresh.
  - **A USB disk left plugged in when the computer starts** can hold
    Windows at startup for minutes, or stop it. Plug USB disks in after
    Windows has started. To help find a fix, you can send an EDD report
    (`docs/using/edd-reports.md`).
  - **A drive the BIOS has set up** at power-on (one it can start from)
    stays the BIOS's unless `StorTakeFirmware` is `1` (98 SE): its
    Stor98 entry shows Code 10. 98 SE reaches the drive through the
    BIOS; ME may not reach it at all.
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
  - **Rarely, a start stops** with "A fatal exception 0E has occurred at
    019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn the
    computer off and on again; the next start works.
  - **USB audio (98 SE):** the first USB audio device plugged in after a
    boot can freeze Windows, also with Windows' own drivers. Plug the device
    in before switching on and leave it plugged in. For a webcam whose
    microphone you don't need, usb-wdm-free's `NOAUD98.INF` avoids it.
  - **Unplugging USB audio while it plays (98 SE):** stop audio playback
    before unplugging a USB audio device. Unplugged while Windows' own
    USB audio driver plays it, the device can freeze Windows. ME no
    longer does this.
  - **Free USB audio and MIDI drivers:** Windows may ask for `wdmaudio.cat`,
    `ks.cat` and other files of its own from the Windows CD. Point it at the
    CD's `WIN98` folder, or click Skip File for the `.cat` files.
  - **Keyboard media keys (98 SE):** a keyboard's volume and mute keys
    work after a restart; its other media keys need the keyboard maker's
    software. ME, 2000 and XP handle them.
  - **A shared IRQ held by another device (98 SE):**
    Windows crawls or freezes once xHCI98 is installed when a device with
    no working driver (an NVMe drive the BIOS still runs, or one whose
    driver failed) shares an xHCI98 controller's IRQ. Give that device a
    working driver or remove it; or, if no other working driver uses that
    IRQ, disable the xHCI98 controllers on it, or set `XhciInterruptMode`
    to `2` and restart. The USB Tree's snapshot names that device ("line
    held by another").
- **Windows 10**
  - Stor98 still matches by the NVMe class there. Windows' own `stornvme`
    outranks it, so an internal drive keeps `stornvme`. Stor98 goes only on
    a drive the installer or you put it on.
- **32-bit Windows 10 before version 2004**
  - XHCIPFW is not offered. On a board that leaves the USB4 ports'
    windows above 4 GB, 10 cannot use the devices behind those ports.

### Choose from 3 downloads

Each zip holds everything in the one before it.
- **`xhci98-3.4.3.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4, Thunderbolt networking), Stor98, the installer and the tools. It
  also has NUSB 3.3's seven Windows 98 SE storage files in
  `USBDRV\NUSB33\`, offered by the installer on 98 SE if you don't already
  have NUSB. If you have NUSB already installed, it will be kept.
- **`xhci98-3.4.3.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet, serial, printing etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) 0.1.9 in `USBDRV\FREE\`.
  **These are experimental:** they may not work with every device and can
  make Windows unstable. Please report their problems to that project.
  Its release notes are at
  https://github.com/yeokm1/usb-wdm-free/releases.
- **`xhci98-3.4.3.0-nonfree.zip`:** also adds a pack of other
  manufacturers' USB Ethernet, HID, serial and audio drivers from
  usb-wdm-nonfree (https://github.com/yeokm1/usb-wdm-nonfree) 1.3.0 in
  `USBDRV\NONFREE\`.

All free/non-free drivers are optional and installed only if you select
them.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.4.2.0 - 2026-11-08

### New

- **Stor98: NVMe drives formatted with 4 KiB sectors work on every
  system.** Windows 98 SE to 7 see them with 512-byte sectors, Stor98
  translating; Windows 8, 8.1 and 10 see the drive's own 4096-byte
  sectors, as Windows' own NVMe driver shows them. Drives with 1 KiB or
  2 KiB sectors have 512-byte sectors everywhere. Up to 3.4.1.0 such a
  drive did not work on 98 SE to 7. A drive partitioned in one view
  cannot be read in the other ("Known limitations").
- **Stor98: VERIFY reads the drive.** A scan that uses VERIFY (a full
  format or `chkdsk /r` on 2000 to 10, MS-DOS FORMAT) now finds an
  unreadable block, through the drive's own Verify command where it
  has one. Before, every VERIFY in range succeeded without reading.
- **Audio and video streams: a larger event ring, and more requests in
  flight.** The controller's event ring holds 1,024 entries where it
  held 256, and an audio or video stream can have eight requests in
  flight where it had four, so a High Speed audio device has more room
  before its sound runs dry. `XhciEventRingTrbs` `256` gives the
  earlier ring; where the memory for the larger one is not there, the
  controller uses the smaller one.
- **Two values for a UAS drive that misbehaves** (98 SE to 7, set per
  drive, `0` by default): `XhciUasNoAtaPassThrough` `1` has the UAS
  driver answer a program's ATA and SMART requests itself, as not
  supported, instead of sending them to the drive; `XhciUasFastReset`
  `1` resets a drive whose commands keep timing out at the second
  timeout, instead of asking it again and again to drop the command
  (`docs/using/tuning.md`).
- **The USB Tree's snapshot times each audio and video stream:** its
  longest gaps and waits, when it ran dry, and the time of each
  transfer error on it.
- **XHCIQUAL `--edd`:** from MS-DOS ("Command prompt only"), lists how
  the BIOS names each disk (its EDD 3.0 device path), reading only and
  touching no controller. It is for the reports asked for in
  `docs/using/edd-reports.md`, about a USB disk that holds Windows 98
  SE and ME at startup ("Known limitations"). The snapshot also notes
  the BIOS handoff's USB legacy-support registers.

### Changed

- **The installer**
  - Windows XP x64 and Server 2003 x64: Setup copies `usbui.dll` from
    Windows' own files when it is missing, so the controller's
    Advanced tab and the root hub's Power tab appear; answer Windows'
    "Windows Logo testing" box with Continue Anyway.
  - Windows 98 SE: the uninstall also removes Windows Setup's records
    of the INFs it placed (`USBSTOR.INF`, `USBNTMAP.INF`) and the disk
    entries of the USB disks it removes.
  - The Free USB drivers page: its notice ends at "... can make Windows
    unstable.", and the line below it gives usb-wdm-free's address, as
    the Non-free USB drivers page does.
  - The Progress page's wait line says only what Setup waits for.
  - The Finish page's choice "Shut down now, then start Windows again"
    reads "Shut down now".
  - `/uninstall /list` changes nothing and says what the uninstall
    would keep. Cancel while Setup first looks for devices closes it
    once that look has finished.
- **The USB Tree**
  - Save Firmware Tables (`/acpi`): on XP x64, where Windows gives no
    DSDT, the DSDT (and the FACP, RSDT and FACS Windows did not give)
    is saved from Windows' own copy in the registry. `MSDM` and `SLIC`,
    which hold the computer's Windows licence data, are listed in
    `TABLES.TXT` by their header alone and never saved, on every
    system.
  - A Thunderbolt 1 or 2 router behind a host xHCI98's connection
    manager runs is named "Thunderbolt 1" or "Thunderbolt 2", also by
    `INSTALL /devices`.
  - The report says when the ASMedia ASM4242 USB4 handover was not sent
    because the firmware is not a known build, though it has the same
    handover code (`XhciCmAsm4242Structural` `0`).
  - On a processor whose time-stamp counter is not invariant, the
    snapshot's times are marked approximate.
  - `XHCITR.EXE` on 64-bit Windows starts `XHCITR64.EXE`; with no
    `XHCITR64.EXE` beside it, it says what it cannot do there and runs.
  - The snapshot lists each UAS drive's last failed commands.
  - A UAS drive at Code 10 on a controller that cannot stream, with
    `XhciBulkOnlyOwnId` at `0`, has a note in its details that says why
    and what to do.
  - The snapshot names what xHCI98 asked Windows 2000 for on the listed
    computers of "Fixed" below: the interrupt removed, or no resources
    at all where it could not be removed.
  - **A Settings window** (View > Settings..., a controller's
    right-click, or the icon's menu) shows and changes the drivers'
    registry settings, with the meanings, choices and defaults Setup
    shows, on a tab each for the USB driver, the Thunderbolt and USB4
    driver and Stor98 where they are installed. "Applies to" sets a
    value for every controller or for one; after Apply it says what
    each change needs and offers to restart Windows. `XHCITR /set
    Name=Value` does the same from a command line, and `XHCITR /set
    /show` lists the values. Setup's USB settings page points to it.
  - **Quick save** on the icon's menu saves the report, the snapshot and
    the settings with no questions, in a new folder under the install
    folder's `Reports\` (in your documents where that cannot be
    written), and says where. A hotkey, off by default, also starts it
    (`Ctrl+Alt+F11` when turned on).
  - The icon's right-click menu is Open, Settings..., About, Quick save
    and Exit. The choices it held before, Start at login among them,
    remain in the View menu and are also on the Settings window's
    "xHCI98 USB Tree" tab.
- **XHCIQUAL:** a controller with no USB 2 ports is no longer
  disqualified; the report gives it as a caveat (its tests C6 and C8
  cannot exercise such a controller).
- **Thunderbolt and USB4:** with `XhciCmPreOsCleanup` `2`, a second
  start of the Thunderbolt 3 / USB4 driver within two minutes in the
  same boot keeps the tunnel its first start cleaned and rebuilt, where
  that tunnel is wholly the driver's own, instead of cleaning it again.
  A chained device, or anything it cannot verify, is still cleaned.
- Improved Thunderbolt networking compatibility.

### Fixed

- **Windows 2000, XP and XP x64: a USB 3 controller the BIOS gives these
  systems no interrupt for** (some HP laptops of 2012 with the Intel 7
  Series USB 3 controller). XP and XP x64 stopped it at Code 12 ("cannot
  find enough free resources"), and Windows 2000 with an ACPI computer
  type stopped at start with STOP 0x000000A5 once xHCI98 was installed.
  xHCI98 now offers Windows the line the chipset routes the controller
  to, on the listed computers by default (`XhciIntelNoRouteIrq`), and
  where Windows can give no line the controller starts and is polled.
  On Windows 2000 on the listed computers it asks for no interrupt and
  polls the controller. On these computers set `XhciIntelPortSwitch` to
  `1` for the USB 3 ports to reach the USB 3 controller under XP and XP
  x64 (`docs/using/tuning.md`).
- **Windows ME: unplugging a USB audio device while Windows' own USB
  audio driver played it** could stop Windows. It no longer does. On 98
  SE, stop the playback first ("Known limitations").
- **A drive that offers UAS and Bulk-Only stopped at Code 10 on a
  controller that cannot stream** (SuperSpeed streams off, such as the
  ASMedia ASM1042), when it was installed with the UAS driver on one
  that can. Where such a drive is used as Bulk-Only (there, or with
  `XhciForceBulkOnly` set) it now has a Device Manager entry of its own
  for that, apart from its UAS entry: Windows installs its own Bulk-Only
  driver on it the first time, and the drive keeps UAS where it is used
  as UAS. A drive installed as Bulk-Only by an earlier release is found
  once more as a new entry, and may get another drive letter.
  `XhciBulkOnlyOwnId` `0` keeps one entry everywhere, as before.
- Windows 98 SE: installing Thunderbolt networking no longer asks for the
  xhci98 disk.
- **A UAS drive could lose its volume and freeze Windows when a program
  read its health (SMART)** ("Wrong Volume", or 0 bytes free until it
  was unplugged): the drive refused the request, and the controller
  restarted the drive's endpoint while the driver stopped it, which left
  it waiting for good. xHCI98 now stops an endpoint again until it stays
  stopped, and where it does not, reconnects that device alone.
- **Stor98, Windows 98 SE and ME:** an NVMe drive the BIOS has set up
  is left to the BIOS also when the BIOS has turned the drive's memory
  access off. Stor98 used to start and reset such a drive.
- **Stor98:** READ CAPACITY(16) was answered only into a buffer of 32
  bytes or more, so Windows 7 saw a drive larger than 2 TiB with a size
  of 0.
- **Stor98, Windows 2000 to 10:** when Windows starts Stor98 again on
  a drive (a resume, or the controller disabled and enabled), Stor98
  no longer clears its command queues before it has seen the drive
  stop.
- **Windows 98 SE and ME:** a device unplugged just before Windows
  starts its driver is answered until Windows removes it, instead of
  having its start refused. On ME such a refused start could stop
  Windows when the device's removal followed.
- **A port whose device reports a cold attach and never trains** is
  reset three times and then left, instead of being reset again and
  again.
- **An audio or video request whose last packet was split** could wait
  for ever for an event the controller never sends.
- **Smaller fixes throughout**, from a review of the whole code on
  2026-11-06: the drivers' waits at stop and removal on 98 SE and ME,
  the controller's stop and start, the Thunderbolt 3 / USB4 driver's
  handover waits and resume, the Thunderbolt networking driver's start
  and halt, the UAS and Stor98 drivers' answers (MODE SENSE, LOG SENSE,
  IDENTIFY), the installer and the tools.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system: point it at `98-me\`, `nt-x86\` or
  `nt-x64\`.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\`; the ones you tick are also added to Windows' driver
  search path. When Windows asks for a driver for another such device,
  point it at that device's folder there. If the installer reports a
  controller "not reached in this run", restart Windows and run it
  again.

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
  - A Thunderbolt or USB4 dock plugged in after Windows starts needs the
    BIOS to reserve bus numbers for those ports, as boards commonly do;
    without that the dock's switch stays at Code 31 on 7, and nothing
    appears behind the port on XP or 98 SE.
  - Thunderbolt networking: one link at a time; a computer behind a dock
    or another router is not reached.
  - A drive in a daisy chain, once stopped, keeps its connection until it
    is unplugged: unplug it after the stop.
  - **Stor98, 4 KiB-sector drives:** a drive partitioned with 512-byte
    sectors (on 98 SE to 7) and one partitioned with 4096-byte sectors
    (on 8 to 10) are not interchangeable: the other view sees its
    partitions and files as damaged. Back up first, or reformat the
    drive itself to 512-byte sectors with its maker's tool.
- **Windows 2000 to 8.1**
  - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
    Device": unplug it, restart Windows (reset the computer if the
    shutdown hangs), run `cmd /c "set devmgr_show_nonpresent_devices=1&&
    devmgmt.msc"`, choose View, Show hidden devices, update the greyed
    "USB Audio Device" to `uac2`, then plug the device in.
- **Windows 2000, XP and XP x64**
  - **A USB 3 controller the BIOS gives these systems no interrupt for**
    (the HP laptops of "Fixed") is polled where Windows can give it no
    line, which uses a little more processor time; on Windows 2000 on
    the listed computers it is always polled. On a computer whose BIOS
    turns that controller on before Windows starts, Windows 2000 with an
    ACPI computer type still stops at start (STOP 0x000000A5): install
    Windows 2000 there as "Standard PC" (F7 in text-mode Setup). On the
    listed computers `XhciIntelNoRouteIrq` `0` brings that stop back
    under an ACPI computer type. If `0` was set after the last start
    that reached the desktop, "Last Known Good Configuration" at F8
    undoes it; otherwise only "Standard PC" starts Windows 2000 there
    (`docs/using/tuning.md`).
  - **Docks:** the first time a dock or an enclosure with a switch is
    plugged in, its devices may stop and start again once, a few seconds
    after it is plugged in (after its Found New Hardware windows are
    answered), while xHCI98 sets up its PCI Express ports. If a drive
    behind it is already in use, unplug it safely and plug it in again
    instead. A device added later behind a port that has no I/O range (a
    network adapter behind a chained dock) is placed after the dock is
    unplugged and plugged in again.
  - **NVMe enclosures:** the first time one is plugged in, two Found New
    Hardware wizards appear. The first finds no driver (finish it either
    way); the second offers "Stor98 Storage Controller for NVMe SSDs":
    follow it to the end.
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
- **Windows 2000**
  - With the Standard PC HAL, a drive in a daisy chain plugged in after
    startup gets no interrupt (Code 10); one chained at power-on starts.
- **Windows XP**
  - Updating asks one more "Windows Logo testing" question for each UAS
    disk that is not plugged in; `/yes` cannot answer it.
- **Windows Vista and 7**
  - Stop a dock before unplugging it, as on 2000 and XP, when a keyboard,
    mouse or similar device is on its own USB controller and the computer
    has a Thunderbolt 3 controller.
  - 32-bit: with safe removal off, a dock's own USB controller shows Code 1
    after its first plug until Windows is restarted once.
- **Windows 98 SE and ME**
  - **A USB disk left plugged in when the computer starts** can hold
    Windows at startup for minutes, or stop it. Plug USB disks in after
    Windows has started. To help find a fix, you can send an EDD report
    (`docs/using/edd-reports.md`).
  - **A drive the BIOS has set up** at power-on (one it can start from)
    stays the BIOS's: its Stor98 entry shows Code 10. 98 SE reaches the
    drive through the BIOS; ME may not reach it at all.
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
  - **Rarely, a start stops** with "A fatal exception 0E has occurred at
    019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn the
    computer off and on again; the next start works.
  - **USB audio (98 SE):** the first USB audio device plugged in after a
    boot can freeze Windows, also with Windows' own drivers. Plug the device
    in before switching on and leave it plugged in. For a webcam whose
    microphone you don't need, usb-wdm-free's `NOAUD98.INF` avoids it.
  - **Unplugging USB audio while it plays (98 SE):** stop audio playback
    before unplugging a USB audio device. Unplugged while Windows' own
    USB audio driver plays it, the device can freeze Windows. ME no
    longer does this.
  - **Free USB audio and MIDI drivers:** Windows may ask for `wdmaudio.cat`,
    `ks.cat` and other files of its own from the Windows CD. Point it at the
    CD's `WIN98` folder, or click Skip File for the `.cat` files.
  - **Keyboard media keys (98 SE):** a keyboard's volume and mute keys
    work after a restart; its other media keys need the keyboard maker's
    software. ME, 2000 and XP handle them.
  - **A shared IRQ held by another device (98 SE):**
    Windows crawls or freezes once xHCI98 is installed when a device with
    no working driver (an NVMe drive the BIOS still runs, or one whose
    driver failed) shares an xHCI98 controller's IRQ. Give that device a
    working driver or remove it; or, if no other working driver uses that
    IRQ, disable the xHCI98 controllers on it, or set `XhciInterruptMode`
    to `2` and restart. The USB Tree's snapshot names that device ("line
    held by another").
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
- **`xhci98-3.4.2.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4, Thunderbolt networking), Stor98, the installer and the tools. It
  also has NUSB 3.3's seven Windows 98 SE storage files in
  `USBDRV\NUSB33\`, offered by the installer on 98 SE if you don't already
  have NUSB. If you have NUSB already installed, it will be kept.
- **`xhci98-3.4.2.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet, serial, printing etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) 0.1.9 in `USBDRV\FREE\`.
  **These are experimental:** they may not work with every device and can
  make Windows unstable. Please report their problems to that project.
  What is new in 0.1.9 is in its release notes
  (https://github.com/yeokm1/usb-wdm-free/releases).
- **`xhci98-3.4.2.0-nonfree.zip`:** also adds a pack of other
  manufacturers' USB Ethernet, HID, serial and audio drivers from
  usb-wdm-nonfree (https://github.com/yeokm1/usb-wdm-nonfree) 1.3.0 in
  `USBDRV\NONFREE\`. New in this release: Microsoft's USB smart card
  reader driver (usbccid) for 2000 and XP, copied only; point Windows'
  hardware wizard at its folder.

All free/non-free drivers are optional and installed only if you select
them.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.4.1.0 - 2026-11-05

### New

- **Windows 98 SE and ME: the USB4 handover on ASMedia ASM4242 hosts.**
  Where a computer's firmware hands the USB4 host to the system only
  through ACPI `_OSC`, which 98 SE and ME never run, the host never
  answered and nothing behind it worked. The Thunderbolt 3 / USB4 driver now sends
  that handover itself when it starts, once a boot, if it knows the
  firmware. `XhciCmAsm4242Handover` `0` turns it off.
- **Stor98: interrupts that stop arriving.** A drive that started on its
  interrupt and then completes requests with no interrupt is polled
  until it next starts, instead of slowing to a few requests a second,
  also on an IRQ shared with another device.
  `StorIntWatchdog` (98 SE and ME) or `storintwatchdog=` (2000 to 10)
  `0` turns it off.
- **Windows 98 SE and ME: a device holding a controller's IRQ is named.**
  At each start on an IRQ xHCI98 looks for another device that holds the
  controller's shared IRQ. The USB Tree's snapshot names it by its PCI
  location and ids.
- **Windows 2000, 32-bit XP and XP x64: the USB4 handover on ASMedia
  ASM4242 hosts**, as on 98 SE and ME, once
  `XhciCmAsm4242Reads` is `1`. The USB Tree says when it is needed.

### Changed

- **The installer**
  - Windows 98 SE and ME: an ASMedia ASM4242 USB4 host is found while
    Windows has not yet listed its router. The page says so and ticks
    the Thunderbolt and USB4 support.
  - Settings... on the Components page holds the USB settings. The
    Thunderbolt and USB4 page has its own Settings... for the
    Thunderbolt / USB4 and Stor98 settings.
  - The Progress page's bar moves with the work done, also while Windows
    installs the devices.
  - The texts of every page, of Help, of the console and of the readme
    are reworded.
- **The USB Tree:** File, Save Snapshot... asks only for the folder, then
  says in a short message where the files went. It names AMD's other USB4
  hosts (Rembrandt, Phoenix, Strix, Strix Halo, Krackan, Krackan2).
- **Windows 98 SE and ME:** the Thunderbolt 3 / USB4 driver stops writing
  to the registry when Windows announces a shutdown or standby.
- **Windows 8, 8.1 and 10:** Setup no longer installs the UAS disk
  driver. A UAS disk set up from now on uses Windows' own UAS driver; a
  disk an earlier release set up with `xhciuas.sys` keeps it.
- **Setup's log:** every line starts with the time.
- **Thunderbolt and USB4 eject:** the confirmation names the drive and
  its letter, and the finish message is "It is now safe to disconnect
  the device."
- **The log:** a command the controller never answers is named in
  the snapshot.
- **The documentation:** the project's own Markdown outside
  `src/stor98/` is licensed under CC BY 4.0 (`LICENSE-docs.md`).

### Fixed

- **Windows 98 SE and ME: Stor98 took every NVMe drive in the computer**
  at the start, Windows' own NVMe system disk included, and the start
  could stop at "Initing stor98.mpd". It now runs only the drive it was
  loaded for, and leaves a drive the BIOS has set up to the BIOS.
- **Windows 98 SE and ME:** Stor98's Advanced settings gave
  `StorMaxTransferKB`'s top as 512 KiB. It is 2048 KiB there.
- **UAS disks (2000 to 7):** one error on a disk's stream that no
  transfer was waiting for disconnected the disk while in use, and
  with some controllers every device on the controller with it.
  The disk's endpoint is now recovered instead; if that fails, only
  the disk is reconnected, never the whole controller.
- **Updating (2000 to 7):** a UAS disk not plugged in during the
  update kept the earlier UAS driver. It is updated too now.
- **Updating (98 SE and ME)** with a UAS disk plugged in asked for
  the driver disk.
- **Windows 98 SE Summary:** the storage drivers' line was followed
  by "Not installed" after they were installed.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system: point it at `98-me\`, `nt-x86\` or
  `nt-x64\`.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\`; the ones you tick are also added to Windows' driver
  search path. When Windows asks for a driver for another such device,
  point it at that device's folder there. If the installer reports a
  controller "not reached in this run", restart Windows and run it
  again.

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
  - A Thunderbolt or USB4 dock plugged in after Windows starts needs the
    BIOS to reserve bus numbers for those ports, as boards commonly do;
    without that the dock's switch stays at Code 31 on 7, and nothing
    appears behind the port on XP or 98 SE.
  - Thunderbolt networking: one link at a time; a computer behind a dock
    or another router is not reached.
  - A drive in a daisy chain, once stopped, keeps its connection until it
    is unplugged: unplug it after the stop.
- **Windows 2000 to 8.1**
  - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
    Device": unplug it, restart Windows (reset the computer if the
    shutdown hangs), run `cmd /c "set devmgr_show_nonpresent_devices=1&&
    devmgmt.msc"`, choose View, Show hidden devices, update the greyed
    "USB Audio Device" to `uac2`, then plug the device in.
- **Windows 2000, XP and XP x64**
  - **Docks:** the first time a dock or an enclosure with a switch is
    plugged in, its devices may stop and start again once, a few seconds
    after it is plugged in (after its Found New Hardware windows are
    answered), while xHCI98 sets up its PCI Express ports. If a drive
    behind it is already in use, unplug it safely and plug it in again
    instead. A device added later behind a port that has no I/O range (a
    network adapter behind a chained dock) is placed after the dock is
    unplugged and plugged in again.
  - **NVMe enclosures:** the first time one is plugged in, two Found New
    Hardware wizards appear. The first finds no driver (finish it either
    way); the second offers "Stor98 Storage Controller for NVMe SSDs":
    follow it to the end.
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
- **Windows 2000**
  - With the Standard PC HAL, a drive in a daisy chain plugged in after
    startup gets no interrupt (Code 10); one chained at power-on starts.
- **Windows XP**
  - Updating asks one more "Windows Logo testing" question for each UAS
    disk that is not plugged in; `/yes` cannot answer it.
- **Windows Vista and 7**
  - Stop a dock before unplugging it, as on 2000 and XP, when a keyboard,
    mouse or similar device is on its own USB controller and the computer
    has a Thunderbolt 3 controller.
  - 32-bit: with safe removal off, a dock's own USB controller shows Code 1
    after its first plug until Windows is restarted once.
- **Windows 98 SE and ME**
  - **A drive the BIOS has set up** at power-on (one it can start from)
    stays the BIOS's: its Stor98 entry shows Code 10, and Windows reaches
    the drive through the BIOS.
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
  - **Rarely, a start stops** with "A fatal exception 0E has occurred at
    019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn the
    computer off and on again; the next start works.
  - **USB audio (98 SE):** the first USB audio device plugged in after a
    boot can freeze Windows, also with Windows' own drivers. Plug the device
    in before switching on and leave it plugged in. For a webcam whose
    microphone you don't need, usb-wdm-free's `NOAUD98.INF` avoids it.
  - **Free USB audio and MIDI drivers:** Windows may ask for `wdmaudio.cat`,
    `ks.cat` and other files of its own from the Windows CD. Point it at the
    CD's `WIN98` folder, or click Skip File for the `.cat` files.
  - **A shared IRQ held by another device (98 SE):**
    Windows crawls or freezes once xHCI98 is installed when a device with
    no working driver (an NVMe drive the BIOS still runs, or one whose
    driver failed) shares an xHCI98 controller's IRQ. Give that device a
    working driver or remove it; or, if no other working driver uses that
    IRQ, disable the xHCI98 controllers on it, or set `XhciInterruptMode`
    to `2` and restart. The USB Tree's snapshot names that device ("line
    held by another").
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
- **`xhci98-3.4.1.0.zip`:** the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
  USB4, Thunderbolt networking), Stor98, the installer and the tools. It
  also has NUSB 3.3's seven Windows 98 SE storage files in
  `USBDRV\NUSB33\`, offered by the installer on 98 SE if you don't already
  have NUSB. If you have NUSB already installed, it will be kept.
- **`xhci98-3.4.1.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet, serial, printing etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) 0.1.8 in `USBDRV\FREE\`.
  **These are experimental:** they may not work with every device and can
  make Windows unstable. Please report their problems to that project. New
  in this release:
  - `uac2`: far fewer pops under heavy CPU load (more sound queued, set
    by `Uac2QueueMs`, and a real-time worker, `Uac2Realtime`; a delay in
    xHCI98 itself of more than 16 ms still interrupts the sound); the volume
    tops out at 0 dB, a likely cause of distortion on some devices
    (`Uac2VolAboveZero` `1` offers the whole range again); each device is
    named by its own product name, such as "Sound Blaster X1".
  - `uac2`, `cdcnet` and `usbflop` are also offered on XP x64, and
    `ax88179` on 2000. `uac2` is also copied on 2000, Vista x64, 8 and
    8.1: point Update Driver at its folder.
- **`xhci98-3.4.1.0-nonfree.zip`:** also adds a pack of other
  manufacturers' USB Ethernet, HID, serial and audio drivers from
  usb-wdm-nonfree (https://github.com/yeokm1/usb-wdm-nonfree) 1.2.0 in
  `USBDRV\NONFREE\`. New in this release: TASCAM's US-122 audio and MIDI
  interface driver for 98 SE and ME.

All free/non-free drivers are optional and installed only if you select
them.

### Disclaimer

This is a fairly new driver. It may have bugs or edge cases that are not yet
accounted for given the large xHCI/USB 3/4/TB specifications and the many
host controller implementations and devices. **USB4/TB/NVMe features and the
usb-wdm-free drivers are even more experimental, so use them at your own
risk!**

Appreciate all issue reports!

## 3.4.0.0 - 2026-11-01

### New

- **Thunderbolt networking:** a network connection between two computers
  joined by one Thunderbolt or USB4 cable, in the frame format Linux's
  Thunderbolt networking uses, on every system from 98 SE to 10, 32-bit
  and 64-bit. With no DHCP server on the link both computers take a
  169.254.x.x address; set fixed addresses for a lasting setup.
  Off unless you tick "Thunderbolt networking" on the installer's
  Thunderbolt and USB4 page; the driver is in each folder's `cm\tbnet\`.
- **Windows 2000, XP and XP x64: Thunderbolt 3 and USB4 docks plugged in
  while Windows runs.** A dock, or an enclosure with a PCI Express switch
  of its own, now gets every port in it placed, so the devices behind
  them (an NVMe drive, a USB controller, a network adapter) can start.
  Before, only one of its ports did. `XhciCmBridgeReq` under the
  installer's "Advanced..." turns it off.
- **Daisy chains** behind a USB4 host whose tunnels xHCI98 builds: a
  device plugged into a USB4 device's downstream port is connected
  through it, and a drive in it appears beside the parent's.
- **USB4 version 2 routers** (Barlow Ridge, say) run as version 2, Gen 4
  links at 80 Gbit/s (120/40 asymmetric) included. An error on the Gen 4
  link to the device plugged into the computer is recovered in place,
  with the devices behind it kept.
- **Devices kept across standby and hibernation:** where the USB
  controller has to be started afresh at the resume (every VIA controller,
  and any controller that cannot save its state), a device on one of the
  computer's own USB ports is kept as it was, with its drive letter and
  its open files, instead of being removed and found again. A UAS drive is
  kept while it is in use too. `XhciResumeKeep` `0` turns it off.
- **UAS drives:** SMART and drive-information tools can read a drive
  whose USB bridge passes ATA commands through (SAT), on 2000 to 10 (ASPI
  tools on 98 SE and ME). On 2000, XP, Vista and 7 a UAS drive is listed
  in Safely Remove Hardware and can be stopped on its own.
  A UAS drive behind a SuperSpeed hub is kept across the hub port's warm
  reset.
- **The installer:** a Thunderbolt and USB4 page after the Components
  page; the Advanced lists offer every setting a user may change; Help
  (and F1) explains the page it is on.
- **USB devices with more than one configuration** are set up in their
  first configuration and split into their parts where a device with
  one configuration would be, so a USB Audio 3.0 device's USB Audio 2.0
  part gets the free `uac2`, where before the device had no driver.
- **XHCIPFW on 32-bit Windows 8 and 8.1**, as a filter of the PCI root bus
  (the service `xhcipff`); not on Windows 10. The installer readme's
  recovery steps say how to reach the recovery environment and remove the
  filter there.
- **The USB Tree's title** shows its version, as `xHCI98 USB Tree
  3.4.0.0`.
- **DISM can add `nt-x64\` to a Windows PE or Setup `boot.wim`** as well
  as `install.wim`. On XP x64 with no other USB driver installed, the
  controller has no Advanced tab and the root hub no Power tab.

### Changed

- **One folder per Windows family:** `98-me\` (98 SE, ME), `nt-x86\`
  (32-bit 2000 to 10) and `nt-x64\` (64-bit), each with only what its
  Windows installs: the USB drivers at its root (the F6 floppy on NT),
  `cm\` and `stor98\`. `INTEGRATE.TXT` says which folder to give nLite,
  NTLite or DISM. The 98 SE and ME Stor98 INF is now `stor98.inf`.
- **One build for everyone:** `debug-x86\`, `debug-x64\` and `/debug` are
  gone. The build keeps a command-state checker that only counts
  (`XhciCommandCheck` `0` turns it off).
- **Every setting the drivers read** is written by the install at its
  default, only where it is missing. On 98 SE and ME the uninstall removes
  Stor98's settings, so a later install starts clean.
- **USB Audio 2.0 and 3.0 devices (Windows 2000 to 8.1):** xHCI98 no
  longer offers such a device to Windows' own USB Audio 1.0 driver ("USB
  Audio Device"), which cannot run it and keeps Device Manager, the Update
  Driver wizard and shutdown waiting until a reset. Windows now asks for a
  driver (the free download's `uac2`), unless one of Windows' own lines
  names that device by its ids.
- **A device that comes back** after a link drop, a recovery or a replug
  is no longer taken for a new one when its serial number cannot be read
  at that moment, so a network adapter keeps its fixed address; if the
  driver's retries fail, the device stays out until plugged in again.
- **Windows Vista and 7, 32-bit:** a dock's own USB controller starts at
  its first plug without a restart.
- **Windows 98 SE and ME: more of the free and non-free drivers can be
  ticked** for Windows' hardware wizard to find by itself, as long as the
  wizard's search path stays short enough (280 characters on 98 SE, 259
  on ME). The Free USB drivers page also lists the drivers that are only
  copied. The installer asks you to close the hardware wizard only when
  one is open (on English Windows).
- **Windows 8, 8.1 and 10:** the drivers keep their own data and their
  64-bit import tables out of executable memory, as memory integrity
  requires.

### Fixed

- **Windows 2000 and XP:** a dock unplugged with a keyboard, mouse or
  tablet on its own USB controller is removed, on a USB4 host whose
  tunnels xHCI98 builds.
- **Windows 98 SE:** an install on a computer with two USB 3 controllers no
  longer asks for the "xhci98 xHCI Host Controller Disk" at the second
  one; a power cut during the install's file copy no longer loses the
  record of NUSB's storage files.
- **Windows 8.1:** Stor98 starts on an NVMe drive that Windows' own
  `stornvme` ran before.
- **Thunderbolt 3 / USB4:** under Driver Verifier, the connection manager
  could stop Windows at the start (Windows 7) or when it was unloaded by an
  uninstall (Windows 8 and 8.1).
- **Windows 10 x64:** waking from Sleep could stop Windows with
  DRIVER_POWER_STATE_FAILURE (0x9F) when a USB mouse or keyboard was
  attached.
- **Smaller fixes** from a review of the whole code: the USB driver, the
  UAS driver, Stor98, the Thunderbolt 3 / USB4 driver and the installer.

### Installing/Updating

- **Use `SETUP.EXE`.** Registry values are kept. Device Manager's Update
  Driver also works on every system: point it at `98-me\`, `nt-x86\` or
  `nt-x64\`.
- **Vista x64 to 10 x64:** the x64 drivers are test-signed, so test-signing
  mode must be on, **and must stay on while the Thunderbolt 3 / USB4 driver
  is installed**. On 8, 8.1 and 10 that also means Secure Boot must be off.
- **Windows 98 SE and ME:** the free and non-free drivers are copied to
  `C:\XHCI98\USBDRV\`; the ones you tick are also added to Windows' driver
  search path. When Windows asks for a driver for another such device,
  point it at that device's folder there. If the installer reports a
  controller "not reached in this run", restart Windows and run it
  again.

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
  - A Thunderbolt or USB4 dock plugged in after Windows starts needs the
    BIOS to reserve bus numbers for those ports, as boards commonly do;
    without that the dock's switch stays at Code 31 on 7, and nothing
    appears behind the port on XP or 98 SE.
  - Thunderbolt networking: one link at a time; a computer behind a dock
    or another router is not reached.
  - A drive in a daisy chain, once stopped, keeps its connection until it
    is unplugged: unplug it after the stop.
- **Windows 2000 to 8.1**
  - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
    Device": unplug it, restart Windows (reset the computer if the
    shutdown hangs), run `cmd /c "set devmgr_show_nonpresent_devices=1&&
    devmgmt.msc"`, choose View, Show hidden devices, update the greyed
    "USB Audio Device" to `uac2`, then plug the device in.
- **Windows 2000, XP and XP x64**
  - **Docks:** the first time a dock or an enclosure with a switch is
    plugged in, its devices may stop and start again once, a few seconds
    after it is plugged in (after its Found New Hardware windows are
    answered), while xHCI98 sets up its PCI Express ports. If a drive
    behind it is already in use, unplug it safely and plug it in again
    instead. A device added later behind a port that has no I/O range (a
    network adapter behind a chained dock) is placed after the dock is
    unplugged and plugged in again.
  - **NVMe enclosures:** the first time one is plugged in, two Found New
    Hardware wizards appear. The first finds no driver (finish it either
    way); the second offers "Stor98 Storage Controller for NVMe SSDs":
    follow it to the end.
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
- **Windows 2000**
  - With the Standard PC HAL, a drive in a daisy chain plugged in after
    startup gets no interrupt (Code 10); one chained at power-on starts.
- **Windows Vista and 7**
  - Stop a dock before unplugging it, as on 2000 and XP, when a keyboard,
    mouse or similar device is on its own USB controller and the computer
    has a Thunderbolt 3 controller.
  - 32-bit: with safe removal off, a dock's own USB controller shows Code 1
    after its first plug until Windows is restarted once.
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
  - **Rarely, a start stops** with "A fatal exception 0E has occurred at
    019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn the
    computer off and on again; the next start works.
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
  USB4), Stor98, the installer and the tools. It
  also has NUSB 3.3's seven Windows 98 SE storage files in
  `USBDRV\NUSB33\`, offered by the installer on 98 SE if you don't already
  have NUSB. If you have NUSB already installed, it will be kept.
- **`xhci98-3.4.0.0-free.zip`:** adds the free USB class drivers (audio,
  MIDI, webcam, Ethernet, printing etc.) from usb-wdm-free
  (https://github.com/yeokm1/usb-wdm-free) 0.1.7 in `USBDRV\FREE\`.
  **These are experimental:** they may not work with every device and can
  make Windows unstable. Please report their problems to that project. New
  in this release:
  - `usbprn`: USB printers and USB-to-parallel printer cables on 98 SE and
    ME (a printer port, `USB001` and on; add the printer with Add
    Printer).
  - `uac2`: each output and input its own sound device; no old sound
    replayed after a seek; a restart asked for when it is installed on
    2000; a co-installer, installed by hand, that keeps Windows' USB Audio
    1.0 driver off a UAC 2.0 device on 2000 to 7.
  - `usbcom`: the FTDI FT-X, FT232H and FT4232H, Prolific PL2303 and
    Silicon Labs CP2105 and CP2108 serial bridges on 98 SE and ME.
  - `rtl815x`: may be staged beside the non-free one on XP to 7, where
    their INFs list different chips.
- **`xhci98-3.4.0.0-nonfree.zip`:** also adds a pack of other
  manufacturers' USB Ethernet, HID and serial drivers from usb-wdm-nonfree
  (https://github.com/yeokm1/usb-wdm-nonfree) 1.1.0 in `USBDRV\NONFREE\`.

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
