==============================================================================
                              x h c i 9 8   2.1.0.0
    USB 3.x for Windows 98 SE, ME, 2000, XP, Vista and 7 on xHCI machines
==============================================================================

Released 2026-10-05.

Most PCs made from the mid 2010s onward have only USB 3.x (xHCI)
controllers, which older Windows cannot use. This package drives them on:

  - Windows 98 SE, ME, 2000 SP4, XP, Vista and 7, 32-bit
  - Windows XP, Vista and 7, x64, through separate 64-bit drivers. On Vista
    x64 and 7 x64 they are unsigned, so driver signature enforcement must be
    disabled (section 4).

xhci98.sys is a whole USB host controller driver. It runs the controller,
the root hub, every hub behind it and the splitting of composite devices
itself, and Windows' own drivers for keyboards, mice, storage, audio and
network adapters sit on top of it unchanged. It drives SuperSpeed (USB 3.x)
devices and hubs as well as High, Full and Low Speed ones. The second driver
beside it, xhciuas.sys, runs UAS (USB Attached SCSI) storage, which none of
these systems has a driver of its own for.


ISSUE REPORTING
------------------------------------------------------------------------------

This is a hobby driver for operating systems that left support two decades
ago, it has run on a small number of machines, and BUGS ARE NOT UNEXPECTED.

Please report what you find, on the project's GitHub page:

      https://github.com/yeokm1/xhci98/issues

The form there asks for what a report needs. Section 7 says what is known
already, so read that first.


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. Check the machine first (optional, but recommended)
  2. What you need
  3. The files Windows supplies, and USB storage on Windows 98 SE
  4. Install
  5. Using it
  6. If something goes wrong
  7. Known limitations
  8. What is in this directory
  9. Registry settings
 10. Release history


==============================================================================
 1. CHECK THE MACHINE FIRST        (optional, but recommended)
==============================================================================

You can skip to step 2 and simply try the driver. Nothing here is required,
and a machine that cannot run it fails visibly rather than dangerously.

It is recommended because not every xHCI controller can be driven, and ONE OF
THE WAYS IT CAN FAIL CANNOT BE FIXED IN SOFTWARE. Finding that out in thirty
seconds is cheaper than finding it out after installing an operating system.

The checker is in the XHCIQUAL\ subdirectory. Copy XHCIQUAL.EXE to a DOS boot
disk, and run ONE command:

      XHCIQUAL

That is it - no arguments. It is read-only: it takes ownership of nothing and
writes no PCI configuration register. It prints one of three verdicts:

  LOOKS QUALIFIED   nothing a read-only pass can see disqualifies this
                    machine. Go ahead and install.

  DISQUALIFIED      something it can see rules the machine out: no xHCI
                    controller, NO LEGACY INTERRUPT PIN, the controller's
                    memory window is unusable or sits above 4 GB, or there
                    are no USB 2.0 ports.

  CANNOT SAY        something it is not allowed to change is in the way -
                    the controller is powered down, or its memory access is
                    switched off. Check the BIOS and try again.

The tool takes options too, and you need none of them to answer the question
above. The whole command line is below so that a log someone asks you for can
be produced without guesswork; XHCIQUAL\readme.txt carries the same list with
the safety notes spelled out.

      XHCIQUAL                          the read-only quick scan, one screen
      XHCIQUAL [xhci|ehci|ohci|all] [options]
      XHCIQUAL --scan TYPE [--scan TYPE ...] [options]
      XHCIQUAL --help

  Options may be written in any order, before or after a family word.

  WHICH CONTROLLERS IT LOOKS AT. With no family word it looks at all three.
  The driver only cares about xHCI; the other two are there because a
  machine's other controllers are part of the picture when something does
  not add up.

      xhci | ehci | ohci     one family only
      all                    all three - the default
      --xhci --ehci --ohci   the same three selectors, written as options
      --scan TYPE            the same again; repeat it to combine families

  READ-ONLY MODES - these change nothing on the machine.

      --quick           the no-argument quick scan, asked for explicitly
      --probe-only      read-only discovery, fuller than --quick. It reads
                        the controller's memory window only if the firmware
                        has already switched it on, and switches nothing on
                        itself
      --no-active       another name for --probe-only

  MODIFIERS - these say what to do with the report, not what to run.
  ON THEIR OWN THEY DO NOT MAKE THE RUN READ-ONLY. Any argument at all
  turns off the no-argument quick scan, so XHCIQUAL --log FILE performs
  the FULL ACTIVE run below. Pair one with --quick or --probe-only when a
  read-only run is what you want.

      --no-page         do not stop at the end of each screenful
      --serial          mirror the output to COM1, 115200 8N1
      --log [FILE]      also write the report to a file, default
                        XHCIQUAL.LOG. A family word after --log is read as a
                        selector, so name the file explicitly if you pass
                        both
      --done-flag FILE  create FILE only if the run finishes normally, so a
                        batch file can tell a crash apart from a bad verdict
      --help, -h, /?    a longer help text, printed by the program itself

  ACTIVE OPTIONS - THESE TAKE OVER THE CONTROLLER, reset it, and reset its
  ports. They are development instrumentation; installing this driver never
  requires them, and XHCIQUAL\readme.txt carries the precautions they need.

      --full            the full active run, across all three families
      --poll-only       active bring-up with no interrupt handler installed.
                        The mildest of these, and the one to try first
      --irq-selftest    xHCI only: an isolated, one-shot interrupt test
      --set-intel-ports try to route the Intel USB2 ports to xHCI and read
                        the result back. This one writes PCI configuration
                        space
      --no-wait         do not wait 15 seconds for a device to be plugged in
      --no-devid        skip the xHCI device identification step

IF YOU ARE ASKED FOR LOGS, run these two in order. The first reads everything
the read-only path can see, writes nothing to the machine, and leaves
PROBE.LOG beside it:

      XHCIQUAL --probe-only --log PROBE.LOG

If that does not crash the machine, continue with the full run, which leaves
FULL.LOG. THIS ONE TAKES OVER THE CONTROLLER, resets it and resets its ports;
use a PS/2 keyboard, and do not write the log to a drive on the controller
being tested:

      XHCIQUAL --log FULL.LOG

Send FULL.LOG if the full run finished; only if it did not, send PROBE.LOG.

It returns 0 if the active tests passed, 1 if the machine is not qualified or
the run was read-only - which cannot pass tests it does not run, so 1 is the
normal result of every read-only command above - and 2 if the command line was
wrong or no controller of that kind is here. The verdict on screen is what to
read; those codes are for batch files.

>> IT MUST BE RUN FROM REAL DOS, NOT A DOS WINDOW INSIDE WINDOWS. <<

   Boot the machine to a plain MS-DOS or FreeDOS floppy, CD or USB key. Do
   not run it from a "MS-DOS Prompt" in Windows 98, and not from CMD.EXE on
   Windows 2000 or later. The tool talks to the controller directly and needs
   memory it can address one-to-one, which a DOS box inside Windows does not
   give it - there, the answers would be wrong or it would simply fail.
   For the same reason, boot without EMM386 or any other memory manager.

   HIMEM.SYS IS THE EXCEPTION, AND ON SOME MACHINES IT IS NEEDED. It is not
   a memory manager in the sense above - it does not put the processor into
   the mode that breaks this tool - and XHCIQUAL runs in 32-bit mode, so it
   needs the extended memory HIMEM provides. If the program will not run at
   all on a boot that loads nothing, add this one line to CONFIG.SYS and try
   again:

         DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V

   Use whatever path HIMEM.SYS is actually at - C:\WINDOWS\ on a Windows 98
   machine, the root of the disk on a boot floppy. The /V makes it say at
   boot whether it loaded.

   Two more things, only if you go on to run the deeper tests listed in
   XHCIQUAL\readme.txt: use a PS/2 keyboard, because a USB keyboard on the
   controller being tested can stop responding mid-run; and do not boot or
   write a log through that same controller.

A controller reporting no interrupt pin cannot be driven at all, on any of
these systems. There is no software workaround: the driver uses the legacy
interrupt pin and has no path for the newer mechanism (MSI) that such a
controller would require.

The checker's last criterion, "no USB 2.0 ports", dates from the earlier,
USB 2.0-only releases. This driver accepts a controller with no USB 2.0
port at all, built from the specification; no such controller has been
tested.


==============================================================================
 2. WHAT YOU NEED
==============================================================================

  Operating system   Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows
                     ME, 32-bit Windows XP (SP3), 32-bit Windows Vista (SP2)
                     and 32-bit Windows 7 (SP1) in virtual machines only.
                     Windows 2000 has never run on real hardware.
                     64-bit: Windows XP x64 (SP2), Windows Vista x64 (SP2)
                     or Windows 7 x64 (SP1), in
                     virtual machines only as well.

  On Windows 98 SE   Nothing for the controller, hubs, mice, keyboards and
                     audio: no USB 2.0 stack is needed. FOR USB STORAGE,
                     NUSB 3.3 or 3.6, or at least its mass-storage part
                     (section 3). SweetLow's USB 2.0 stack alone has no
                     storage part.

  On Windows ME      SweetLow's USB 2.0 stack: every Windows ME test ran
                     under it. Windows ME carries its own USB storage
                     files. DO NOT
                     install NUSB on Windows ME: it is a Windows 98 SE
                     package.

  On Windows 2000,   Nothing to install. DO NOT install NUSB on these.
  XP, Vista and 7

  Controller         xHCI, PCI class code 0C0330, a memory window below
                     4 GB, and a legacy interrupt pin.


==============================================================================
 3. THE FILES WINDOWS SUPPLIES, AND USB STORAGE ON WINDOWS 98 SE
==============================================================================

The package's own files are five, and they are in:

      RELEASE-X86\, DEBUG-X86\, RELEASE-X64\ and DEBUG-X64\

  xhci98.inf, xhci98.sys     the USB host controller driver
  xhciuas.inf, xhciuas.sys   the UAS storage driver
  txtsetup.oem               the driver description Windows 2000 and XP
                             Setup read when F6 is pressed (section 4)

