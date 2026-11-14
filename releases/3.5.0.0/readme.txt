==============================================================================
                              x h c i 9 8   3.5.0.0
    USB 3.x for Windows 98 SE to 7, Thunderbolt 3 and USB4 for 98 SE to 10
==============================================================================

Released 2026-11-14.

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

This is xhci98-3.5.0.0.zip, the base download of xhci98 3.5.0.0: the drivers,
their tools and installer, and NUSB 3.3's Windows 98 SE storage files
(USBDRV\NUSB33\).

This release has these downloads, each holding the one before it:

  xhci98-3.5.0.0.zip         the base download
  xhci98-3.5.0.0-free.zip    adds the free USB class drivers (USBDRV\FREE\)
  xhci98-3.5.0.0-nonfree.zip adds the non-free driver pack (USBDRV\NONFREE\)

The installer copies those drivers only when you tick them, and never
installs one by itself. The up-to-date drivers, free and non-free, are at:

  https://github.com/yeokm1/usb-wdm-free
  https://github.com/yeokm1/usb-wdm-nonfree


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. What is new in 3.5.0.0
  2. Install
  3. The files Windows supplies, and USB storage on Windows 98 SE
  4. Reporting a problem
  5. What is in this directory
  6. Earlier releases


==============================================================================
 1. WHAT IS NEW IN 3.5.0.0
