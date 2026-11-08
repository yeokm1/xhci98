==============================================================================
                              x h c i 9 8   3.4.2.0
    USB 3.x for Windows 98 SE to 7, Thunderbolt 3 and USB4 for 98 SE to 10
==============================================================================

Released 2026-11-08.

Most PCs made from the mid 2010s onward have only USB 3.x (xHCI)
controllers, which older Windows cannot use. This package drives them on:

  - Windows 98 SE, ME, 2000 SP4, XP, Vista and 7, 32-bit
  - Windows XP, Vista and 7, x64, through separate 64-bit drivers, which
    also serve 8, 8.1 and 10 x64 (below). From Vista x64 on they carry a
    test signature only, so Windows must be in test-signing mode, or driver
    signature enforcement disabled (section 2).
  - Windows 8, 8.1 and 10, for the Thunderbolt 3 / USB4 connection
    manager: Windows' own USB 3 driver stays there unless you tick this
    package's USB drivers. Secure Boot must be off (section 2).

xhci98.sys is a whole USB host controller driver: it runs the controller,
the root hub, every hub and the splitting of composite devices itself, and
Windows' own drivers for keyboards, mice, storage, audio and network
adapters sit on top of it unchanged. xhciuas.sys runs UAS (USB Attached
SCSI) storage, xhcicm.sys is the Thunderbolt 3 and USB4 connection manager,
and the bundled Stor98 drives an NVMe drive behind a Thunderbolt or USB4
port.

This file is the short guide. The full release notes - what each system
needs, every registry setting, every file and every known limitation -
are online:

      https://github.com/yeokm1/xhci98/blob/main/docs/using/release-notes.md


THIS DOWNLOAD
------------------------------------------------------------------------------

This is xhci98-3.4.2.0.zip, the base download of xhci98 3.4.2.0: the drivers,
their tools and installer, and NUSB 3.3's Windows 98 SE storage files
(USBDRV\NUSB33\).

This release has these downloads, each holding the one before it:

  xhci98-3.4.2.0.zip         the base download
  xhci98-3.4.2.0-free.zip    adds the free USB class drivers (USBDRV\FREE\)
  xhci98-3.4.2.0-nonfree.zip adds the non-free driver pack (USBDRV\NONFREE\)

The installer copies those drivers only when you tick them, and never
installs one by itself. The up-to-date drivers, free and non-free, are at:

  https://github.com/yeokm1/usb-wdm-free
  https://github.com/yeokm1/usb-wdm-nonfree


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. What is new in 3.4.2.0
  2. Install
  3. The files Windows supplies, and USB storage on Windows 98 SE
  4. Reporting a problem
  5. What is in this directory
  6. Earlier releases


==============================================================================
 1. WHAT IS NEW IN 3.4.2.0