Nothing else is in the package, and there is nothing to complete.

Two files the driver depends on are NOT in the package, because they are
Windows' own, unmodified, and no Microsoft file is in this download:

  usbd.sys     Helper routines the drivers above this one call. Without it
               the USB devices' own drivers do not load.

  usbui.dll    The USB property pages in Device Manager. Only cosmetic.

UP TO WINDOWS XP, WINDOWS ONLY INSTALLS ITS USB FILES WHEN SETUP FINDS A USB
CONTROLLER IT RECOGNISES, and on an xHCI-only machine it never does. The
install in step 4 therefore asks Windows to copy them from its own
installation source, each only if it is absent.

  WINDOWS 98 SE   HAVE THE WINDOWS 98 SE INSTALLATION CD AT HAND. Unless the
                  Windows CABs are on the hard disk (C:\WINDOWS\OPTIONS\CABS,
                  as on OEM and Windows 98 QuickInstall installs), the
                  install shows "Insert Disk" asking for the Windows 98
                  Second Edition CD-ROM: insert it and click OK, and if it
                  then asks where to copy from, give it the CD's WIN98
                  folder. It is asking for usbd.sys, not for anything of
                  this driver's. The first mouse or keyboard can ask again,
                  for Windows' own hidclass.sys; give it the same.

  WINDOWS ME      The same, with the Windows ME CD, or the CABs its Setup
                  left on the hard disk.

  WINDOWS 2000    Nothing to do: both come from the driver cache every
  AND XP          installation has, with no prompt.
  WINDOWS XP x64  Nothing to do either: both come from Driver Cache\amd64.
                  The machine tried (a virtual one, which had never had a
                  USB controller) asked for no file.

  WINDOWS VISTA   Nothing to do: every installation already has both.
  AND 7

USB STORAGE ON WINDOWS 98 SE NEEDS NUSB'S MASS-STORAGE PART
...........................................................

Windows 98 SE has no USB storage driver, and a drive letter there comes from
a layer this package does not replace. It needs five files, which NUSB 3.3
and NUSB 3.6 install:

      USBSTOR.INF, USBSTOR.SYS     the Bulk-Only storage driver
      USBNTMAP.INF, USBNTMAP.SYS   what gives a USB disk a drive letter
      USBMPHLP.PDR                 the mapping port driver

They are Microsoft's, and this package does not carry them.

  WITHOUT THEM a USB stick has no driver at all ("Unknown Device", Code 28),
  and a UAS disk installs and then sits at Code 2 ("The NTKERN.VXD device
  loader(s) for this device could not load the device driver"). A restart
  does not change that.

  WITH NUSB 3.3 OR 3.6 INSTALLED, storage works.

  THE FIVE FILES CAN ALSO BE INSTALLED ON THEIR OWN, without the rest of
  NUSB: point the Add New Hardware Wizard, or Update Driver on the device,
  at a folder holding them. ON THE FIRST STORAGE DEVICE, ONE STEP IS NOT
  OBVIOUS. Windows gives the new disk its own generic "Disk drive" before it
  has seen USBNTMAP.INF, and no drive letter appears. Then:

    1. Device Manager -> Disk drives -> "Disk drive" -> Driver -> Update
       Driver, pointed at the same folder. It installs "USB Disk".
    2. Unplug the device and plug it back in.

  After that every storage device, USB stick or UAS disk, works with no
  extra step.

On Windows ME, Windows' own storage files serve USB sticks: they are copied
from the Windows ME CABs when the first stick installs. UAS disks need the
same files: a UAS disk plugged in before any ordinary stick shows Code 2.
Plug in any ordinary USB stick once, then unplug the UAS disk and plug it
back in.


==============================================================================
 4. INSTALL
==============================================================================

INSTALL FROM THE RELEASE DIRECTORY:

      RELEASE-X86 or RELEASE-X64\

This package carries BOTH builds side by side, RELEASE and DEBUG, each a
complete set of files with the same names, so the directory you point
Windows at is what decides which drivers you get. RELEASE is the one you
want. DEBUG (DEBUG-X86 or DEBUG-X64\) is the same drivers built
so that a crash on them can be traced further back. It records nothing more
than RELEASE does, and it is there only for troubleshooting a machine that
has already gone wrong. It prints nothing as it runs. Section 8 describes
both, and nothing about a copied file says which one it is - so point at a
directory, never at a loose .sys file.

There are two of each, one per architecture: the -X86 directories are for
32-bit Windows and the -X64 ones for 64-bit Windows. If you pick the wrong
one nothing breaks - Windows finds no driver in it and says so - so try the
other.

Put the whole unzipped package somewhere the machine can read - a floppy, a
CD, a shared folder - then:

  WINDOWS 98 SE
      If you want USB storage, install NUSB 3.3 or 3.6 first (section 3).
      Then open Device Manager and find the unrecognised xHCI controller:
      it sits unclaimed with a yellow mark, usually under "Other devices".
      Then
          Properties -> Driver -> Update Driver -> Specify a location
      and point it at the RELEASE-X86\ directory. "Insert Disk" may ask for
      the Windows 98 Second Edition CD-ROM (section 3). Restart when asked.

      (If Windows finds the controller for you first, the Add New Hardware
      Wizard asks the same question - give it the same directory.)

  WINDOWS ME
      SweetLow's USB 2.0 stack first (section 2). Then the same Device
      Manager route as Windows 98 SE, pointed at the RELEASE-X86\
      directory, and restart when asked. Windows ME has only been run in a
      virtual machine.

  WINDOWS 2000 SP4 AND WINDOWS XP (32-BIT)
      Open Device Manager and find the unrecognised xHCI controller, then
          Properties -> Driver -> Update Driver -> Have Disk
      and point it at the RELEASE-X86\ directory. On Windows XP choose
      "Continue Anyway" at the unsigned-driver warning. Nothing else is
      asked for. Windows XP has only been run in a virtual machine.

  WINDOWS VISTA AND WINDOWS 7 (32-BIT)
      Open Device Manager and find the unrecognised xHCI controller, then
          Update Driver Software -> Browse my computer for driver software
      and point it at the RELEASE-X86\ directory. If Windows warns that it
      cannot verify the publisher, install the driver anyway. Use Device
      Manager, not a right-click on the INF. Both have only been run in
      virtual machines.

  64-BIT WINDOWS
      The same route as the 32-bit edition of the same Windows, pointed at
      RELEASE-X64\. On Windows Vista x64 and Windows 7 x64 the install works
      on an ordinary start, but the drivers run only while driver signature
      enforcement is disabled, at every start, as they are unsigned;
      otherwise the controller shows Code 39 and nothing on it works.
      Windows XP x64 needs none of that.

It installs as "xHCI98 USB 3.x eXtensible Host Controller", with "xHCI98
USB 3.x Root Hub" underneath it. Neither should carry a warning mark.

THE UAS DRIVER installs the first time a UAS disk is plugged in: the Found
New Hardware wizard asks for a driver for "xHCI98 USB Attached SCSI
Storage". Point it at the same directory. No restart is needed.

WINDOWS 2000 OR XP SETUP, OR THE RECOVERY CONSOLE, ON AN xHCI-ONLY MACHINE
..........................................................................

When the keyboard or the install medium is on the xHCI controller, Setup
can load this driver at its F6 prompt. Copy the files of RELEASE-X86\ (or
RELEASE-X64\ for Windows XP x64) to the root of a floppy disk - it holds
txtsetup.oem, xhci98.sys and xhci98.inf, which is all F6 reads. Start
Setup, press F6 when "Press F6 if you need to install a third party SCSI
or RAID driver" shows, press S at the next screen, insert the floppy, and
pick "xHCI98 USB 3.x Host Controller". Setup then uses its own keyboard,
mouse and USB storage drivers above it.

The same floppy serves the RECOVERY CONSOLE of Windows 2000 and XP: press
F6 as for an install, then R at Setup's Welcome screen (on Windows 2000,
then C for the console). The USB keyboard logs in and types commands. The
console runs in text mode throughout, so the Windows XP restriction below
does not touch it.

  - Pressing F6 itself needs the firmware's own USB keyboard support.
  - The floppy must be drive A: as the firmware sees it.
  - A USB disk that supports UAS (most USB 3 enclosures and SSDs) is
    usually not usable during Setup, even if it also supports the older
    Bulk-Only mode. Most USB flash sticks are Bulk-Only only and always
    work; use one of those.
  - Installing Windows ONTO a USB disk is not supported.
  - A USB DRIVE PRESENT AT THE PARTITION SCREEN TAKES THE LETTER C:.
    Unplug the USB drives you do not need, or Windows installs to the next
    letter.
  - On Windows 2000, plug the USB keyboard and the USB stick in BEFORE
    Setup starts. Its text-mode Setup uses only the USB devices present
    when the driver first reports them; one plugged in later stays unused
    there. The driver waits up to 5 seconds for them (section 9,
    XhciFirstEnumWaitMs).
  - Later in Setup, Windows installs the driver again from xhci98.inf and
    may ask for the floppy or the Windows CD.
  - INSTALLING WINDOWS XP NEEDS A PS/2 KEYBOARD, OR A LAPTOP'S BUILT-IN
    ONE, LATER IN SETUP. Setup copies Windows' own HID and USB helper files
    only with Microsoft's own USB controller drivers, so the USB keyboard
    and mouse do nothing until the "Installing Devices" step installs them
    from the CD - and that step first asks about this unsigned driver,
    default No. A machine whose only keyboard is USB cannot answer, and
    Setup stops. A laptop's built-in keyboard (connected inside as PS/2) or
    any PS/2 keyboard answers it; afterwards the USB keyboard and mouse
    work. This package cannot carry those files. Windows 2000 does not ask.

  What has run, in virtual machines only: text mode with a USB keyboard
  and a USB stick on Windows 2000 and XP, and the Recovery Console on both,
  logged in and running commands with the USB keyboard alone, on this
  release's code; and Windows XP and XP x64 installed to the desktop this
  way on the build before it. Nothing of this has run on real hardware. A
  repair install, Windows 2000's Emergency Repair Disk and Windows XP's
  Automated System Recovery were not tried.

