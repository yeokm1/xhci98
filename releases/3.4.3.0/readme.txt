==============================================================================
                              x h c i 9 8   3.4.3.0
    USB 3.x for Windows 98 SE to 7, Thunderbolt 3 and USB4 for 98 SE to 10
==============================================================================

Released 2026-11-10.

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

This is xhci98-3.4.3.0.zip, the base download of xhci98 3.4.3.0: the drivers,
their tools and installer, and NUSB 3.3's Windows 98 SE storage files
(USBDRV\NUSB33\).

This release has these downloads, each holding the one before it:

  xhci98-3.4.3.0.zip         the base download
  xhci98-3.4.3.0-free.zip    adds the free USB class drivers (USBDRV\FREE\)
  xhci98-3.4.3.0-nonfree.zip adds the non-free driver pack (USBDRV\NONFREE\)

The installer copies those drivers only when you tick them, and never
installs one by itself. The up-to-date drivers, free and non-free, are at:

  https://github.com/yeokm1/usb-wdm-free
  https://github.com/yeokm1/usb-wdm-nonfree


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. What is new in 3.4.3.0
  2. Install
  3. The files Windows supplies, and USB storage on Windows 98 SE
  4. Reporting a problem
  5. What is in this directory
  6. Earlier releases


==============================================================================
 1. WHAT IS NEW IN 3.4.3.0