==============================================================================

  New

    * Stor98: NVMe drives formatted with 4 KiB sectors work on every system.
      Windows 98 SE to 7 see them with 512-byte sectors, Stor98 translating;
      Windows 8, 8.1 and 10 see the drive's own 4096-byte sectors, as Windows'
      own NVMe driver shows them. Drives with 1 KiB or 2 KiB sectors have
      512-byte sectors everywhere. Up to 3.4.1.0 such a drive did not work on
      98 SE to 7. A drive partitioned in one view cannot be read in the other
      ("Known limitations").
    * Stor98: VERIFY reads the drive. A scan that uses VERIFY (a full format
      or chkdsk /r on 2000 to 10, MS-DOS FORMAT) now finds an unreadable
      block, through the drive's own Verify command where it has one. Before,
      every VERIFY in range succeeded without reading.
    * Audio and video streams: a larger event ring, and more requests in
      flight. The controller's event ring holds 1,024 entries where it held
      256, and an audio or video stream can have eight requests in flight
      where it had four, so a High Speed audio device has more room before its
      sound runs dry. XhciEventRingTrbs 256 gives the earlier ring; where the
      memory for the larger one is not there, the controller uses the smaller
      one.
    * Two values for a UAS drive that misbehaves (98 SE to 7, set per drive, 0
      by default): XhciUasNoAtaPassThrough 1 has the UAS driver answer a
      program's ATA and SMART requests itself, as not supported, instead of
      sending them to the drive; XhciUasFastReset 1 resets a drive whose
      commands keep timing out at the second timeout, instead of asking it
      again and again to drop the command (docs/using/tuning.md).
    * The USB Tree's snapshot times each audio and video stream: its longest
      gaps and waits, when it ran dry, and the time of each transfer error on
      it.
    * XHCIQUAL --edd: from MS-DOS ("Command prompt only"), lists how the BIOS
      names each disk (its EDD 3.0 device path), reading only and touching no
      controller. It is for the reports asked for in
      docs/using/edd-reports.md, about a USB disk that holds Windows 98 SE and
      ME at startup ("Known limitations"). The snapshot also notes the BIOS
      handoff's USB legacy-support registers.

  Changed

    * The installer
        - Windows XP x64 and Server 2003 x64: Setup copies usbui.dll from
          Windows' own files when it is missing, so the controller's Advanced
          tab and the root hub's Power tab appear; answer Windows' "Windows
          Logo testing" box with Continue Anyway.
        - Windows 98 SE: the uninstall also removes Windows Setup's records of
          the INFs it placed (USBSTOR.INF, USBNTMAP.INF) and the disk entries
          of the USB disks it removes.
        - The Free USB drivers page: its notice ends at "... can make Windows
          unstable.", and the line below it gives usb-wdm-free's address, as
          the Non-free USB drivers page does.
        - The Progress page's wait line says only what Setup waits for.
        - The Finish page's choice "Shut down now, then start Windows again"
          reads "Shut down now".
        - /uninstall /list changes nothing and says what the uninstall would
          keep. Cancel while Setup first looks for devices closes it once that
          look has finished.
    * The USB Tree
        - Save Firmware Tables (/acpi): on XP x64, where Windows gives no
          DSDT, the DSDT (and the FACP, RSDT and FACS Windows did not give) is
          saved from Windows' own copy in the registry. MSDM and SLIC, which
          hold the computer's Windows licence data, are listed in TABLES.TXT
          by their header alone and never saved, on every system.
        - A Thunderbolt 1 or 2 router behind a host xHCI98's connection
          manager runs is named "Thunderbolt 1" or "Thunderbolt 2", also by
          INSTALL /devices.
        - The report says when the ASMedia ASM4242 USB4 handover was not sent
          because the firmware is not a known build, though it has the same
          handover code (XhciCmAsm4242Structural 0).
        - On a processor whose time-stamp counter is not invariant, the
          snapshot's times are marked approximate.
        - XHCITR.EXE on 64-bit Windows starts XHCITR64.EXE; with no
          XHCITR64.EXE beside it, it says what it cannot do there and runs.
        - The snapshot lists each UAS drive's last failed commands.
        - A UAS drive at Code 10 on a controller that cannot stream, with
          XhciBulkOnlyOwnId at 0, has a note in its details that says why and
          what to do.
        - The snapshot names what xHCI98 asked Windows 2000 for on the listed
          computers of "Fixed" below: the interrupt removed, or no resources
          at all where it could not be removed.
        - A Settings window (View > Settings..., a controller's right-click,
          or the icon's menu) shows and changes the drivers' registry
          settings, with the meanings, choices and defaults Setup shows, on a
          tab each for the USB driver, the Thunderbolt and USB4 driver and
          Stor98 where they are installed. "Applies to" sets a value for every
          controller or for one; after Apply it says what each change needs
          and offers to restart Windows. XHCITR /set Name=Value does the same
          from a command line, and XHCITR /set /show lists the values. Setup's
          USB settings page points to it.
        - Quick save on the icon's menu saves the report, the snapshot and the
          settings with no questions, in a new folder under the install
          folder's Reports\ (in your documents where that cannot be written),
          and says where. A hotkey, off by default, also starts it
          (Ctrl+Alt+F11 when turned on).
        - The icon's right-click menu is Open, Settings..., About, Quick save
          and Exit. The choices it held before, Start at login among them,
          remain in the View menu and are also on the Settings window's
          "xHCI98 USB Tree" tab.
    * XHCIQUAL: a controller with no USB 2 ports is no longer disqualified;
      the report gives it as a caveat (its tests C6 and C8 cannot exercise
      such a controller).
    * Thunderbolt and USB4: with XhciCmPreOsCleanup 2, a second start of the
      Thunderbolt 3 / USB4 driver within two minutes in the same boot keeps
      the tunnel its first start cleaned and rebuilt, where that tunnel is
      wholly the driver's own, instead of cleaning it again. A chained device,
      or anything it cannot verify, is still cleaned.
    * Improved Thunderbolt networking compatibility.

  Fixed

    * Windows 2000, XP and XP x64: a USB 3 controller the BIOS gives these
      systems no interrupt for (some HP laptops of 2012 with the Intel 7
      Series USB 3 controller). XP and XP x64 stopped it at Code 12 ("cannot
      find enough free resources"), and Windows 2000 with an ACPI computer
      type stopped at start with STOP 0x000000A5 once xHCI98 was installed.
      xHCI98 now offers Windows the line the chipset routes the controller to,
      on the listed computers by default (XhciIntelNoRouteIrq), and where
      Windows can give no line the controller starts and is polled. On Windows
      2000 on the listed computers it asks for no interrupt and polls the
      controller. On these computers set XhciIntelPortSwitch to 1 for the USB
      3 ports to reach the USB 3 controller under XP and XP x64
      (docs/using/tuning.md).
    * Windows ME: unplugging a USB audio device while Windows' own USB audio
      driver played it could stop Windows. It no longer does. On 98 SE, stop
      the playback first ("Known limitations").
    * A drive that offers UAS and Bulk-Only stopped at Code 10 on a controller
      that cannot stream (SuperSpeed streams off, such as the ASMedia
      ASM1042), when it was installed with the UAS driver on one that can.
      Where such a drive is used as Bulk-Only (there, or with
      XhciForceBulkOnly set) it now has a Device Manager entry of its own for
      that, apart from its UAS entry: Windows installs its own Bulk-Only
      driver on it the first time, and the drive keeps UAS where it is used as
      UAS. A drive installed as Bulk-Only by an earlier release is found once
      more as a new entry, and may get another drive letter. XhciBulkOnlyOwnId
      0 keeps one entry everywhere, as before.
    * Windows 98 SE: installing Thunderbolt networking no longer asks for the
      xhci98 disk.
    * A UAS drive could lose its volume and freeze Windows when a program read
      its health (SMART) ("Wrong Volume", or 0 bytes free until it was
      unplugged): the drive refused the request, and the controller restarted
      the drive's endpoint while the driver stopped it, which left it waiting
      for good. xHCI98 now stops an endpoint again until it stays stopped, and
      where it does not, reconnects that device alone.
    * Stor98, Windows 98 SE and ME: an NVMe drive the BIOS has set up is left
      to the BIOS also when the BIOS has turned the drive's memory access off.
      Stor98 used to start and reset such a drive.
    * Stor98: READ CAPACITY(16) was answered only into a buffer of 32 bytes or
      more, so Windows 7 saw a drive larger than 2 TiB with a size of 0.
    * Stor98, Windows 2000 to 10: when Windows starts Stor98 again on a drive
      (a resume, or the controller disabled and enabled), Stor98 no longer
      clears its command queues before it has seen the drive stop.
    * Windows 98 SE and ME: a device unplugged just before Windows starts its
      driver is answered until Windows removes it, instead of having its start
      refused. On ME such a refused start could stop Windows when the device's
      removal followed.
    * A port whose device reports a cold attach and never trains is reset
      three times and then left, instead of being reset again and again.
    * An audio or video request whose last packet was split could wait for
      ever for an event the controller never sends.
    * Smaller fixes throughout, from a review of the whole code on 2026-11-06:
      the drivers' waits at stop and removal on 98 SE and ME, the controller's
      stop and start, the Thunderbolt 3 / USB4 driver's handover waits and
      resume, the Thunderbolt networking driver's start and halt, the UAS and
      Stor98 drivers' answers (MODE SENSE, LOG SENSE, IDENTIFY), the installer
      and the tools.

  Installing/Updating

    * Use SETUP.EXE. Registry values are kept. Device Manager's Update Driver
      also works on every system: point it at 98-me\, nt-x86\ or nt-x64\.
    * Vista x64 to 10 x64: the x64 drivers are test-signed, so test-signing
      mode must be on, and must stay on while the Thunderbolt 3 / USB4 driver
      is installed. On 8, 8.1 and 10 that also means Secure Boot must be off.
    * Windows 98 SE and ME: the free and non-free drivers are copied to
      C:\XHCI98\USBDRV\; the ones you tick are also added to Windows' driver
      search path. When Windows asks for a driver for another such device,
      point it at that device's folder there. If the installer reports a
      controller "not reached in this run", restart Windows and run it again.

  Known limitations

    * All systems
        - Standby and hibernation with a Thunderbolt or USB4 device attached
          are not supported: use Safely Remove and unplug the device first.
        - After a standby or hibernation that starts the USB controller
          afresh, a hub and the devices behind it, a Bulk-Only drive, a drive
          with no serial number or one that could not finish its work before
          the sleep, a card reader or other removable drive with a medium in
          it, a drive that does not report its own identity, and an audio or
          video device in use are still removed and found again.
        - An internal NVMe drive already on Stor98 (put there by hand) keeps
          it.
        - A maker's driver that takes a whole USB device with more than one
          configuration no longer binds to it under xHCI98 where xHCI98 now
          splits that device.
        - A Thunderbolt or USB4 dock plugged in after Windows starts needs the
          BIOS to reserve bus numbers for those ports, as boards commonly do;
          without that the dock's switch stays at Code 31 on 7, and nothing
          appears behind the port on XP or 98 SE.
        - Thunderbolt networking: one link at a time; a computer behind a dock
          or another router is not reached.
        - A drive in a daisy chain, once stopped, keeps its connection until
          it is unplugged: unplug it after the stop.
        - Stor98, 4 KiB-sector drives: a drive partitioned with 512-byte
          sectors (on 98 SE to 7) and one partitioned with 4096-byte sectors
          (on 8 to 10) are not interchangeable: the other view sees its
          partitions and files as damaged. Back up first, or reformat the
          drive itself to 512-byte sectors with its maker's tool.
    * Windows 2000 to 8.1
        - A USB Audio 2.0 or 3.0 device that already shows as "USB Audio
          Device": unplug it, restart Windows (reset the computer if the
          shutdown hangs), run cmd /c "set devmgr_show_nonpresent_devices=1&&
          devmgmt.msc", choose View, Show hidden devices, update the greyed
          "USB Audio Device" to uac2, then plug the device in.
    * Windows 2000, XP and XP x64
        - A USB 3 controller the BIOS gives these systems no interrupt for
          (the HP laptops of "Fixed") is polled where Windows can give it no
          line, which uses a little more processor time; on Windows 2000 on
          the listed computers it is always polled. On a computer whose BIOS
          turns that controller on before Windows starts, Windows 2000 with an
          ACPI computer type still stops at start (STOP 0x000000A5): install
          Windows 2000 there as "Standard PC" (F7 in text-mode Setup). On the
          listed computers XhciIntelNoRouteIrq 0 brings that stop back under
          an ACPI computer type. If 0 was set after the last start that
          reached the desktop, "Last Known Good Configuration" at F8 undoes
          it; otherwise only "Standard PC" starts Windows 2000 there
          (docs/using/tuning.md).
        - Docks: the first time a dock or an enclosure with a switch is
          plugged in, its devices may stop and start again once, a few seconds
          after it is plugged in (after its Found New Hardware windows are
          answered), while xHCI98 sets up its PCI Express ports. If a drive
          behind it is already in use, unplug it safely and plug it in again
          instead. A device added later behind a port that has no I/O range (a
          network adapter behind a chained dock) is placed after the dock is
          unplugged and plugged in again.
        - NVMe enclosures: the first time one is plugged in, two Found New
          Hardware wizards appear. The first finds no driver (finish it either
          way); the second offers "Stor98 Storage Controller for NVMe SSDs":
          follow it to the end.
        - Stop a dock before unplugging it (Safely Remove Hardware, Unplug or
          Eject Hardware on 2000, or the xHCI98 USB Tree) when a keyboard,
          mouse, tablet or similar device is on the dock's own USB controller,
          unless xHCI98 builds the computer's USB4 tunnels itself (a USB4 host
          whose firmware has no connection manager), where the unplug is
          normally handled. Otherwise Windows may never finish removing the
          dock: it is not found again when plugged back, and Windows does not
          shut down.
        - Updated from 3.2.0.0: an internal NVMe drive added after the update
          can start on Stor98: 3.2.0.0 left the key
          CriticalDeviceDatabase\pci#cc_010802 with Service stor98. Delete
          that key with Regedit as an administrator, then restart (the release
          notes, "Known limitations").
    * Windows 2000
        - With the Standard PC HAL, a drive in a daisy chain plugged in after
          startup gets no interrupt (Code 10); one chained at power-on starts.
    * Windows XP
        - Updating asks one more "Windows Logo testing" question for each UAS
          disk that is not plugged in; /yes cannot answer it.
    * Windows Vista and 7
        - Stop a dock before unplugging it, as on 2000 and XP, when a
          keyboard, mouse or similar device is on its own USB controller and
          the computer has a Thunderbolt 3 controller.
        - 32-bit: with safe removal off, a dock's own USB controller shows
          Code 1 after its first plug until Windows is restarted once.
    * Windows 98 SE and ME
        - A USB disk left plugged in when the computer starts can hold Windows
          at startup for minutes, or stop it. Plug USB disks in after Windows
          has started. To help find a fix, you can send an EDD report
          (docs/using/edd-reports.md).
        - A drive the BIOS has set up at power-on (one it can start from)
          stays the BIOS's: its Stor98 entry shows Code 10. 98 SE reaches the
          drive through the BIOS; ME may not reach it at all.
        - Stor98 and internal NVMe drives: these systems have no NVMe driver
          of their own, and they look for a driver before the Thunderbolt 3 /
          USB4 driver can give a drive its id. So Stor98 still matches every
          NVMe drive there, and Windows may offer it for an internal drive
          with no driver: decline it. The installer puts Stor98 on tunnelled
          drives only.
        - Docks: stop a dock in the xHCI98 USB Tree before unplugging it when
          a keyboard, mouse, tablet or similar device is on the dock's own USB
          controller.
        - First plug of a Thunderbolt or USB4 enclosure (98 SE): when Windows
          asks to restart after installing the enclosure's PCI bridges, answer
          No. If you answered Yes and the drive does not appear, run asd from
          Start, Run, tick "Enumerating a device => PCI standard PCI-to-PCI
          bridge", click OK and restart.
        - Rarely, a start stops with "A fatal exception 0E has occurred at
          019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170" (ME). Turn
          the computer off and on again; the next start works.
        - USB audio (98 SE): the first USB audio device plugged in after a
          boot can freeze Windows, also with Windows' own drivers. Plug the
          device in before switching on and leave it plugged in. For a webcam
          whose microphone you don't need, usb-wdm-free's NOAUD98.INF avoids
          it.
        - Unplugging USB audio while it plays (98 SE): stop audio playback
          before unplugging a USB audio device. Unplugged while Windows' own
          USB audio driver plays it, the device can freeze Windows. ME no
          longer does this.
        - Free USB audio and MIDI drivers: Windows may ask for wdmaudio.cat,
          ks.cat and other files of its own from the Windows CD. Point it at
          the CD's WIN98 folder, or click Skip File for the .cat files.
        - Keyboard media keys (98 SE): a keyboard's volume and mute keys work
          after a restart; its other media keys need the keyboard maker's
          software. ME, 2000 and XP handle them.
        - A shared IRQ held by another device (98 SE): Windows crawls or
          freezes once xHCI98 is installed when a device with no working
          driver (an NVMe drive the BIOS still runs, or one whose driver
          failed) shares an xHCI98 controller's IRQ. Give that device a
          working driver or remove it; or, if no other working driver uses
          that IRQ, disable the xHCI98 controllers on it, or set
          XhciInterruptMode to 2 and restart. The USB Tree's snapshot names
          that device ("line held by another").
    * Windows 8.1 and 10
        - Stor98 still matches by the NVMe class there. Windows' own stornvme
          outranks it, so an internal drive keeps stornvme. Stor98 goes only
          on a drive the installer or you put it on.
    * 32-bit Windows 10
        - XHCIPFW is not offered: there it cannot start in time. On a board
          that leaves the USB4 ports' windows above 4 GB, 10 cannot use the
          devices behind those ports.

  Choose from 3 downloads

  Each zip holds everything in the one before it.
    * xhci98-3.4.2.0.zip: the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
      USB4, Thunderbolt networking), Stor98, the installer and the tools. It
      also has NUSB 3.3's seven Windows 98 SE storage files in USBDRV\NUSB33\,
      offered by the installer on 98 SE if you don't already have NUSB. If you
      have NUSB already installed, it will be kept.
    * xhci98-3.4.2.0-free.zip: adds the free USB class drivers (audio, MIDI,
      webcam, Ethernet, serial, printing etc.) from usb-wdm-free
      (https://github.com/yeokm1/usb-wdm-free) 0.1.9 in USBDRV\FREE\. These
      are experimental: they may not work with every device and can make
      Windows unstable. Please report their problems to that project. What is
      new in 0.1.9 is in its release notes
      (https://github.com/yeokm1/usb-wdm-free/releases).
    * xhci98-3.4.2.0-nonfree.zip: also adds a pack of other manufacturers' USB
      Ethernet, HID, serial and audio drivers from usb-wdm-nonfree
      (https://github.com/yeokm1/usb-wdm-nonfree) 1.3.0 in USBDRV\NONFREE\.
      New in this release: Microsoft's USB smart card reader driver (usbccid)
      for 2000 and XP, copied only; point Windows' hardware wizard at its
      folder.

  All free/non-free drivers are optional and installed only if you select
  them.

  Disclaimer

  This is a fairly new driver. It may have bugs or edge cases that are not yet
  accounted for given the large xHCI/USB 3/4/TB specifications and the many
  host controller implementations and devices. USB4/TB/NVMe features and the
  usb-wdm-free drivers are even more experimental, so use them at your own
  risk!

  Appreciate all issue reports!


==============================================================================
 2. INSTALL
==============================================================================

Unzip the WHOLE download first.

SETUP.EXE IS THE RECOMMENDED WAY TO INSTALL OR UPDATE. Double-click it in
the top directory: on Windows 2000 and XP sign in as an administrator, on
Vista and later Windows asks once for an administrator's approval. Go
through its pages - Welcome, Components, Thunderbolt and USB4 (when that
support is ticked), the free and non-free USB driver pages when this
download carries them, and Summary; the driver's options are under
Settings. Nothing changes before you click Install. Answer Windows'
unsigned-driver questions, do any steps its last page lists for you, and
restart when it offers.

  - Windows 2000 to 7: it installs or updates the driver of every device
    present and stages the package for devices plugged in later.
  - Windows 98 SE and ME: it copies the package to the install directory
    (C:\XHCI98 unless you choose another) and installs each device through
    Windows' own installer. A USB 3 controller added later may ask once
    for the "xhci98 xHCI Host Controller Disk": give it that directory.
  - Windows 8, 8.1 and 10: it installs the Thunderbolt 3 / USB4 support,
    and this package's USB drivers only if you tick them. Secure Boot must
    be off, or it refuses before changing anything.

It also installs the xHCI98 USB Tree (XHCITR), which shows every USB and
Thunderbolt or USB4 device and removes a Thunderbolt or USB4 device
safely from its icon by the clock. To uninstall, use Remove in
Add/Remove Programs (Programs and Features on Vista and later). Every run
adds to XHCIINST.LOG at the root of the system drive.

BY HAND, without SETUP.EXE: in Device Manager find the unrecognised xHCI
controller (a yellow mark, usually under "Other devices"), then
Properties, Driver, Update Driver, Have Disk, and point it at the folder
for your Windows:

      98-ME\    on Windows 98 SE and ME
      NT-X86\   on 32-bit Windows 2000 to 10
      NT-X64\   on 64-bit Windows

Point Windows at that folder, never at a loose .sys file.

If you pick the wrong one nothing breaks - Windows finds no driver in it
and says so - so try another.

A UAS disk or an external hub asks for its driver when plugged in: give
it the same folder; a Thunderbolt or USB4 controller its CM folder. The
release notes' "Installing" sections have every system's steps, and
INTEGRATE.TXT the F6 disk of Windows 2000 and XP Setup and how to add
the drivers to a Windows setup with nLite, NTLite or DISM.

64-BIT WINDOWS: the same steps, pointed at NT-X64\. The 64-bit
drivers are test-signed, so Vista x64 to 10 x64 run them in test-signing
mode (on 8, 8.1 and 10 turn Secure Boot off in the firmware setup first):
in a Command Prompt run as administrator type

      bcdedit /set testsigning on

and restart; "Test Mode" then shows on the desktop. Windows still says it
cannot verify the publisher: choose "Install this driver software anyway".
To check: bcdedit /enum {current} shows "testsigning Yes". Keep it on while
the Thunderbolt 3 / USB4 driver is installed. With BitLocker on the system
drive, suspend it before changing the boot setting. This is not Microsoft
signing: the certificate is this project's own test certificate
(xhci98-test.cer, beside the drivers; you need not import it). F8, Disable
Driver Signature Enforcement, also works for one start.


==============================================================================
 3. THE FILES WINDOWS SUPPLIES, AND USB STORAGE ON WINDOWS 98 SE
==============================================================================

Two files the driver depends on, usbd.sys and usbui.dll, are Windows' own
and are not in this download. The install asks Windows to copy them from
its own installation source, each only if it is absent:

  WINDOWS 98 SE   Have the Windows 98 SE CD at hand (or its CABs on the
  AND ME          disk): Windows may show "Insert Disk". Give it the CD's
                  WIN98 folder (the Windows ME CD on ME).
  WINDOWS 2000    Nothing to do: both come from the driver cache, with no
  AND XP          prompt.
  XP X64          usbd.sys comes from Driver Cache\amd64. From 3.4.2.0
                  SETUP.EXE copies usbui.dll from Windows' own files
                  when it is missing, for the controller's Advanced
                  tab and the root hub's Power tab; answer Windows'
                  "Windows Logo testing" box with Continue Anyway.
                  Update Driver alone does not copy it.
  VISTA TO 10     Nothing to do: every installation has both.

USB storage on Windows 98 SE, UAS disks included, needs seven files that
NUSB 3.3 and NUSB 3.6 install (USBSTOR.INF and .SYS, USBAUTH.SYS,
USBU2A.SYS, USBNTMAP.INF and .SYS, USBMPHLP.PDR). SETUP.EXE offers them
when NUSB is not installed.

They are Microsoft's, apart from USBU2A.SYS (a third party's). This
download carries NUSB 3.3's copies of them, unmodified, in a folder of their
own with a NOTICE.md saying whose they are (LICENCE, at the end).

Without them a USB stick has no driver (Code 28) and a UAS disk sits at
Code 2. On Windows ME, Windows' own storage files serve USB sticks: plug an
ordinary stick in once, so ME copies them, before a UAS disk.


==============================================================================
 4. REPORTING A PROBLEM
==============================================================================

This is a hobby driver for systems that left support two decades ago, and
bugs are not unexpected. Please report what you find:

      https://github.com/yeokm1/xhci98/issues

and attach:

  - The driver's report: set the logging level to 2 (the default; View >
    Logging level in the xHCI98 USB Tree) and restart if you changed it;
    make the problem happen, then use File > Save Report (or XHCITR
    /report <file>) and attach the .TXT. At level 4 the report leaves the
    log out, as it holds internal addresses.
  - The driver's snapshot: right after the report, File > Save Snapshot
    (or XHCITR /snapshot; XHCITR64 on 64-bit Windows), and attach the four
    files it writes (.DEV, .TXT, .BIN and .PSC).
  - For a problem with the install, XHCIINST.LOG from the root of the
    system drive.
  - The DOS report, if a maintainer asks for it: run QUALREP.BAT from real
    DOS in TOOLS\XHCIQUAL\ and attach the .LOG it names (read-only).

Problems with the free USB class drivers go to usb-wdm-free's own issues
(https://github.com/yeokm1/usb-wdm-free/issues).


==============================================================================
 5. WHAT IS IN THIS DIRECTORY
==============================================================================

  98-ME\  - Windows 98 SE and ME

      xhci98.inf
      xhci98.sys   349,083 bytes
      SHA-256
      6575E65ECE0DB52D28FE6C48D542F88E1694FFB6A962842099946577F3DA5EDF
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  43,888 bytes
      SHA-256
      17F0440EAB89F9B00B50621D45F7844039CD43F582150C50700FD9AF764492DF
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  456,942 bytes
      SHA-256
      1F8929CF78B7E931F1A8435D7FD5B3C670C694071C0F02BF7483F1598625AFF4
      CM\XHCIPFW.VXD (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys
      STOR98\      Stor98, the NVMe driver (below)

  NT-X86\  - 32-bit Windows 2000, XP, Vista, 7, 8, 8.1 and 10

      xhci98.inf
      xhci98.sys   349,083 bytes
      SHA-256
      6575E65ECE0DB52D28FE6C48D542F88E1694FFB6A962842099946577F3DA5EDF
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  43,888 bytes
      SHA-256
      17F0440EAB89F9B00B50621D45F7844039CD43F582150C50700FD9AF764492DF
      xhcihlpr.sys (the helper for 32-bit 2000 to 10)
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  456,942 bytes
      SHA-256
      1F8929CF78B7E931F1A8435D7FD5B3C670C694071C0F02BF7483F1598625AFF4
      CM\xhcihlpr.sys (the same helper: xhcicm.inf installs it too)
      CM\xhcicmf.inx (its tunnel-port filter, which SETUP.EXE installs;
                   not an INF for integration: INTEGRATE.TXT)
      CM\XHCIPFW.SYS (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys,
                   xhcinet6.sys (for 8, 8.1 and 10)
      STOR98\      Stor98, the NVMe driver (below)

  NT-X64\  - 64-bit Windows: XP x64, Server 2003 x64, Vista to 10

      xhci98.inf
      xhci98.sys   401,880 bytes
      SHA-256
      C299D29492B9C509F033FC187045ED279A4CE89EA7F1D6EF2B5A298A7784CCAF
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  57,304 bytes
      SHA-256
      ADAA9C4E7BE0F29B4DB72F04EC6A49C9248E4998DBA43C7E5D65732869B3A579
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      xhci98-test.cer (the test certificate every .sys file here is
                   signed with; you need not import it; section 2)
      xhci98.cat and the other .cat files (one per INF, signed with
                   it: Windows 8, 8.1 and 10 x64 need them)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  498,648 bytes
      SHA-256
      A2560A36A0462D09110342D2959E4C6515DB44126257CAF9EBE9A54D59EE74E9
      CM\xhcicmf.inx (its tunnel-port filter, which SETUP.EXE installs;
                   not an INF for integration: INTEGRATE.TXT)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys,
                   xhcinet6.sys (for 8, 8.1 and 10), xhcinet.cat
      STOR98\      Stor98, the NVMe driver (below)

  Which folder: 98-ME for Windows 98 SE and ME, NT-X86 for 32-bit
  Windows 2000 to 10, NT-X64 for 64-bit Windows. If you pick the wrong
  one nothing breaks - Windows simply finds no driver in it and says
  so - so try another. INTEGRATE.TXT says which folder to give nLite,
  NTLite or DISM.

  USBDRV\NUSB33\  - NUSB 3.3's Windows 98 SE storage files (section 3)

      NOTICE.md    whose they are: not this project's
      SHA256.TXT   their hashes
      W98SE\       the seven files, unmodified

  (No other USB drivers in this download: see THIS DOWNLOAD, at the top.)

  TOOLS\XHCIQUAL\  - the DOS report tool, when a maintainer asks (section 4)

      XHCIQUAL.EXE 178,338 bytes
      XHCIQUAL.MAP keep it beside the EXE; see TOOLS\XHCIQUAL\readme.txt
      QUALREP.BAT  the read-only probe a bug report asks for, saved
                   to a new .LOG named from the date and time
      readme.txt
      NOTICE.TXT   third-party notices this EXE carries

  TOOLS\XHCIMARK\ - the Thunderbolt safe-removal mark service

      XHCIMARK.EXE 167,936 bytes, for 32-bit Windows 2000 to 10
      XHCIMK64.EXE 207,360 bytes, for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      Installed by SETUP.EXE with the Thunderbolt 3 /
      USB4 component on Windows 2000 to 10; never run by
      hand. It keeps a Thunderbolt device listed in
      Safely Remove Hardware across a driver install,
      update or a disable and enable.

  SETUP.EXE       - installs or updates the drivers: double-click
                    it (69,632 bytes); Remove in Add/Remove
                    Programs uninstalls them
  INSTALL.BAT     - for command-line and scripted installs:
                    TOOLS\XHCIINST\readme.txt is the reference

  TOOLS\XHCIINST\ - the programs SETUP.EXE and INSTALL.BAT run

      XHCIWIZ.EXE  679,936 bytes, the installer's window
      XHCIWZ64.EXE 1,006,080 bytes, the same for 64-bit Windows
      XHCIINST.EXE 593,920 bytes, the console tool
      XHCIIN64.EXE 898,048 bytes, the same for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      Section 2 has how to install; readme.txt here is the
      command-line reference.

      XHCIDI.EXE   14,920 bytes, beside them: the 16-bit
      helper that installs the drivers on Windows 98 SE
      and ME devices; the install tool starts it

  TOOLS\XHCITR\   - the xHCI98 USB Tree

      XHCITR.EXE   565,248 bytes, for 32-bit Windows
      XHCITR64.EXE 754,176 bytes, for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      SETUP.EXE copies the one for your Windows into the
      install directory it asks for (C:\XHCI98 unless you
      choose another) and starts it at each login
      (section 2). Its File > Save Report writes what a
      bug report asks for (section 4).

  TOOLS\PORTSW\  - the Intel port switchover's switch (the release notes)

      portsw.vbs     shows XhciIntelPortSwitch, and sets it on or off
      portsw-on.bat  turns it on in one double-click
      portsw-off.bat turns it off in one double-click

      One copy for every driver directory above. Run them from
      this directory.

  TOOLS\SILENT\  - the silent install, Windows 98 SE and ME only

      SILENT.REG     the list of device classes that may install
                     with no wizard
      SILENT.VBS     shows XhciSilentInstall and the list, and sets
                     the value on or off
      silent-on.bat  merges SILENT.REG, checks it, then turns it on
      silent-off.bat turns it off and keeps the list

      OFF BY DEFAULT. With XhciSilentInstall at 1 on the
      controller, a device whose class is in the list installs
      with no Add New Hardware wizard; at anything else, or
      absent, nothing changes. The install writes it as 0,
      and an update keeps a 1 already there.
      Never on Windows 2000 and later.

      The list: hubs, boot keyboards and mice, and Bulk-Only
      USB storage. UAS is not in it: its driver is this
      package's own, and its first install is the wizard's.

      With no wizard, 98 SE asks for a missing file from its
      CD (a keyboard needs KBDHID.VXD), so a device whose
      driver is not on the machine yet needs the CD at hand.

      Double-click silent-on.bat or silent-off.bat here, or run
      it at a prompt in this directory, then RESTART WINDOWS.
      silent-off.bat leaves the list in place, since another
      USB stack may read the same key. To delete it too, in
      regedit, under
        HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB
      delete the SilentClasses key, and restart.

      Windows ME does not need it for a keyboard, mouse, stick
      or hub of its own: it installs them with no wizard
      already. On 98 SE they install with no wizard once it is
      on, the keyboard and mouse after the CD prompt above. The
      information on how the silent install is decided, and the
      list, are from SweetLow.

  LICENSE

  The GNU GPL v2 or later this driver is published under, with the
  note on what in the wider project is third-party material and is
  NOT covered by it. The LICENCE section at the end points here.

  Bundled in this download: Stor98 3.4.2.0, built with this release.

  98-ME\STOR98, NT-X86\STOR98 and NT-X64\STOR98 hold Stor98, an NVMe storage
  driver for NVMe drives such as one in a Thunderbolt or USB4 enclosure:
  stor98.inf and the files beside it whose names start with stor98. It carries
  this release's version and date, and its own licence (BSD-3-Clause):
  stor98-LICENSE.txt and stor98-NOTICE.txt are beside it, and
  stor98-readme.txt is its guide. SETUP.EXE installs it with the Thunderbolt 3
  / USB4 support (its box under that one on the installer's Components page,
  ticked with it but on Windows 10, where Windows' own stornvme stays and
  Stor98 is yours to tick; untick it to leave Stor98 out), and only on NVMe
  drives behind a Thunderbolt or USB4 tunnel: the installer selects tunnel
  drives only and leaves an internal NVMe drive as it is. Stor98 is not for
  internal NVMe drives. The package gives a drive behind a Thunderbolt or USB4
  port an id of its own, PCI\XHCI98_TUNNEL&CC_010802, and on Windows 2000 to 8
  stor98.inf matches that id alone, so Windows never offers Stor98 for an
  internal drive there. On 8.1 and 10 it names PCI\CC_010802 as a compatible
  id only, which Windows' own NVMe driver (stornvme) outranks, so there Stor98
  goes only on a drive the installer, its safe-removal mark service or you put
  it on. On 98 SE and ME, which have no NVMe driver of their own, Stor98
  installs from 98-ME\STOR98's stor98.inf, which matches every NVMe
  controller, so Windows may offer it for an internal NVMe drive that has no
  driver: decline it there. To install it by hand on a tunnel drive, in the
  hardware wizard (Have Disk), point at the STOR98 folder of your Windows'
  folder.

  NT-X64\STOR98's stor98.sys is signed with the same test certificate as
  this project's other x64 drivers (xhci98-test.cer), in the same
  step: test-signing mode loads them all.

  98-ME\STOR98's S98VSD.VXD is for Windows 98 SE and ME: it lets reads
  and writes of up to 2 MiB reach Stor98 whole. SETUP.EXE installs it
  with Stor98 there (its own box, ticked with Stor98's).

The drivers carry the same names in every directory, and every copy
carries driver version 3.4.2.0, so point Windows at a directory rather
than at a copied file.


==============================================================================
 6. EARLIER RELEASES
==============================================================================

  3.4.1.0, 3.4.0.0, 3.3.0.0, 3.2.0.0, 3.1.0.0, 3.0.0.0, 2.4.0.0, 2.3.1.0,
  2.3.0.0, 2.2.0.0, 2.1.1.0, 2.1.0.0, 2.0.0.0, 1.2.0.0, 1.1.1.0, 1.1.0.0,
  1.0.2.0, 1.0.1.0, 1.0.0.1, 1.0.0.0.

What each of them changed is in the project's release history,
releases/history.md, and the full release notes are at the address at the
top of this file. XHCIQUAL, the DOS tool in TOOLS\XHCIQUAL\, is described
in their section "XHCIQUAL: checking a machine's controller (optional)".


==============================================================================
 LICENCE
==============================================================================

GNU GPL v2 or later - see the LICENSE file in this directory, beside this
readme. This applies to this project's own code in xhci98.sys, xhciuas.sys,
xhcicm.sys and the 32-bit helper xhcihlpr.sys, and to their INFs. The 64-bit
builds of the first three, and xhcihlpr.sys, also carry a few small support
routines that Microsoft's driver kit links in (a stack-cookie check, and in
xhcihlpr.sys a device-security helper), which are not this project's work;
docs/contributing/legal-provenance.md, in the project's source repository,
records them.

Microsoft files are in this download in one place only: the seven Windows
98 SE storage files of section 3, in a folder of their own, copied
unmodified from NUSB 3.3 (Microsoft's, apart from USBU2A.SYS, a third
party's). They are not this project's work. No licence to redistribute them
has been established, nothing here grants you any right in them, and the
NOTICE.md beside them says whose they are. The usbd.sys and usbui.dll the
install needs are not in this download: Windows copies them from your own
Windows installation source (section 3).

The provenance record for everything the project depends on but does not own
is in docs/contributing/legal-provenance.md, in the project's source
repository rather than here.