UPDATING FROM AN EARLIER 2.x RELEASE
....................................

Install over it with Update Driver on "xHCI98 USB 3.x eXtensible Host
Controller", pointed at the same directory as a new install. Nothing needs
renaming first. As read in virtual machines:

  WINDOWS 98 SE AND ME: RESTART AFTERWARDS, ALTHOUGH WINDOWS DOES NOT ASK.
  The new file waits to replace the old one at the next start, and until
  then the earlier release keeps running. Under SweetLow's stack the
  controller showed a problem for a minute or two after Finish; on ME it
  shows one until the restart, while the devices keep working.

  WINDOWS 2000: USE HAVE DISK. Letting Windows search answers that a
  suitable driver is already installed and keeps the earlier release. Use
  "Display a list of the known drivers" -> Have Disk, as below.

  WINDOWS XP, XP X64, VISTA AND 7: the update took effect at once, with no
  restart. (Read with a command-line driver update rather than Device
  Manager.)

  THE ROOT HUB'S DRIVER TAB STILL SHOWS THE EARLIER VERSION (on 98 SE and
  ME, its date), although it runs the new file. To change it, run Update
  Driver on "xHCI98 USB 3.x Root Hub" too: on Vista and 7, "Let me pick
  from a list of device drivers on my computer" and the new entry;
  elsewhere Have Disk.

  Afterwards each device is found once more as new hardware, exactly once:
  a stick under its serial number, the other devices under a new id. On
  the NT systems this needs no answer; on Windows 98 SE the wizard runs for
  each and may ask for the CD for hidclass.sys. Let Windows install them.
  A hidusbf setting on such a device has to be applied again (section 5).

UPGRADING FROM THE EARLIER, USB 2.0-ONLY RELEASES (1.x)
.......................................................

This driver replaces the 1.x driver, which has the same file name,
xhci98.sys. Going back is a reinstall of a 1.x package from its own
download.

  On every system, update the "USB 2.0 eXtensible Host Controller
  (xhci98)" entry in Device Manager and pick the driver from a list with
  Have Disk. Do not let Windows search: it can reinstall the old driver
  from its own copy.

  WINDOWS 98 SE WITH NUSB: DO NOT UPDATE WHILE THE 1.x DRIVER IS RUNNING.
  NUSB's usbport.sys crashes the machine with a blue screen as it stops the
  old driver. Instead:
    1. Open an MS-DOS Prompt and type
           ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
    2. Shut the machine down and switch it on again. The controller now
       shows a yellow mark.
    3. Device Manager -> the controller -> Update Driver -> "Display a list
       of all the drivers in a specific location" -> Have Disk -> the
       RELEASE-X86\ directory.
    4. Pick "xHCI98 USB 3.x eXtensible Host Controller", give it the
       Windows 98 SE CD when it asks for usbd.sys, and restart when asked.
    5. Each USB device is found once more as new hardware.
  If you already updated in place and got the blue screen, restart: this
  driver comes up on its own.

  On every system each USB device is a new entry in Device Manager after
  the upgrade, so a hidusbf polling rate set under 1.x has to be set again
  (section 5).

  WINDOWS 98 SE WITH SWEETLOW'S STACK, AND WINDOWS ME: the same Update
  Driver route, in place. Windows does not ask you to restart, but you
  must: shut down and switch on again straight away. Until then USB
  devices stop working. (Windows ME has not been tested as an upgrade.)

  WINDOWS 2000: Update Driver -> "Display a list of the known drivers" ->
  Have Disk. Pick "xHCI98 USB 3.x eXtensible Host Controller", the first
  of three. It starts at once; at the next restart Windows may ask for
  one more.

  WINDOWS XP: Update Driver -> "Install from a list or specific location"
  -> "Don't search. I will choose the driver to install" -> Have Disk ->
  Continue Anyway. A second wizard follows for the root hub. No restart.

  WINDOWS VISTA AND 7: Update Driver Software -> "Browse my computer" ->
  "Let me pick from a list of device drivers on my computer" -> Have
  Disk. Typing the folder into the search box keeps the old driver.


==============================================================================
 5. USING IT
==============================================================================

Plug devices in and they are found and installed the usual way. Keyboards,
mice, flash drives, USB disks, USB Ethernet adapters, audio devices and hubs
all work through the system's own drivers.

Things specific to this driver, worth knowing in advance:

  * USB DISKS GET UAS WHERE THEY OFFER IT. A disk that can do UAS gets
    xhciuas.sys; anything else gets Windows' own Bulk-Only storage driver,
    at whatever speed it connects. A disk that offers both can be kept on
    Bulk-Only (XhciForceBulkOnly, section 9).

  * DEVICE MANAGER SHOWS A SUPERSPEED DEVICE AS HIGH SPEED AT MOST. The
    interface it reads on these systems predates SuperSpeed, so that display
    says nothing about the real link. XHCISNAP's report (section 6) shows the
    speed the driver actually uses.

  * HUBS HAVE ENTRIES OF THEIR OWN IN DEVICE MANAGER. A hub appears as
    "xHCI98 USB Hub", with the devices behind it beneath it. A USB 3 hub
    appears twice, the second time as "xHCI98 USB 3.x Hub" for its
    SuperSpeed half. Each hub has a Power tab. A hub installs from the
    driver already installed: on Windows 98 SE and 2000 with nothing to
    answer, on Windows XP with the Found New Hardware wizard and the
    unsigned-driver warning (Continue Anyway) for each newly plugged hub.
    Disabling and enabling a hub in Device Manager brings back the devices
    behind it (read on 98 SE, 2000, XP). The driver still runs every hub
    itself.

  * A DEVICE WITH A SERIAL NUMBER KEEPS ITS ENTRY ON ANY PORT. Moved to
    another port or behind a hub, it is not found again as new hardware.
    A device without a serial number is known by its port, as under
    Microsoft's own hub driver, and moved elsewhere it is found again.

  * DEVICES NO WINDOWS INF NAMES ARE LISTED UNDER THEIR OWN PRODUCT NAMES,
    in the Add New Hardware wizard and in Device Manager, instead of "USB
    Device". Where one of Windows' own INFs names a device (a mouse, a
    keyboard, a USB stick), that name shows, as over Microsoft's own
    stack. On Windows 98 SE and ME a character outside plain ASCII shows
    as '?'.

  * SWEETLOW'S HIDUSBF sets a mouse's polling rate, and this driver
    programs the rate it sets, up to 1000 Hz for a Low- or Full-Speed
    device (more only with XhciFastPollFsLs, section 9). Read in virtual
    machines at a root port and behind a hub on Windows 98 SE under NUSB
    and on ME, and at a root port on XP and on a stock Windows 98 SE. It
    works behind a hub on XP too (read). Its setting is
    kept on the device's Device Manager entry, so it has to be applied
    again whenever the device is found as new hardware.

  * IDLE DEVICES ARE NEVER PUT TO SLEEP. The driver never starts selective
    suspend, of a device or of a hub port, so an idle device draws its
    normal power. There is no switch for it.

  UPDATING xhciuas.sys OVER AN OLDER COPY
  .......................................

  When the new xhciuas.sys carries the same version as the one installed,
  "Search for a better driver" keeps the copy Windows already has. Instead:
  Update Driver on "xHCI98 USB Attached SCSI Storage" -> "Display a list of
  all the drivers in a specific location" -> Have Disk -> this package's
  directory.


==============================================================================
 6. IF SOMETHING GOES WRONG
==============================================================================

  REPORT IT FIRST. XHCISNAP IS ONLY FOR WHEN THE MAINTAINER ASKS FOR IT
  .....................................................................

  Open an issue on the project's GitHub page (section 7) and describe what
  happened. DO NOT RUN XHCISNAP UNLESS THE MAINTAINER ASKS YOU TO. Installing
  and using the driver never needs it, and its first step changes one of the
  driver's settings, so it is not something to try on your own.

  XHCISNAP.EXE is in the XHCISNAP directory of this package. It reads the
  driver's own log straight out of the running machine and writes a report
  you can attach to that issue. On Windows 98 and Windows ME it is the ONLY
  way to get anything out. When you are asked, it is four steps, and none of
  them is REGEDIT:

      1. XHCISNAP -verbosity 2
      2. restart the machine
      3. make the problem happen again
      4. XHCISNAP -o C:\MYDUMP

  Then send C:\MYDUMP.TXT. Attach C:\MYDUMP.BIN as well if you are asked
  for it. Step 1 finds the right registry key for you, on every controller
  this driver runs - see section 9 for what it sets.

  STEP 2 IS NOT OPTIONAL. The driver reads that setting when it starts.
  Without the restart the driver is still at whatever it read last time -
  which on a fresh install is OFF, and then XHCISNAP gets no answer at all
  rather than an empty one.

  If nothing comes back at all, run XHCISNAP -probe. It checks the route to
  the driver separately from whether this driver answers on it.

  USE THE XHCISNAP.EXE FROM THIS PACKAGE, not a copy kept from an earlier
  release. This driver's report is snapshot schema 5.

  XHCISNAP.EXE changes nothing about how the driver behaves on the bus, and
  writes no file it was not asked to. It does READ the controller's port
  registers, which is a hardware access - it just does not write one.

  What step 1 DOES change is ONE of this driver's own settings, and it says
  so as it writes it - that is the point of it, and it is why step 2 is a
  restart. XHCISNAP -disable puts it back, and you should run that once you
  have sent the capture: while it is on, anyone using this machine can read
  the driver's diagnostic state. See section 9.

  WHAT THE LOG CAN AND CANNOT ANSWER

  It answers "the device does not work" and "transfers are wrong". It answers
  NOTHING ABOUT A CRASH - a machine that has crashed is not running for
  anything to read.

  THE DRIVER WRITES NO LOG FILE ITSELF, and there is no registry value that
  makes it. XHCISNAP writes the file, and you name it on the command line.

  DEBUGVIEW (Sysinternals), with "Capture Kernel" switched on, receives this
  driver's log as it runs if XhciLogDebugView is set. LIKE XHCISNAP, USE IT
  ONLY WHEN THE MAINTAINER ASKS FOR IT.

      !! Do not run DebugView on Windows 98 on real hardware while
         capturing. Under the earlier releases, plugging in a device while
         it captured crashed the machine, and this driver has not been
         run under DebugView on Windows 98 hardware. Inside a virtual
         machine it is fine. You do not need DebugView to send a report.