==============================================================================

  New

    * Standby and hibernation with a Thunderbolt or USB4 drive attached are
      supported on Windows 2000 and later. The USB4 connection manager puts
      the devices to sleep in order and builds their connections again at
      resume (XhciCmSleepEntry, on by default). On Windows 98 SE and ME, stop
      and unplug the drive first ("Known limitations").
    * A USB 3 device whose SuperSpeed link keeps failing (three resets within
      a minute, then a fourth failure), at a root port or behind a USB 3 hub,
      is moved to the USB 2.0 side of the same connector or hub, where it runs
      at High Speed, instead of being reset again and again.
      XhciSsFlapFallback 0 leaves such a device off until it is plugged in
      again.
    * XhciDmaStaging (0 by default): 1 passes every transfer's data through
      memory of the driver's own, so that a disable, update or removal of a
      controller that has stopped answering completes instead of waiting, at
      the cost of one more copy of the data. XhciStagePoolKiB sets the size of
      that memory (docs/using/tuning.md).
    * Windows 98 SE and ME: a Thunderbolt dock's second and further USB
      controllers, which Windows leaves at Code 29 for want of an interrupt,
      start without one from the dock's first plug, xhci98.sys polling them
      (XhciCmNoIrqStart).
    * Windows 98 SE and ME: the Thunderbolt or USB4 controller's interrupt.
      XhciCmRingInterrupt 1, with XhciPfwNhiLine 0, now works on these systems
      too: a plug, an unplug and network traffic are reported at once (off by
      default; "Thunderbolt networking speed on 98 SE and ME: the controller's
      interrupt", docs/using/tuning.md). On every system network traffic is
      reported by that interrupt where it is on (XhciCmNetInterrupt, on by
      default).
    * Daisy chains on USB4 hosts whose tunnels xHCI98 builds: on 98 SE and ME
      a device behind another device is set up when it is plugged in, removed
      when it is unplugged, and marked removable; on 2000 to 10 stopping it
      closes its own connection.
    * A device already attached when Windows starts, on a USB4 host that does
      not tell the driver which of its ports leads where (the ASMedia
      ASM4242): XhciCmInferAtStart 1 (off by default) lets the driver work out
      its port when only one such device is attached and two separate readings
      agree, so that safely removing it closes its connection.
    * Stor98 on Windows 98 SE: StorTakeFirmware (0 by default): 1 takes an
      NVMe controller the BIOS has set up, which Stor98 otherwise leaves to
      the BIOS; stortakefirmware=1 in one controller's AdapterSettings takes
      that one alone. Taking it can hang Windows' start or corrupt data if the
      BIOS still uses the drive. Ignored on ME.
    * 32-bit Windows 10, version 2004 or later: XHCIPFW.SYS, which closes PCI
      bridges' windows above 4 GB, is offered as on 8 and 8.1.
    * The USB Tree: USB drive notices, off unless you tick "USB drive notices"
      on the Settings window's xHCI98 USB Tree tab, say when a USB drive is
      connected or removed ("SanDisk Ultra (F:) is connected.").
    * The USB Tree: Thunderbolt networking. A node under the Thunderbolt or
      USB4 controller shows each link's state, the other computer, the speed,
      the addresses and the counters, also in the saved report and the
      snapshot (where the addresses and the other computer's name are left
      out).
    * Two tips for Thunderbolt networking speed on 98 SE and ME
      (docs/using/tuning.md): the systems' own TCP receive window
      (DefaultRcvWindow 64240) can take data coming in from about 10 to about
      100 Mbit/s; the controller's interrupt (above) makes the link about
      three times faster in both directions, where the BIOS gives the
      controller an interrupt no other device holds.

  Changed

    * The installer
        - A reinstall or update at the same version replaces every installed
          file of this package whose bytes differ from the package's, on every
          system, as it replaces an older file. A newer version is still kept
          unless /force is given. /list says "Keep (same version)" or "Replace
          files (same version)".
        - Windows 98 SE and ME restart at the finish like every other system:
          after a Stor98 update, and on 98 SE after the storage drivers' copy
          with a UAS disk at Code 2, Setup offers "Restart Windows now" where
          it asked you to shut down and start Windows again. Disconnect your
          external NVMe drives before a Stor98 update, as before.
        - The Advanced lists show XhciSsFlapFallback and Stor98's
          StorTakeFirmware (98 SE; greyed on ME), and XhciCmRingInterrupt on
          every system.
        - The refusal while Windows is installing something names the program
          to run again, SETUP.EXE or INSTALL.BAT.
        - On 8, 8.1 and 10 XHCIPFW.SYS acts only at a full start: restart
          Windows after installing or updating it (with fast startup on, Shut
          down and power on is not a full start).
        - Windows 98 SE and ME: INSTALL.BAT waits for Setup's window and
          returns its exit code.
        - The text-mode Setup (F6) entry for Windows 2000 also serves 32-bit
          XP: "xHCI98 USB 3 Controller (2000/XP, USB keyboard/mouse, files
          from CD)".
    * The USB Tree
        - The Settings window shows XhciSsFlapFallback and Stor98's
          StorTakeFirmware.
        - When a tunnel stays up after an eject because the driver does not
          know which port the device is on, the eject says so.
        - /eject with the ID of a device inside a Thunderbolt or USB4 device
          (its drive, say) stops that whole device.
        - A unit's details say "Marked removable." on every system.
    * Windows 8.1: a tunnelled NVMe drive keeps its Thunderbolt id, and so
      Stor98, across a disable, a driver update or another restart of its own
      devices; on 8.1 Stor98 matches a drive by that id alone.
    * USB4 hosts whose tunnels xHCI98 builds: the port of a tunnel found up at
      start is learned only from the tunnel's own far end, so a function that
      answers late is never taken for the device.
    * The USB devices answer Windows' bus information request as Windows' own
      USB stack does.

  Fixed

    * Windows 2000: no USB audio played with 3.4.2.0 (GitHub issue 32).
      3.4.2.0 refused the request with which Windows 2000's own USB audio
      driver chooses its stream, because the request's header gave a length of
      0 ("Your audio hardware cannot play files like the current file"). A
      request whose header states a length too short for it is now served and
      counted, as Windows' own USB stack serves it.
    * Windows 98 SE and ME, Thunderbolt networking with the controller's
      interrupt on: a fatal exception 0E inside Windows' TCP/IP (MSTCP) while
      sending, and data coming in that stalled. The network driver now hands
      Windows what it receives and the sends it completes from an NDIS timer,
      as these systems expect.
    * Thunderbolt networking with a Windows 11 computer: the link came up and
      carried nothing, and after the cable was plugged in again at the Windows
      end the other computer was not found again. Both work.
    * An audio or video request whose last packet failed part-way could wait
      for its cancellation; it ends at once.
    * A USB 2.0 endpoint whose maximum packet size its speed does not list was
      refused; it is opened as declared, or at the speed's maximum.
    * A SuperSpeed hub port whose move to USB 2.0 failed was left with nothing
      more reported; the move is tried again.
    * XHCIPERF (the developer diagnostic in the source tree) on 98 SE and ME:
      Stop and closing the window wait for the disk to answer a request
      already sent, which these systems cannot cancel.

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
        - Standby and hibernation with a Thunderbolt or USB4 drive attached
          are supported on Windows 2000 and later. On Windows 98 SE and ME,
          stop and unplug the drive first. On a computer whose firmware does
          not restore the Thunderbolt or USB4 connection at resume, stop the
          drive before standby or hibernation.
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
          it is unplugged, except on 2000 to 10 with a USB4 host whose tunnels
          xHCI98 builds: unplug it after the stop.
        - A device attached when Windows started, on a USB4 host that does not
          tell the driver which of its ports leads where, keeps its connection
          after a stop until it is unplugged, unless XhciCmInferAtStart is 1;
          the eject says so.
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
          (some HP laptops of 2012 with the Intel 7 Series USB 3 controller)
          is polled where Windows can give it no line, which uses a little
          more processor time; on Windows 2000 on the listed computers it is
          always polled. On a computer whose BIOS turns that controller on
          before Windows starts, Windows 2000 with an ACPI computer type still
          stops at start (STOP 0x000000A5): install Windows 2000 there as
          "Standard PC" (F7 in text-mode Setup). On the listed computers
          XhciIntelNoRouteIrq 0 brings that stop back under an ACPI computer
          type. If 0 was set after the last start that reached the desktop,
          "Last Known Good Configuration" at F8 undoes it; otherwise only
          "Standard PC" starts Windows 2000 there (docs/using/tuning.md).
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
        - The first time a Thunderbolt dock is plugged in, Windows asks to
          restart for each of its PCI bridges; until the restart some of the
          dock's devices do not start.
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
        - Windows ME (and rarely 98 SE): with a Thunderbolt dock or chain
          attached at power-on, the first start after the install can stop
          with a fatal exception 0E. Setting XhciCmReenum to 0 avoids it, at
          the cost of no automatic re-enumeration on 9x: Windows then finds
          devices plugged in behind the Thunderbolt port only at Device
          Manager's Refresh.
        - A USB disk left plugged in when the computer starts can hold Windows
          at startup for minutes, or stop it. Plug USB disks in after Windows
          has started. To help find a fix, you can send an EDD report
          (docs/using/edd-reports.md).
        - A drive the BIOS has set up at power-on (one it can start from)
          stays the BIOS's unless StorTakeFirmware is 1 (98 SE): its Stor98
          entry shows Code 10. 98 SE reaches the drive through the BIOS; ME
          may not reach it at all.
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
    * Windows 10
        - Stor98 still matches by the NVMe class there. Windows' own stornvme
          outranks it, so an internal drive keeps stornvme. Stor98 goes only
          on a drive the installer or you put it on.
    * 32-bit Windows 10 before version 2004
        - XHCIPFW is not offered. On a board that leaves the USB4 ports'
          windows above 4 GB, 10 cannot use the devices behind those ports.

  Choose from 3 downloads

  Each zip holds everything in the one before it.
    * xhci98-3.4.3.0.zip: the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
      USB4, Thunderbolt networking), Stor98, the installer and the tools. It
      also has NUSB 3.3's seven Windows 98 SE storage files in USBDRV\NUSB33\,
      offered by the installer on 98 SE if you don't already have NUSB. If you
      have NUSB already installed, it will be kept.
    * xhci98-3.4.3.0-free.zip: adds the free USB class drivers (audio, MIDI,
      webcam, Ethernet, serial, printing etc.) from usb-wdm-free
      (https://github.com/yeokm1/usb-wdm-free) 0.1.9 in USBDRV\FREE\. These
      are experimental: they may not work with every device and can make
      Windows unstable. Please report their problems to that project. Its
      release notes are at https://github.com/yeokm1/usb-wdm-free/releases.
    * xhci98-3.4.3.0-nonfree.zip: also adds a pack of other manufacturers' USB
      Ethernet, HID, serial and audio drivers from usb-wdm-nonfree
      (https://github.com/yeokm1/usb-wdm-nonfree) 1.3.0 in USBDRV\NONFREE\.

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
      xhci98.sys   366,051 bytes
      SHA-256
      3B35B4AE102266B512DC97C8D4CE41746D53F4F51074508740FE81E6C9FC7266
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  43,880 bytes
      SHA-256
      BE716C6500563B9DAC475484BDADAC0D7250417C5FDF6CC9415EE150B13CB96E
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  505,062 bytes
      SHA-256
      9DBD715B3C2DF443370DE2532C15DDA73E13027303443F84DB1E200A8C4E0A14
      CM\XHCIPFW.VXD (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys
      STOR98\      Stor98, the NVMe driver (below)

  NT-X86\  - 32-bit Windows 2000, XP, Vista, 7, 8, 8.1 and 10

      xhci98.inf
      xhci98.sys   366,051 bytes
      SHA-256
      3B35B4AE102266B512DC97C8D4CE41746D53F4F51074508740FE81E6C9FC7266
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  43,880 bytes
      SHA-256
      BE716C6500563B9DAC475484BDADAC0D7250417C5FDF6CC9415EE150B13CB96E
      xhcihlpr.sys (the helper for 32-bit 2000 to 10)
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  505,062 bytes
      SHA-256
      9DBD715B3C2DF443370DE2532C15DDA73E13027303443F84DB1E200A8C4E0A14
      CM\xhcihlpr.sys (the same helper: xhcicm.inf installs it too)
      CM\xhcicmf.inx (its tunnel-port filter, which SETUP.EXE installs;
                   not an INF for integration: INTEGRATE.TXT)
      CM\XHCIPFW.SYS (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys,
                   xhcinet6.sys (for 8, 8.1 and 10)
      STOR98\      Stor98, the NVMe driver (below)

  NT-X64\  - 64-bit Windows: XP x64, Server 2003 x64, Vista to 10

      xhci98.inf
      xhci98.sys   419,800 bytes
      SHA-256
      77273BB0B86D61466A3FA70CA2BE1C62261D1D1369F18C8719A932080499287A
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  57,304 bytes
      SHA-256
      65053ECECA262A84DABCF7DDD80154DF445AAD85F63FD09D8EFE281E3AF9C78C
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      xhci98-test.cer (the test certificate every .sys file here is
                   signed with; you need not import it; section 2)
      xhci98.cat and the other .cat files (one per INF, signed with
                   it: Windows 8, 8.1 and 10 x64 need them)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  553,432 bytes
      SHA-256
      56F05F9F1ABEA42DADB96AD4BC4E5C35811B33223332899E6CD2E556F3E1A2EE
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

      XHCIMARK.EXE 176,128 bytes, for 32-bit Windows 2000 to 10
      XHCIMK64.EXE 211,968 bytes, for 64-bit Windows
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

      XHCIWIZ.EXE  692,224 bytes, the installer's window
      XHCIWZ64.EXE 1,018,368 bytes, the same for 64-bit Windows
      XHCIINST.EXE 610,304 bytes, the console tool
      XHCIIN64.EXE 912,384 bytes, the same for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      Section 2 has how to install; readme.txt here is the
      command-line reference.

      XHCIDI.EXE   14,920 bytes, beside them: the 16-bit
      helper that installs the drivers on Windows 98 SE
      and ME devices; the install tool starts it

  TOOLS\XHCITR\   - the xHCI98 USB Tree

      XHCITR.EXE   589,824 bytes, for 32-bit Windows
      XHCITR64.EXE 787,456 bytes, for 64-bit Windows
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

  Bundled in this download: Stor98 3.4.3.0, built with this release.

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
carries driver version 3.4.3.0, so point Windows at a directory rather
than at a copied file.


==============================================================================
 6. EARLIER RELEASES
==============================================================================

  3.4.2.0, 3.4.1.0, 3.4.0.0, 3.3.0.0, 3.2.0.0, 3.1.0.0, 3.0.0.0, 2.4.0.0,
  2.3.1.0, 2.3.0.0, 2.2.0.0, 2.1.1.0, 2.1.0.0, 2.0.0.0, 1.2.0.0, 1.1.1.0,
  1.1.0.0, 1.0.2.0, 1.0.1.0, 1.0.0.1, 1.0.0.0.

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