==============================================================================

  Fixed

    * A controller that the driver restarts in place after it stopped
      answering is given time to finish a slow reset, instead of being given
      up at once.
    * A UAS drive that a hardware monitor program asks for its SMART data no
      longer hangs until it is unplugged. A command the drive answered but
      whose data never arrived resets the drive so its volume can carry on.
    * 2000 to 10, a USB4 host with no pre-boot support: a drive behind a dock
      or in a chain no longer disappears about 40 seconds after a standby.
    * A controller older than xHCI 1.0 could stop answering with large event
      rings. It now always uses 256 entries.
    * Standby and hibernation with a Thunderbolt or USB4 drive on Stor98 (not
      on 98 SE and ME): a drive that woke before its connection was back could
      stop answering, and Windows could then not finish a copy to it, find new
      devices or shut down. The driver now powers it up again once its
      connection is back, so Windows keeps answering it and can shut down
      (XhciCmWakePowerUp). A drive plugged in after Windows started is still
      disconnected by the sleep and must be plugged in again.
    * 98 SE and ME: stopping a Thunderbolt or USB4 port while a drive behind
      it ran through the BIOS took the drive away under Windows and caused
      "Disk Write Error". That stop is now disallowed.

  Changed

    * The installer
        - Every registry value the drivers read is a row of Setup's settings,
          but for the few set on a page of their own (the log levels,
          Thunderbolt networking, the network adapter's own settings) and a
          UAS drive's own values. Among the new rows: XhciResumeKeepBot,
          XhciStreamDequeueRepair and XhciCmWakePowerUp.
        - A fresh install sets the event ring to 256 entries
          (XhciEventRingTrbs) for every controller, to maximise compatibility.
          You may increase it to 1024 if you have audio or video dropouts.
        - 2000 to 10: only one Setup runs at a time.
        - 98 SE and ME: If XHCIINST.LOG cannot be written at the root of the
          system drive, Setup writes it in the temp folder and shows where.
        - The safe-removal mark service no longer reads the Thunderbolt and
          USB4 devices again every second, and Setup holds it off while it
          updates devices.
    * The USB Tree
        - The settings page has a row for every registry value the drivers
          read.
        - Search box in settings page.
        - A snapshot takes every controller this driver drives.
        - A snapshot also holds more UAS troubleshooting information.
    * Goodbye to tools\portsw\: the USB Tree's Settings window can do this
      now.
    * A UAS drive that refuses an ATA pass-through request as unknown is asked
      once. After that refusal the driver answers the same request itself, for
      that drive and unit, until the drive is plugged in again.
    * Two drives that report the same serial number are no longer kept across
      a resume that starts the controller afresh, so that one cannot be taken
      for the other.

  New

    * Windows 2000: a USB drive that is not UAS (a Bulk-Only stick or disk) is
      kept across a hibernation that starts the controller afresh, with its
      drive letter and open files.
    * Intel 7-, 8- and 9-series and C610/X99 chipsets: the port switchover at
      a first install. Setup enables it by default so that USB 3 devices on
      the blue connectors run at USB 3 speed. The driver's own default stays
      off for stability during Windows Setup. To change it, use the USB Tree's
      View > Settings..., and restart Windows.
    * Another manufacturer's USB 3 driver on a controller is left as it is
      unless you tick the Setup option (not on 98 SE and ME). xHCI98 then
      replaces it there, one controller at a time, and Setup refuses a
      controller the Windows disk is behind.
    * Windows 98 SE: a drive behind a Thunderbolt or USB4 port is kept across
      a standby. A dock's or a chain's drives are removed after the wake and
      found again a few seconds later.
    * A port on hold says so. When a device keeps failing, the driver stops
      trying its port. The USB Tree now shows such a port as "(on hold)".

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

  Choose from 3 downloads

  Each zip holds everything in the one before it.
    * xhci98-3.5.0.0.zip: the xHCI98 drivers (USB 3, UAS, Thunderbolt 3 /
      USB4, Thunderbolt networking), Stor98, the installer and the tools. It
      also has NUSB 3.3's seven Windows 98 SE storage files in USBDRV\NUSB33\,
      offered by the installer on 98 SE if you don't already have NUSB. If you
      have NUSB already installed, it will be kept.
    * xhci98-3.5.0.0-free.zip: adds the free USB class drivers (audio, MIDI,
      webcam, Ethernet, serial, printing etc.) from usb-wdm-free
      (https://github.com/yeokm1/usb-wdm-free) 0.1.10 in USBDRV\FREE\. These
      are experimental: they may not work with every device and can make
      Windows unstable. Please report their problems to that project.
        - The network drivers (rtl815x, ax88179, ax88772, cdcnet) gain the
          Advanced tab (Energy-Efficient Ethernet, speed and duplex, flow
          control, offloads). cdcnet tries again when an adapter does not come
          back after standby.
        - uac2 and uac1: sound starts again after a stall; a recording or
          playback opened just after another closed waits for it instead of
          failing; a device with no product name is shown as "USB Audio 2.0
          (uac2)" or "USB Audio 1.0 (uac1)".
        - uac1 (98 SE) also runs devices with 8-bit, floating-point, A-law or
          mu-law sound, and is copied only: point Update Driver at its folder.
        - uvc98: a webcam stream starts again after a stall.
    * xhci98-3.5.0.0-nonfree.zip: also adds a pack of other manufacturers' USB
      Ethernet, HID, serial and audio drivers from usb-wdm-nonfree
      (https://github.com/yeokm1/usb-wdm-nonfree) 1.4.0 in USBDRV\NONFREE\.
        - Realtek drivers with modified INFs to support RTL8157 and RTL8159
          adapters on Windows 7, 8 and 8.1.

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
adds to XHCIINST.LOG at the root of the system drive, or, on 98 SE and ME,
in the temp folder if that file cannot be written; Setup shows the path.

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
    /report <file>) and attach the .TXT. At level 4 the report includes
    the log with the memory addresses it holds.
  - The driver's snapshot: right after the report, File > Save Snapshot
    (or XHCITR /snapshot; XHCITR64 on 64-bit Windows), and attach every
    file it writes: four for each controller, and two for each
    Thunderbolt or USB4 controller, named from the date and time.
  - For a problem with the install, XHCIINST.LOG from the root of the
    system drive (or from the path Setup showed).
  - The DOS report, if a maintainer asks for it: run QUALREP.BAT from real
    DOS in TOOLS\XHCIQUAL\ and attach the .LOG it names (read-only).

Problems with the free USB class drivers go to usb-wdm-free's own issues
(https://github.com/yeokm1/usb-wdm-free/issues).


==============================================================================
 5. WHAT IS IN THIS DIRECTORY
==============================================================================

  98-ME\  - Windows 98 SE and ME

      xhci98.inf
      xhci98.sys   445,099 bytes
      SHA-256
      B3EA6650D0DF7F64E442E33C065414D82C905A1DC7D72B8F535678409DAF4E49
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  45,712 bytes
      SHA-256
      50254935A448B4716D4376262A91624C4AE9E13123E1F385B116989DDB6EAFDA
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  637,326 bytes
      SHA-256
      CB74EBC1622A1F41B5B64766B82790A08455D2492147C834C55FB59874176659
      CM\XHCIPFW.VXD (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys
      STOR98\      Stor98, the NVMe driver (below)

  NT-X86\  - 32-bit Windows 2000, XP, Vista, 7, 8, 8.1 and 10

      xhci98.inf
      xhci98.sys   445,099 bytes
      SHA-256
      B3EA6650D0DF7F64E442E33C065414D82C905A1DC7D72B8F535678409DAF4E49
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  45,712 bytes
      SHA-256
      50254935A448B4716D4376262A91624C4AE9E13123E1F385B116989DDB6EAFDA
      xhcihlpr.sys (the helper for 32-bit 2000 to 10)
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  637,326 bytes
      SHA-256
      CB74EBC1622A1F41B5B64766B82790A08455D2492147C834C55FB59874176659
      CM\xhcihlpr.sys (the same helper: xhcicm.inf installs it too)
      CM\xhcicmf.inx (its tunnel-port filter, which SETUP.EXE installs;
                   not an INF for integration: INTEGRATE.TXT)
      CM\XHCIPFW.SYS (which SETUP.EXE places)
      CM\TBNET\    Thunderbolt networking: xhcinet.inf, xhcinet.sys,
                   xhcinet6.sys (for 8, 8.1 and 10)
      STOR98\      Stor98, the NVMe driver (below)

  NT-X64\  - 64-bit Windows: XP x64, Server 2003 x64, Vista to 10

      xhci98.inf
      xhci98.sys   496,600 bytes
      SHA-256
      44A385737E4D3B24F9DA1EEDFD710B816ED9CE6DAAA554CAAA4E04306D676BA4
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  59,352 bytes
      SHA-256
      A6811824CC57961149D510BF3EA362D30EED858E480D372E7F270C11D88A0F8B
      txtsetup.oem (Setup's F6 disk: this folder's root files)
      xhci98-test.cer (the test certificate every .sys file here is
                   signed with; you need not import it; section 2)
      xhci98.cat and the other .cat files (one per INF, signed with
                   it: Windows 8, 8.1 and 10 x64 need them)
      CM\xhcicm.inf  (the Thunderbolt 3 / USB4 connection manager's INF)
      CM\xhcicm.sys  688,088 bytes
      SHA-256
      A9F8C102D18FB198E3A15D5F58A37F2D993C9D42D9A20986C30DDA2C3A0BBAF7
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

      XHCIQUAL.EXE 178,346 bytes
      XHCIQUAL.MAP keep it beside the EXE; see TOOLS\XHCIQUAL\readme.txt
      QUALREP.BAT  the read-only probe a bug report asks for, saved
                   to a new .LOG named from the date and time
      readme.txt
      NOTICE.TXT   third-party notices this EXE carries

  TOOLS\XHCIMARK\ - the Thunderbolt safe-removal mark service

      XHCIMARK.EXE 200,704 bytes, for 32-bit Windows 2000 to 10
      XHCIMK64.EXE 243,712 bytes, for 64-bit Windows
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

      XHCIWIZ.EXE  745,472 bytes, the installer's window
      XHCIWZ64.EXE 1,090,048 bytes, the same for 64-bit Windows
      XHCIINST.EXE 655,360 bytes, the console tool
      XHCIIN64.EXE 986,112 bytes, the same for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      Section 2 has how to install; readme.txt here is the
      command-line reference.

      XHCIDI.EXE   14,920 bytes, beside them: the 16-bit
      helper that installs the drivers on Windows 98 SE
      and ME devices; the install tool starts it

  TOOLS\XHCITR\   - the xHCI98 USB Tree

      XHCITR.EXE   671,744 bytes, for 32-bit Windows
      XHCITR64.EXE 893,440 bytes, for 64-bit Windows
      readme.txt
      NOTICE.TXT   third-party notices these EXEs carry

      SETUP.EXE copies the one for your Windows into the
      install directory it asks for (C:\XHCI98 unless you
      choose another) and starts it at each login
      (section 2). Its File > Save Report writes what a
      bug report asks for (section 4).

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

  Bundled in this download: Stor98 3.5.0.0, built with this release.

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
carries driver version 3.5.0.0, so point Windows at a directory rather
than at a copied file.


==============================================================================
 6. EARLIER RELEASES
==============================================================================

  3.4.3.0, 3.4.2.0, 3.4.1.0, 3.4.0.0, 3.3.0.0, 3.2.0.0, 3.1.0.0, 3.0.0.0,
  2.4.0.0, 2.3.1.0, 2.3.0.0, 2.2.0.0, 2.1.1.0, 2.1.0.0, 2.0.0.0, 1.2.0.0,
  1.1.1.0, 1.1.0.0, 1.0.2.0, 1.0.1.0, 1.0.0.1, 1.0.0.0.

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