==============================================================================
 7. KNOWN LIMITATIONS
==============================================================================

The full measured list is in the project's docs/using/release-notes.md,
together with what has and has not been tested on real hardware as opposed
to in a virtual machine. Read it before reporting a problem, and then report
it anyway if it is not there - the reports are what fix it:

      https://github.com/yeokm1/xhci98/issues

Outside this driver's control - these come from Windows, NUSB or the driver
being unsigned, and no change to this driver can remove them:

  * UPGRADING IN PLACE OVER A RUNNING 1.X DRIVER UNDER NUSB BLUE-SCREENS.
    NUSB's usbport.sys crashes the machine as it stops the old driver,
    before this release runs. Follow section 4, which avoids it.

  * WINDOWS VISTA X64 AND 7 X64 NEED DRIVER SIGNATURE ENFORCEMENT DISABLED.
    The driver is not signed. Driver signature enforcement must be disabled
    at every start, or the controller sits at Code 39.

  * NO USB STORAGE ON A STOCK WINDOWS 98 SE (section 3). With no NUSB
    installed there is no mass-storage driver at all. HID and audio still
    work.

  * WINDOWS ME: DO NOT UNPLUG A DEVICE WHILE WINDOWS IS INSTALLING IT. ME's
    own device manager stops responding; it does the same on Microsoft's
    own USB stack. Wait for the install to finish before unplugging.

  * A SUPERSPEED DEVICE'S POWER READS A QUARTER OF ITS DRAW on the Power
    tab: the page doubles a value that is in 8 mA units at SuperSpeed.

  * WINDOWS XP FROM THE F6 FLOPPY: GUI-MODE SETUP ASKS ABOUT THE UNSIGNED
    DRIVER BEFORE THE USB KEYBOARD WORKS. A PS/2 or built-in laptop
    keyboard answers it (section 4).

May be addressed in a later release:

  * THE DRIVER NEVER STARTS SELECTIVE SUSPEND (section 5). Idle devices and
    hub ports are never suspended to save power. A suspend or resume a hub
    reports is handled.

  * USB STORAGE ON WINDOWS 98 IS SLOWER THAN THE DRIVE. An observation, not
    a defect found: Windows 98 sends one command at a time. On a ThinkPad
    P14s Gen 1 with an MSSU10 drive at 64 KB, about 208 MB/s on Windows 98
    against 277 MB/s on Windows 11 at the same queue depth of one. This may
    be looked into in a later release.

  * WINDOWS ME: A UAS DRIVE AS THE FIRST USB STORAGE DEVICE SHOWS CODE 2.
    ME has not yet copied its own USBNTMAP.SYS and USBMPHLP.PDR, which it
    installs only when its first ordinary USB stick is plugged in. Plug in
    any ordinary USB stick once, then unplug the UAS drive and plug it back
    in. No Remove and no restart are needed.

  * WINDOWS VISTA AND 7: THE CONTROLLER'S ADVANCED TAB SHOWS NO BANDWIDTH.
    The figure comes from a query this driver does not answer.

  * WINDOWS 98 SE AND ME SHOW A DEVICE NAME'S NON-ASCII CHARACTERS AS '?'.

Untested ground:

  * SUPERSPEED ISOCHRONOUS TRANSFERS. Built from the specification. No
    SuperSpeed isochronous device has been held and QEMU models none.

  * SUPERSPEEDPLUS (USB 3.1 GEN 2, USB 3.2 GEN 1X2 AND GEN 2X2). Accepted at
    its trained rate, built from the specification. No Gen 2 device has been
    tested, so every mode is untested.

  * A UAS-ONLY DRIVE AT SUPERSPEED ON A CONTROLLER WITHOUT STREAMS. It is
    sent back to its USB 2.0 port and runs UAS at High Speed, or is refused
    if it has no USB 2.0 port. Built from the specification; no such
    controller has been held.

  * A USB 3 HUB'S SECOND ENTRY, "xHCI98 USB 3.x Hub", AND A HIGH-SPEED
    HUB'S OWN ENTRY. Hub entries were read in virtual machines on QEMU's
    USB 1.1 Full-Speed hub only; no virtual machine models a SuperSpeed or
    a High-Speed hub, and neither entry was read on real hardware.

  * POLLING ABOVE 1000 HZ (XhciFastPollFsLs, section 9). Outside the xHCI
    specification, and read on no real controller and in no virtual
    machine.


==============================================================================
 8. WHAT IS IN THIS DIRECTORY
==============================================================================

  RELEASE-X86\  - INSTALL THIS ONE, on 32-bit Windows

  The normal driver. This is the one you want.

      xhci98.inf
      xhci98.sys   168,955 bytes
      SHA-256
      079296AB5B69664D7418AC96C563F6ABD6F809B8BCE6AAC9F8CD20C47E662AF7
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  24,944 bytes
      SHA-256
      B40B064EB65A07669EA250BF293F3B55182BAB4831950D9F6E0EAC3D60C186E0
      txtsetup.oem (for Windows 2000 and XP Setup's F6 prompt)

  DEBUG-X86\  - only when diagnosing a problem

  The same driver, built so that a crash on it can be traced
  further back. It records nothing more than RELEASE-X86\ does, and
  it prints nothing as it runs. It is here only so that it can be
  installed at this exact version if something goes wrong. Do not
  install it otherwise - and note that BOTH builds answer
  XHCISNAP, so you do not need this one to send a report.

      xhci98.inf
      xhci98.sys   172,267 bytes
      SHA-256
      AE332A91866FDD32BAA29EA84C3DF3C64F39EEFAA474CFE26F014A544628D9A1
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  25,104 bytes
      SHA-256
      5038339CCD7DDC1D2F4471A569F3B4A09C3CB574C83A1EB9DF96CCC7DA4CBA7A
      txtsetup.oem (for Windows 2000 and XP Setup's F6 prompt)

  RELEASE-X64\  - INSTALL THIS ONE, on 64-bit Windows

  The normal driver. This is the one you want.

      xhci98.inf
      xhci98.sys   197,632 bytes
      SHA-256
      70D3465FD6BFDE3462ABD14A1B909734BAC74372D1153D71E91EB22638218A17
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  33,280 bytes
      SHA-256
      88B3019EDC74A8B4EF9F2EE30151F2CEA9C78E7A09FDB33F17634D450A772A63
      txtsetup.oem (for Windows 2000 and XP Setup's F6 prompt)

  DEBUG-X64\  - only when diagnosing a problem

  The same driver, built so that a crash on it can be traced
  further back. It records nothing more than RELEASE-X64\ does, and
  it prints nothing as it runs. It is here only so that it can be
  installed at this exact version if something goes wrong. Do not
  install it otherwise - and note that BOTH builds answer
  XHCISNAP, so you do not need this one to send a report.

      xhci98.inf
      xhci98.sys   344,576 bytes
      SHA-256
      66233DAB45ADEDEB7F77033A4A4D7BAD815F637C7142E51ED71CA91EF4AD4D5E
      xhciuas.inf  (the UAS storage class driver's INF)
      xhciuas.sys  50,176 bytes
      SHA-256
      384005C5B74317A858425D0E85737BB71F9E678BC37A9080C013F9B43422BE66
      txtsetup.oem (for Windows 2000 and XP Setup's F6 prompt)

  Which pair: the -X86 directories are for 32-bit Windows and the
  -X64 ones for 64-bit Windows. If you pick the wrong one nothing
  breaks - Windows simply finds no driver in it and says so - so
  try the other.

  XHCIQUAL\  - the DOS machine checker from step 1

      XHCIQUAL.EXE 117,972 bytes
      XHCIQUAL.MAP keep it beside the EXE; see xhciqual\readme.txt
      readme.txt
      NOTICE.TXT   third-party notices this EXE carries

  XHCISNAP\  - what to run if something goes wrong

      XHCISNAP.EXE 81,920 bytes
      readme.txt
      NOTICE.TXT   third-party notices this EXE carries

      It reads the driver's own log off the running
      machine and writes a report you can paste into a
      bug report. On Windows 98 it is the only way to
      get anything out at all. Four steps, and none of
      them is regedit:

        XHCISNAP -verbosity 2
        restart the machine
        make the problem happen again
        XHCISNAP -o C:\MYDUMP

      Then send C:\MYDUMP.TXT, and attach
      C:\MYDUMP.BIN if you are asked for it.

  LICENSE

  The GNU GPL v2 this driver is published under, with the note on
  what in the wider project is third-party material and is NOT
  covered by it. The LICENCE section at the end points here.

Every driver binary in this download is called xhci98.sys or xhciuas.sys
and every one carries driver version 2.1.0.0, so a copy taken out of its
directory cannot be identified by name or by version.
Nothing you can see in Explorer tells the two architectures apart: the "debug"
flag on the Version tab separates RELEASE from DEBUG, and there is no
equivalent for 32-bit against 64-bit. (The architecture IS in the file, in
the PE header, but reading it takes a tool.) So the directory a copy came out
of is what identifies it.

(In Windows driver-kit terms, RELEASE is what the DDK calls a "free" build
and DEBUG is what it calls a "checked" build. This project says release and
debug throughout, in its build scripts and its documentation alike.)


==============================================================================
 9. REGISTRY SETTINGS
==============================================================================

Every registry value this driver reads. There are seven, all DWORDs. The
install writes one of them, XhciImodInterval250ns; the other six are absent
until you set them, and absent means the default each one states.

  YOU SHOULD NOT NEED THIS SECTION FOR A LOG. If the maintainer asks for one,
  XHCISNAP -verbosity 2 sets the value that matters, on every controller, and
  finds the key itself.

  XhciLogVerbosity  -  the whole log switch
  .........................................

  Default 0. Level 0 is off outright; above it each level is the one below
  plus one thing:

      0   OFF. The driver does not answer XHCISNAP at all. This is what a
          fresh install does.
      1   the channel, plus the counters. The log of what happened is still
          off, so this is the cheapest reading there is.
      2   plus the log of what happened.  USE THIS ONE.
      3   plus the USB port register table.  Still no internal addresses.
      4   plus everything, including internal addresses. Only if asked -
          it is more than you would want to paste in public.

  A value outside 0-4 is REFUSED rather than treated as the nearest one: the
  driver falls back to 0, which is off.

  XhciLogDebugView  -  the log to a capture tool
  ..............................................

  Default 0. Set it to 1 to have the log handed to DebugView as the driver
  runs, and once more when the controller stops. It does not affect what
  XHCISNAP reads. Leave it at 0 unless the maintainer asks for a DebugView
  capture, and see section 6 about DebugView on Windows 98.

  XhciImodInterval250ns  -  how long the controller holds back an interrupt
  .........................................................................

  Counted in UNITS OF 250 NANOSECONDS. It sets how long the controller
  waits after one interrupt before raising the next. A shorter interval
  makes USB storage faster at the cost of more interrupts.

      160    written by the install: 40 microseconds, at most 25,000
             interrupts a second. The value Linux uses.
      4000   used when the value is MISSING, UNREADABLE, OR OUTSIDE
             10-4000: 1 ms, at most 1,000 a second.
      10     the lowest accepted: 2.5 microseconds, at most 400,000 a
             second.

  A value outside 10-4000 is REPLACED BY 4000, not rounded to the nearest
  limit, so a mistyped 0 cannot turn moderation off. 4000 is the
  controller's own power-on value.

  On a ThinkPad P14s Gen 1 under Windows 98 SE, a UAS flash drive at
  SuperSpeed read about 221 MB/s and wrote about 211 MB/s at 8 MB transfers
  at 160, against about 181 MB/s at 500, the value the earlier releases
  wrote. 40 added only 1 to 3% more.

  FEEL FREE TO TUNE IT. Raise it towards 4000 (or delete it) if you get
  audio stutter or instability under load; on real hardware under Windows
  98 SE, Full-Speed audio played without stutter at 160 while a drive was
  read at full speed. Enter it as a decimal DWORD. The driver
  reads it when it starts, so a change takes effect after a restart;
  XHCISNAP's report then shows under "registry values" the value it read,
  the interval in force, and what the controller took.

  XhciForceBulkOnly  -  keep storage on Bulk-Only instead of UAS
  ..............................................................

  Default 0 (absent): a disk that offers UAS gets UAS. Set it to 1 and every
  disk on that controller that offers BOTH transports gets Bulk-Only,
  Windows' own storage driver, instead. A UAS-only disk stays on UAS
  whatever this says.

  It is read each time a device is plugged in, so unplug the disk and plug
  it back in after changing it. On Windows 2000 and later a disk already
  installed keeps its driver until you uninstall it in Device Manager and
  plug it back in.

  XhciFastPollFsLs  -  Low- and Full-Speed polling above 1000 Hz
  ..............................................................

  Default 0 (absent): off. For a mouse on a ROOT PORT that hidusbf has set
  to its "31 Hz" or "62 Hz" rate:

      2      "31 Hz" becomes 2000 Hz, "62 Hz" becomes 4000 Hz
      3      "31 Hz" becomes 4000 Hz, "62 Hz" becomes 8000 Hz

  Any other value is off. A device behind a hub keeps its normal rate.

  THIS IS OUTSIDE THE xHCI SPECIFICATION, which sets 1 ms as the shortest
  interval for these devices. A controller that refuses it is caught: the
  device runs at its normal rate, and XHCISNAP's report counts it as
  fastpoll.fallbacks. A controller that accepts it and then misbehaves
  cannot be caught; if anything misbehaves, delete the value and restart.
  While it is set, ANY Low- or Full-Speed device on a root port that asks
  for 16 to 63 ms is polled faster too. Read when the controller starts, so
  restart after changing it. It has been read on no real controller and
  in no virtual machine yet.

  XhciFirstEnumWaitMs, XhciFirstEnumPortMs  -  the first report's wait
  ....................................................................

  When the root hub or a hub first reports its devices after it starts,
  the driver waits for the devices already plugged in to be ready, so
  that they are in that first report (Windows 2000's Setup uses only
  those, section 4). The wait ends as soon as they are ready.

      XhciFirstEnumWaitMs   the longest wait, in milliseconds. Default
                            5000; 0 turns the wait off; above 30000 is
                            held to 30000.
      XhciFirstEnumPortMs   the longest one port may hold it. Default
                            2000, held to the total; a slower device is
                            reported later instead. 0 sets no limit
                            per port.

  In virtual machines, with the defaults, the first report went 20 to 30
  ms after the start with nothing plugged in, and 0.3 to 0.9 s after it
  with a mouse and a stick plugged in.

  THOSE SEVEN ARE THE WHOLE LIST. The earlier releases' XhciVirtualHSHub,
  XhciVirtualHSHubVid and XhciVirtualHSHubPid are not read: a copy left in
  the key by an earlier install has no effect, because this driver reports
  every device at its true speed with no virtual hub in the way. Delete
  them if you want them gone.

  WHERE THE KEY IS. The values live in the controller's own driver key,
  which is spelled one way on the NT systems and another on the 9x ones:

    Windows 2000, XP, Vista and 7
      HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\
        {36FC9E60-C465-11CF-8056-444553540000}\0002

    Windows 98 SE and Windows ME
      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0002

  THE LAST PART OF THE PATH IS ASSIGNED BY THE MACHINE AND WILL NOT
  NECESSARILY BE 0002 ON YOURS. That is also why no ready-made .REG file
  ships here.

  DO NOT IDENTIFY THAT KEY BY ITS DESCRIPTION, AND DO NOT ASSUME THERE IS
  ONLY ONE. If the controller has ever been enumerated at more than one PCI
  slot, there is one such key per slot, all carrying this driver's name.
  Values typed into a stale one are read by nothing. Ask the device itself
  which key it uses. Find your controller under

    Windows 98    HKEY_LOCAL_MACHINE\Enum\PCI
    Windows 2000  HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\PCI
    and later

  open the subkey for the slot it occupies, and read its Driver value. It
  names the key to edit - for example USB\0004.

  SET THE TWO LOG VALUES ONLY WHILE DIAGNOSING SOMETHING, AND RUN
  XHCISNAP -DISABLE WHEN YOU HAVE SENT THE CAPTURE. While the channel is
  enabled, anyone using this machine can read the driver's own diagnostic
  state through it - counters, the log, the port table, and at level 4
  internal addresses. It is this driver's own state and nothing else: no
  documents, no passwords, no other program's memory. At level 0 the driver
  does not answer at all, so the value you set IS the lock. On Windows 2000
  and later you need administrator rights to set it (on Vista and 7, a
  Command Prompt started with "Run as administrator").


==============================================================================
 10. RELEASE HISTORY
==============================================================================

  2.1.0.0 - 2026-10-05

  The first update of the host controller driver, a minor version because it
  adds features: external hubs in Device Manager, devices known by their
  serial numbers and named by their product names, a floppy for Windows 2000
  and XP Setup and their Recovery Console, and an opt-in for polling Low- and
  Full-Speed mice above 1000 Hz. It also fixes the Windows ME controller
  re-enable and the root hub's Power page. Every system 2.0.0.0 supports is
  supported, from the same four directories, each now holding txtsetup.oem
  beside the two drivers. Its changes were read in QEMU virtual machines, on
  the 2.1.0.0 code before the cut, and on no real hardware; the release notes
  say what was read on which system.

  What changed

    * External hubs appear in Device Manager as entries of their own, "xHCI98
      USB Hub", with the devices behind them beneath them, as on Microsoft's
      own USB stacks; a USB 3 hub appears twice, its SuperSpeed half as
      "xHCI98 USB 3.x Hub". Each hub has a Power page, and USBView and Device
      Manager's view by connection can walk into it. A hub installs from the
      driver already installed: on Windows 98 SE and 2000 with nothing to
      answer, on Windows XP with the Found New Hardware wizard and the
      unsigned-driver warning for each newly plugged hub. Disabling and
      enabling an external hub in Device Manager brings back the devices
      behind it (read on 98 SE, 2000 and XP). The driver still runs every hub
      itself. Read on QEMU's USB 1.1 Full-Speed hub only: a High-Speed hub's
      entry and the "xHCI98 USB 3.x Hub" are untested ground.
    * A device with a serial number is known by it, as Microsoft's hub driver
      knows it: moved to another port, it keeps its Device Manager entry and
      is not found again as new hardware, and two identical devices plugged
      into the same port in turn no longer share one entry. A device without a
      serial number is still known by its port.
    * A device that none of Windows' own INFs names is named by its own
      product name, in Windows 98's Add New Hardware wizard and in Device
      Manager, instead of "USB Device"; where a Windows INF names the device,
      its name shows, as over Microsoft's own stack. On Windows 98 SE and ME a
      character outside plain ASCII shows as ?.
    * Fixed on Windows ME: re-enabling the controller in Device Manager with a
      USB mouse attached no longer makes Windows ME stop responding. The
      devices on the controller are now kept while it is disabled and come
      back as the same Device Manager entries when it is enabled again, as
      under Microsoft's own hub driver, instead of being removed and found
      again. Read with a mouse, a mouse and a stick, a USB keyboard alone, and
      devices behind a hub.
    * Fixed: the root hub's Power page in Device Manager showed every device's
      power as unknown, on every system, because the driver refused the
      request Windows sends for it; it now shows each device's power in mA.
      The Advanced page's bandwidth figure counts only isochronous pipes in
      use, as Microsoft's own stack does, so a mouse, a keyboard or a drive
      adds nothing to it; on Windows Vista and 7 it stays at zero, and a
      SuperSpeed device's power reads a quarter of its draw, both known
      limitations.
    * txtsetup.oem in every flavour directory, so that Windows 2000, XP and XP
      x64 text-mode Setup, and the Recovery Console of Windows 2000 and XP,
      can load the driver from a floppy at the F6 prompt on a machine whose
      keyboard or install medium is on an xHCI controller. Setup's own HID and
      storage drivers then run above this one. Limits: pressing F6 needs the
      firmware's own USB keyboard support, the floppy must be drive A:, a disk
      the driver runs as UAS is not usable until GUI-mode Setup, installing
      Windows onto a USB disk is not supported, and a USB drive left plugged
      in at the partition screen takes C:. When installing Windows XP and XP
      x64, GUI-mode Setup asks about the unsigned driver before the USB
      keyboard works, so a PS/2 keyboard or a laptop's built-in one is needed
      to answer it. In virtual machines Windows 2000 and XP text mode worked
      with a USB keyboard and stick, the Recovery Console of both logged in
      and ran commands with the USB keyboard alone, and XP and XP x64
      installed this way to the desktop.
    * The root hub's, and each hub's, first report of its devices waits up to
      5 seconds for the devices plugged in at start, because Windows 2000's
      text-mode Setup uses only the devices in that first report. Two new
      values, XhciFirstEnumWaitMs (default 5000, 0 off, at most 30000) and
      XhciFirstEnumPortMs (default 2000), set it. In virtual machines the
      first report went 20 to 30 ms after the start with nothing plugged in
      and 0.3 to 0.9 s after it with devices plugged in.
    * SweetLow's hidusbf sets a mouse's polling rate under this driver, up to
      1000 Hz for a Low- or Full-Speed device: read at 1000, 500 and 250 Hz at
      a root port and behind a hub on Windows 98 SE under NUSB 3.6 and on ME,
      at a root port on XP, and loading on a stock Windows 98 SE too, whose
      own usbd.sys has the routine it needs. It works behind a hub on XP as
      well (read on XP). Its setting must be applied again after an upgrade
      from 1.x.x.x.
    * XhciFastPollFsLs, a new value, off by default: at 2 or 3 a Low- or
      Full-Speed device on a root port that hidusbf sets to "31 Hz" or "62 Hz"
      is polled at 2000 and 4000 Hz, or 4000 and 8000 Hz. It is outside the
      xHCI specification and untested ground, read on no real controller and
      in no virtual machine; a controller that refuses it is caught and
      counted.
    * Updating from 2.0.0.0: install over it with Update Driver. On Windows 98
      SE and ME restart afterwards, although Windows does not ask: 2.0.0.0
      keeps running until then. On Windows 2000 use Have Disk, since a search
      keeps 2.0.0.0. The root hub's Driver tab keeps showing 2.0.0.0 until the
      root hub is updated too. Each device is found once more as new hardware,
      exactly once.
    * Known limitations: those of 2.0.0.0, less the Windows ME controller
      re-enable and the device with a serial number moved to another port,
      plus the XP F6 GUI-mode prompt that needs a PS/2 or built-in keyboard
      and the ? in a non-ASCII name on Windows 98 SE and ME. The release notes
      have the full list.

  2.0.0.0 - 2026-10-04

  The driver is rewritten as a whole USB host controller driver. xhci98.sys no
  longer plugs in underneath Windows' own USB port driver: it runs the
  controller, the root hub, every hub and the splitting of composite devices
  itself, and Windows' own class drivers sit on top of it unchanged. Because
  nothing of the USB 2.0-era stack is left underneath it, it drives SuperSpeed
  devices and hubs, and a second driver in the same package, xhciuas.sys, runs
  UAS storage. Every system 1.2.0.0 supports is supported, from the same four
  directories, each now holding both drivers.

  What changed

    * SuperSpeed (USB 3.x, 5 Gbit/s) devices on the root ports, and SuperSpeed
      hubs. A link that trains faster (SuperSpeedPlus) is accepted at its
      trained rate, untested. Device Manager on these systems still shows a
      SuperSpeed device as High Speed at most; the interface it reads predates
      SuperSpeed.
    * UAS storage, through xhciuas.sys, with streams at SuperSpeed and without
      at High Speed. A drive that offers both UAS and Bulk-Only gets UAS
      unless the new XhciForceBulkOnly value is set to 1.
    * Every device is reported to Windows at its true speed, on a root port
      and behind a hub. The virtual High-Speed hub of 1.2.0.0 is gone, and its
      three values (XhciVirtualHSHub, XhciVirtualHSHubVid,
      XhciVirtualHSHubPid) have no effect.
    * Windows 98 SE needs no USB 2.0 stack for the controller, hubs, mice,
      keyboards and audio. USB storage there, UAS included, still needs NUSB's
      mass-storage component (NUSB 3.3 or 3.6, or its five storage files on
      their own); the release notes and the readme's section 3 have the
      details.
    * The install writes an interrupt moderation interval of 160 (40
      microseconds) instead of 500. On the one machine measured, a UAS drive
      at SuperSpeed lost 15 to 22% of its throughput at 500.
    * In Device Manager the controller is "xHCI98 USB 3.x eXtensible Host
      Controller" and the root hub "xHCI98 USB 3.x Root Hub", with every
      device beneath it; external hubs no longer appear as entries of their
      own.
    * Gone under 2.0.0.0: the Windows 2000 audio device unplugged during
      playback that was never fully removed, the Windows 7 controller disable
      that hung, the Windows 98 freeze on fast repeated plugging, and the
      Windows 98 crash under NUSB when the controller was stopped - except on
      one path: updating in place over a running 1.2.0.0 under NUSB still
      crashes, because NUSB stops the old driver before the new one runs.
    * Upgrading from 1.2.0.0: on Windows 98 SE with NUSB, rename the old
      XHCI98.SYS and cold-boot before updating; on every system, pick the
      driver from a list with Have Disk rather than let Windows search. The
      readme's section 4 has the steps for each system.
    * Known limitations: the driver never puts an idle device or hub port to
      sleep; Windows 98 SE can wedge when a USB audio device is plugged in
      soon after a cold boot, as it could under 1.2.0.0; on Windows ME,
      re-enabling the controller with a USB mouse or keyboard attached hangs
      the machine; a device moved to a different port is found again as new
      hardware. The release notes have the full list and the untested ground.

  1.2.0.0 - 2026-10-02

  The driver gains an optional virtual USB 2.0 hub that lets a Full- or
  Low-Speed device on a root port be reported to Windows at its true speed. It
  is experimental and off by default: only use it if you know what you are
  doing. With it off the driver reports root ports exactly as 1.1.1.0 does.
  Every system 1.1.1.0 supports installs as it did, from the same four
  directories.

  What changed

    * The virtual High-Speed hub switch. XhciVirtualHSHub, a DWORD in the
      controller's driver key beside XhciImodInterval250ns, is 0 (off, what
      the install writes), 1 (a virtual hub appears above a Full- or Low-Speed
      device plugged into a root port, and goes with it) or 2 (every USB 2.0
      port carries one from start to stop). Behind it a mouse on a root port
      polls at the rate it asks for, a Full-Speed audio device on a root port
      plays from Windows XP on (the release notes say where it was read), and
      on Windows Vista and 7 a USB 1.1 hub on a root port no longer crashes
      the machine. The hub is one more entry in Device Manager and one more
      tier in a chain of hubs: with the switch on, a chain of external hubs
      can be one hub shorter than USB's five. Its id is pid.codes' shared test
      id 1209:0001, set by two string values the install also writes,
      XhciVirtualHSHubVid and XhciVirtualHSHubPid; a missing or invalid id
      turns the switch off for that start. Measured in virtual machines only;
      the virtual hub has never run on real hardware. The release notes' "The
      virtual High-Speed hub switch" and the readme's section 9 have the
      details.
    * Fixed, whatever the switch is set to: a Low-Speed device behind a hub
      showed Code 10 under SweetLow's stack on Windows 98 at 250 Hz and faster
      (GitHub issue 4).
    * Fixed, whatever the switch is set to: a High-Speed interrupt device
      behind a USB 2.0 hub, a mouse for example, could be polled every 125
      microseconds rather than at the interval usbport sets for it (the one
      the device asks for, up to usbport's 4 ms limit). It worked, but kept
      the bus busier than it needed to. Read in virtual machines on Windows 98
      SE under SweetLow's stack, ME, 2000, 32-bit XP, XP x64 and Vista in both
      architectures.
    * Fixed on Windows Vista and 7: the driver could arm one of usbport's
      timers without the lock usbport expects, a race with usbport's own timer
      code on another processor. It was found by reading the code and never
      observed. A root-port change with no hardware event behind it can now
      take up to one health-poll interval longer to be seen. Earlier systems
      are unchanged.
    * XHCISNAP reports what the driver read and applied for the three new
      values. Its snapshot format moved to schema 5 for that, so an XHCISNAP
      from an earlier release refuses this driver with "schema mismatch", and
      this one refuses an earlier driver. Use the copy in this package.
    * Not changed with the switch off: every device on a root port is still
      reported to Windows as High Speed, and the known limitations 1.1.1.0
      listed still apply. The release notes' "Known limitations" says which of
      them the switch addresses.

  1.1.1.0 - 2026-09-24

  The xHCI controller's properties in Device Manager gain an Advanced tab, and
  the install sets a shorter interrupt moderation interval, which on the one
  machine measured nearly doubled large reads from a USB stick. Every system
  1.1.0.0 supports installs as it did, from the same four directories.

  What changed

    * The controller's Advanced tab. Its properties in Device Manager now
      carry the tab Windows' own USB controllers have: a "Disable USB error
      detection" checkbox and a "Bandwidth Usage" button. Both are Windows'
      own; the package adds one line to its INF naming the page. Read in
      virtual machines on every supported system: Windows 98 SE under NUSB 3.3
      and under SweetLow's stack, Windows ME, Windows 2000, Windows XP in both
      architectures, and Windows Vista and Windows 7 in both. The bandwidth
      figures cost a Full-Speed device on a root port as a High-Speed one; the
      release notes' "Known limitations" say why.
    * The interrupt moderation interval. XhciImodInterval250ns, a DWORD in the
      controller's driver key in units of 250 ns, sets how long the controller
      waits after one interrupt before raising the next. The install writes
      500 (125 microseconds, at most 8,000 interrupts a second). A missing
      value, one the driver cannot read, or one outside 10 to 4000 means 4000
      (1 ms, the controller's own power-on value and what every earlier
      release ran at); an out-of-range value is replaced, not rounded, so a
      mistyped 0 cannot turn moderation off. A lower value raises the
      interrupt rate. On a ThinkPad P14s Gen 1 under Windows 98 SE, ATTO Disk
      Benchmark read and wrote about 33 to 34.6 MB/s at 500 from 64 KB
      transfers upward, where 4000 gave about 18 MB/s. The value in force was
      read back from the controller in Windows 98 SE and Windows 2000 virtual
      machines. The release notes' "The interrupt moderation setting" and the
      readme's section 9 say where the key is and how to change it.
    * On Windows 98 with NUSB, upgrading over an installed xhci98 still
      crashes that stack before the install's registry step, so an upgrade
      gets neither the tab nor the moderation value. The readme's section 5
      has the route that delivers both, even after an upgrade that has already
      crashed; with SweetLow's stack an ordinary Update Driver is enough.
      Right-clicking xhci98.inf and choosing Install, which earlier readmes
      suggested, copies the files and writes no registry value at all.
    * XHCISNAP reports the moderation value it read, the interval in force and
      what the controller took. Its snapshot format moved to schema 4 for
      that, so an XHCISNAP from an earlier release refuses this driver with
      "schema mismatch", and this one refuses an earlier driver. Use the copy
      in this package.
    * Not changed: the polling rates of Full- and Low-Speed devices, and every
      device on a root port being reported to Windows as High Speed. The known
      limitations 1.1.0.0 listed all still apply.
    * A known limitation, found on real hardware on 2026-09-23. On Windows 98,
      a USB audio device can stutter while a USB drive is read at full speed:
      on the P14s, a Full-Speed audio device on a root port stuttered from the
      2048 KB reads of a disk benchmark onwards at 500 and at 1000, and only
      on the last write at 4000. It follows the doubled read speed. If audio
      matters more than read speed, raise the value towards 4000 or delete it.
      The readme's section 7 and the release notes' "Known limitations" have
      it.

  1.1.0.0 - 2026-09-18

  Windows Vista and Windows 7 join the targets supported in virtual machines,
  in both architectures, and the download gains a 64-bit driver for them and
  for Windows XP x64. The package no longer writes any machine-wide registry
  value. Windows 98 SE, Windows ME, Windows 2000 and 32-bit Windows XP install
  as they did in 1.0.2.0, from a directory with a new name.

  Every 64-bit, Vista and Windows 7 result below comes from virtual machines.
  Of the new systems only 32-bit Windows 7 has run on real hardware, once,
  after this release was cut (a ThinkPad E460, 2026-09-19): the install and
  devices at a root port and behind USB 2.0 hubs worked, and the first disable
  of the controller hung. Three limitations were found after the cut and are
  listed in the last item below.

  What changed

    * The download has four driver directories instead of two: release-x86\
      and debug-x86\ hold the 32-bit driver, the one earlier releases carried
      in release\ and debug\, and release-x64\ and debug-x64\ hold a separate
      64-bit driver with an INF of its own. The 32-bit driver does not install
      on a 64-bit Windows or the other way round; picking the wrong directory
      is harmless, Windows finds no driver there and says so.
    * Windows XP x64 is supported through the 64-bit driver, in virtual
      machines only. An XP Professional x64 SP2 guest whose only USB
      controller was the xHCI installed it with no prompt for media, and bound
      a HID mouse, a USB mass-storage device and a composite audio device;
      disable, enable, remove and rescan in Device Manager all survived.
    * Windows Vista (SP2) and Windows 7 (SP1) are supported, 32-bit through
      the same driver as Windows 98 to XP and x64 through the 64-bit one, in
      virtual machines only. On each of four guests the package installed, the
      three devices above bound, and five disable and enable cycles, a remove
      and a rescan survived. On Vista x64 and Windows 7 x64 the driver loads
      only while driver signature enforcement is disabled, because it is not
      signed. Install it from Device Manager; right-clicking xhci98.inf and
      choosing Install does not work on these systems.
    * The idle suspend fix no longer writes the registry. Until now the
      install set DisableSelectiveSuspend = 1 under
      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB, which stopped
      every USB controller on the machine idling and outlived an uninstall.
      The driver now tells the USB stack itself, as it registers, that this
      controller must not be idled, which affects no other controller and
      leaves nothing behind. Read in virtual machines on all ten supported
      systems against a build that does idle. An upgrade leaves the old value
      in place on purpose; the release notes say how to delete it.
    * The driver: a finished transfer is handed back to Windows' USB stack
      only in the context that stack expects, holding its lock for that
      endpoint. Handing it back from anywhere else could corrupt the stack's
      own lists on a machine with more than one processor; a four-processor XP
      x64 guest crashed from it, and the 32-bit USB stacks on every target
      were read to make the same assumption, so both drivers now do this. Read
      on a four-processor 32-bit XP guest, and on Windows 98, ME and 2000.
    * The driver, from two audits: a device whose setup failed no longer keeps
      a controller slot it can never use; a device being set up when the
      controller is torn down no longer leaves Windows waiting for an address
      that will not come; a hub reusing an address another device held no
      longer confuses which device is behind which hub; a control transfer the
      controller stopped at its last stage reports the data it did move; a
      controller reset waits for the controller to be ready before and after,
      as Intel's controllers require; and the root hub keeps its port state
      current across a port suspend and resume, including a resume the
      controller ignores. Covered by host tests; the last was also read on a
      Vista guest, where Windows stopped with error 0xFE a minute after an
      ignored resume before the fix.
    * The tools: XHCIQUAL's quick scan ends on its verdict; its legacy verdict
      can no longer print "NOT QUALIFIED" and "QUALIFIED (with warnings)" for
      one run; and its controller quirk table follows Linux's for ASMedia,
      NEC, VIA and Fresco. XHCISNAP finds the driver's settings on Windows
      Vista and Windows 7, refuses switch combinations it used to accept and
      then ignore, and deletes a report it could not finish writing.
    * The download's readme.txt describes the current release only, and asks
      that XHCISNAP and the DebugView log be used only when the maintainer
      asks for them. The release notes add a Windows 2000 limitation: with a
      USB audio device attached, disabling the controller asks for a restart.
    * Known limitations found after the cut, 2026-09-19, with the driver
      unchanged. On Windows Vista and Windows 7, 32-bit and x64, a USB 1.1 hub
      on a root port crashes the machine (STOP 0x7E in USBPORT.SYS) once a
      mouse, keyboard or other slower device is used behind it; use a root
      port directly, or a USB 2.0 hub. On Windows XP and later a Full-Speed
      USB audio device on a root port plays nothing, though Windows shows it
      playing; behind a hub it plays (on Vista and 7 a USB 2.0 hub), and
      Windows 2000 plays on a root port. Both were measured in virtual
      machines, and the USB 2.0 hub workaround on the one real 32-bit Windows
      7 machine. On that same machine, disabling the controller in Device
      Manager hung, and so did the restart after it; the cause is not known
      yet. The readme's section 7 and the release notes' "Known limitations"
      have all three.

  1.0.2.0 - 2026-09-07

  A fix release. An audit found no critical defect and nineteen things worth
  fixing across the driver, the two tools and the package. All of them are
  closed (docs/contributing/roadmap.md, Phase 20).

  The install changes in one way: Windows now supplies usbui.dll as well,
  which brings back the USB Root Hub's Power tab on Windows 2000 and Windows
  XP. Everything else about it is as 1.0.1.0 left it.

  The device matrix on Windows 98 SE and Windows 2000 was re-read on this
  driver and is no worse than 1.0.1.0's. One change here has no machine behind
  it, the control-endpoint refusal below: nothing has been built to produce
  that state on purpose, and a test on the development machine is what covers
  it.

  What changed

    * The driver: an endpoint handle the hub driver has already replaced can
      no longer act on the endpoint that replaced it, and a device's endpoint
      table is reset under the lock the endpoint callbacks take. Read on a
      two-CPU Windows 2000 guest under Driver Verifier, the controller killed
      from outside the guest four times and back each time with every device,
      and on the Windows XP sequence 1.0.1.0's fix was for.
    * The driver: a device this driver has given up on can no longer have its
      control endpoint opened, or reopened after the failure. Covered by a
      host test; no machine has shown it.
    * The driver: after an in-place controller recovery the health poll's
      fatal latch reopens, so a second fault is recovered too. Until now only
      the first after boot was.
    * The driver: a recovery whose delivery is lost no longer stays armed for
      ever. It ages out after twenty health polls, counts as one of the
      bounded attempts, and a late delivery from the expired request is
      ignored.
    * The driver, three smaller ones: a Command Ring Stopped event still
      naming the abandoned command is resolved with a No Op rather than by
      adopting that command's own entry; the BIOS handoff write preserves the
      reserved bits of USBLEGCTLSTS; and the restore from standby puts back
      the interrupt moderation value it saved instead of zero. The last is
      host-model only, since the virtual machines fail every restore.
    * The tools: XHCISNAP exits nonzero on a report it could not finish
      writing instead of calling it written, and refuses a snapshot whose
      extension size does not match the driver's. XHCIQUAL's EHCI clean-up no
      longer writes the controller's write-one-to-clear status bits back.
    * The install: Windows supplies usbui.dll too, from its own installation
      source and only if the file is absent, by the same rule as usbd.sys and
      usbhub.sys. On Windows 2000 and Windows XP that brings back the USB Root
      Hub's Power tab, showing the hub's power budget and what is attached:
      those systems' own installer asks for that page and names this file as
      its provider, so on a machine that never had a USB controller it was
      silently missing. On Windows 98 and Windows ME nothing you can see
      changes. Upgrading a Windows 98 or Windows ME machine may ask for the
      Windows CD where the last install did not, because this file is new
      here; it sits on the same cabinet as the other two, so the same CD
      answers it. No Microsoft file is in the download.
    * The download: readme.txt and LICENSE no longer describe Microsoft files
      it stopped carrying in 1.0.0.1, and the "Windows 2000 never idles this
      controller" statement carries the measurement that qualified it
      (1.0.1.0's correction below). The checks that produce the download were
      tightened; its layout is unchanged.

  1.0.1.0 - 2026-09-04

  Windows XP joins the targets supported in virtual machines, the Windows 2000
  and Windows XP install now has the operating system supply every file the
  driver depends on, and one driver code change rides with them, for a fault
  the first XP guest showed. Windows 98 SE and Windows ME install as they did
  in 1.0.0.1.

  What changed

    * 32-bit Windows XP (SP3) is supported, in virtual machines only, the
      standing Windows ME has. An XP guest whose only USB controller was the
      xHCI installed the package from its directory with no prompt for media,
      loaded the driver on the first boot under XP's own USB stack, and bound
      a HID mouse, a USB mass-storage device and a composite audio device;
      disable, enable, remove and rescan in Device Manager all survived. XP
      reads the INF's Windows 2000 half, shows its unsigned-driver warning
      (choose Continue Anyway) and asks for nothing else. NUSB is a Windows 98
      SE package and is not for XP. Nothing has run on XP on real hardware.
    * Windows 2000 and Windows XP: usbport.sys, the USB stack this driver
      plugs into, now comes from the operating system's own driver cache
      (sp4.cab, sp3.cab), the way usbd.sys already did, and usbhub.sys with
      it; the install asks for no media. Windows Setup places none of the
      three unless it finds a USB controller it recognises, so a Windows 2000
      or XP machine that has never had another USB controller has none of them
      on its disk. Until this release the package's NT install named only
      usbd.sys, and on such a machine the driver installed but could not load
      (Code 39 on XP). A machine that ever had a USB 1.1 or 2.0 controller
      already has the files and sees no difference.
    * Windows 2000 and Windows XP: the install now writes
      DisableSelectiveSuspend = 1 under
      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB, as the Windows
      98 install has since 1.0.0.0. XP's USB stack idles a controller with
      nothing attached about half a minute after start, and a sleeping xHCI
      cannot report a newly plugged device; Windows 2000's never idles this
      controller, so there the value changes nothing you can see. It is a
      machine-wide setting, and an uninstall does not remove it; the release
      notes' "Known limitations" say what it does to other controllers.
    * The driver: when the hub driver re-creates a device in the middle of its
      enumeration through a second device handle and then removes the first,
      as Windows XP does on the first attach of a mass-storage or composite
      device, the removal of the superseded handle's control endpoint is no
      longer taken for the live one closing. Before this release such a device
      failed on its first attach on XP and worked when unplugged and plugged
      in again (docs/issues/04-xp-restore-device-ep0-remove.md). Windows 98 SE
      and Windows 2000 never provoke it and read unchanged on the same binary.
    * Correction: the DisableSelectiveSuspend entry above says Windows 2000's
      USB stack never idles this controller and the value changes nothing
      there. That was generalised from the Phase 3 spike's observation window
      and was never measured; the owner's checks contradict it. Whether and
      when Windows 2000 idles the controller is unestablished until a reading
      is recorded (roadmap Phase 20, F18). The value is written on every
      install path regardless, and that is unchanged.

  1.0.0.1 - 2026-09-02

  The driver is unchanged. This release changes how it is installed: the
  package no longer carries any Microsoft file, and the two Windows files the
  driver depends on come from Windows itself.

  What changed

    * 1.0.0.0 shipped usbd98.sys, usbd2k.sys and usbhub98.sys beside the
      driver: Windows 98 SE's and Windows 2000 SP4's own usbd.sys and Windows
      98 SE's own usbhub.sys, because Windows only places its USB files when
      Setup finds a USB controller it recognises and an xHCI-only machine has
      none of them. The INF now asks Windows to copy those files from its own
      installation source instead (the LayoutFile directive Windows' own INFs
      use), still without overwriting a file that is already there. The
      download is this project's two files per flavour, the tools and the
      readmes.
    * What you see: on an xHCI-only Windows 98 SE machine the install asks for
      the Windows 98 Second Edition CD-ROM ("Insert Disk") unless the Windows
      CABs are on the hard disk, as on OEM and Windows 98 QuickInstall
      installs. Have the CD at hand; readme.txt section 3 says what is being
      fetched and what happens if the prompt is cancelled. A machine that ever
      had a USB 1.1 controller already has the files and is not asked. Windows
      2000 asks for nothing.
    * Windows ME is a supported target, in virtual machines only and under
      SweetLow's USB 2.0 stack only, the standing Windows 2000 has. A Windows
      ME guest loaded and started the driver and bound a HID mouse, a USB
      mass-storage device and a composite audio device. Its stock USB stack
      has no usbport.sys, so on a stock Windows ME machine the driver installs
      and shows Code 2 until SweetLow's stack is installed; NUSB is a Windows
      98 SE package and is not for Windows ME. The INF is unchanged by this:
      Windows ME reads its Windows 98 half.
    * xhci98.sys is rebuilt only so that its version resource matches; no
      driver code changed between 1.0.0.0 and this release.

  1.0.0.0 - 2026-08-30

  The first release. There is nothing before it to compare against: the builds
  this project cut while the work was going on were numbered 0.x, none was
  uploaded anywhere or given to anyone, and they are gone. If you are holding
  a copy of this driver, this is the version of it.

  What it is

  xhci98.sys is a USB host controller driver for xHCI (USB 3.0) controllers on
  Windows 98 SE and Windows 2000 SP4. It gives those systems working USB on a
  machine whose only USB controller is xHCI, which is what most x86 PCs built
  from around the mid 2010s onward have. One binary serves both systems, and
  the installer carries an install path for each.

  What you get is USB 2.0: High-, Full- and Low-Speed devices, on the USB 2.0
  ports an xHCI controller exposes alongside its SuperSpeed ones. SuperSpeed
  is out of scope, so a USB 3.0 device trains at High Speed rather than not
  connecting at all. Keyboards, mice, flash drives, USB Ethernet adapters,
  hubs with devices behind them and USB audio have all run through it.

  On Windows 98 it is not standalone. NUSB 3.3 has to be installed first,
  since that is what puts Microsoft's USB port driver on the machine; the
  driver plugs in underneath it rather than replacing it. Windows 2000 SP4
  already has its own.

  What is in the download

    * release\ and debug\, the same driver built two ways. Install from
      release\. debug\ is there for diagnosing a machine that misbehaves, and
      it is the same version, so the two are kept in the directories they
      arrived in rather than copied together.
    * XHCIQUAL.EXE, a DOS tool that answers "will this driver work on this
      machine" before anything is installed. Run it first; one of the ways a
      machine can fail cannot be fixed in software, and finding that out takes
      thirty seconds.
    * XHCISNAP.EXE, which reads the driver's own log off a running machine and
      writes a report you can send. On Windows 98 it is the only route there
      is: the usual kernel capture tool crashes that system on real hardware.
    * readme.txt, a standalone install and usage guide that assumes you have
      the directory and nothing else, and LICENSE.
    * The three Microsoft files the installer needs and an xHCI-only machine
      has never been given: Windows 98's and Windows 2000's own usbd.sys, and
      Windows 98's usbhub.sys, which is what multi-function devices bind
      through. Each is copied without overwriting a file you already have.

  What 1.0.0.0 claims, and what it does not

  Final means the driver does what this project says it does and that its
  limits are written down, not that nothing is left to do.

  On Windows 98 SE the driver is validated on real hardware behaviourally:
  devices enumerate, work, and survive being unplugged, on a physical machine
  rather than only in an emulator. What it is not on that target is
  continuously instrumented. There is no running trace to be had on Windows 98
  on real hardware and no way to capture anything from a crash, so a machine
  that goes down takes what the driver was holding with it. What can be had is
  a report on demand, with XHCISNAP.EXE, after the fact.

  On Windows 2000 SP4 every result this project has comes from a virtual
  machine. Windows 2000 has never run on real hardware here: Setup bugchecks
  during installation on both machines it was tried on, a ThinkPad E460 and a
  ThinkPad P14s Gen 1, and no other candidate machine is available. Nothing
  about this driver caused that, since it never got as far as loading. If you
  already run Windows 2000 SP4 on a machine with an xHCI controller, the
  install path is written for you and you would be the first to walk it.

  Every xHCI controller this project has ever read is an Intel one, in those
  two laptops. No AMD controller has been tried.

  The known limitations are published rather than summarised. Several of them
  are faults in the USB stack this driver plugs into rather than in the
  driver, and each says how that was established. Two matter enough to name
  here: stopping this driver in Device Manager crashes Windows 98, which makes
  disabling, uninstalling and upgrading it on that system cost a crash; and
  plugging a device in and out repeatedly, several times a second for minutes,
  can freeze Windows 98, which is this driver's own defect and has no
  explanation yet. The release notes (docs/using/release-notes.md, "Known
  limitations", which section 7 of readme.txt points at) have the full list,
  with what was measured and on which machine.

==============================================================================
 LICENCE
==============================================================================

GNU GPL v2 - see the LICENSE file in this directory, beside this readme. This
applies to xhci98.sys, xhciuas.sys and their INFs, which are this project's
own work.

No Microsoft file is in this download. The usbd.sys and usbui.dll the
install needs are copied by Windows from your own Windows installation
source (section 3), and on Windows 98 SE the storage files of section 3 come
from NUSB; nothing here grants you any right in them, and nothing here
copies them. xhciuas.inf names NUSB's USBNTMAP.SYS on Windows 98 SE; the
file is named, never shipped.

The provenance record for everything the project depends on but does not own
is in docs/contributing/legal-provenance.md, in the project's source
repository rather than here.
