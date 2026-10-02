==============================================================================
                              x h c i 9 8   1.2.0.0
  USB 2.0 for Windows 98 SE, ME, 2000, XP, Vista and 7 on xHCI-only machines
==============================================================================

Released 2026-10-02.

Most PCs made from the mid 2010s onward have only USB 3.0 (xHCI) controllers,
which older Windows cannot use. This driver drives them as USB 2.0 on:

  - Windows 98 SE, ME, 2000 SP4, XP, Vista and 7, 32-bit
  - Windows XP, Vista and 7, x64, through a separate 64-bit driver. On Vista
    x64 and 7 x64 it is unsigned, so driver signature enforcement must be
    disabled (section 4).

Only Windows 98 SE has been validated on real hardware; the rest in virtual
machines only. Speeds are USB 2.0 (High, Full and Low Speed); a USB 3.0
device still works, at USB 2.0 speed, through the same connector.


WHY ONLY USB 2.0, WHEN THE CONTROLLER IS A USB 3.0 ONE
------------------------------------------------------------------------------

The existing usbport.sys this driver depends on does not support USB 3.0, and
neither does anything above it. This driver, xhci98.sys, is only the miniport
underneath that stack.

SuperSpeed would mean rewriting the entire USB host controller driver for
every one of these operating systems. That is significantly more work than
this driver, for a speed that most machines running Windows 98 or Windows
2000 are unlikely to effectively use. The project's xHCI programming guide,
docs/usb-xhci-info/xhci-programming.md in the source repository, summarises
what it would take.

Every USB 3.x connector (USB4 and Thunderbolt included) also carries the USB
2.0 wires, and xHCI exposes them as a separate logical port per connector.
This driver manages those USB 2.0 ports and leaves the USB 3.x ones
unpowered, so a SuperSpeed-capable device falls back on the USB 2.0 port and
runs at High Speed. What can differ on such a machine is which controller a
given port belongs to, so the machine may show more than one unrecognised USB
controller - install on the one XHCIQUAL reports USB 2.0 ports for.


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
  3. The files Windows supplies
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

A controller reporting no interrupt pin cannot be driven at all, on either
Windows version. There is no software workaround: neither system can use the
modern interrupt mechanism (MSI) that such a controller would require.


==============================================================================
 2. WHAT YOU NEED
==============================================================================

  Operating system   Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows
                     ME, 32-bit Windows XP (SP3), 32-bit Windows Vista (SP2)
                     and 32-bit Windows 7 (SP1) in virtual machines only
                     (of these, only 32-bit Windows 7 has run on a real
                     machine, once; see section 7).
                     64-bit: Windows XP x64 (SP2), Windows Vista x64 (SP2)
                     or Windows 7 x64 (SP1), in
                     virtual machines only as well.

  On Windows 98      NUSB 3.3 or the newer SweetLow USB 2.0 stack, your
                     choice, installed BEFORE this driver (section 4).

  On Windows ME      SweetLow's USB 2.0 stack, installed BEFORE this driver
                     (section 4). NOT NUSB: that is a Windows 98 SE package.
                     Windows ME's own USB stack has no usbport.sys, and on
                     it this driver installs and shows Code 2.

  On Windows 2000    SP4's own USB stack, or the standalone USB 2.0 update
                     KB319973. DO NOT install NUSB on Windows 2000.

  On Windows XP      XP's own USB stack; nothing to install. DO NOT install
                     NUSB on Windows XP.

  On Windows Vista   The system's own USB stack; nothing to install.
  and Windows 7

  Controller         xHCI, PCI class code 0C0330, at least one USB 2.0 port,
                     a memory window below 4 GB, and a legacy interrupt pin.


==============================================================================
 3. THE FILES WINDOWS SUPPLIES
==============================================================================

xhci98.inf names two files of its own, xhci98.inf and xhci98.sys, and they
are in:

      RELEASE-X86\, DEBUG-X86\, RELEASE-X64\ and DEBUG-X64\

Nothing else is in the package, and there is nothing to complete: a copy
taken from the project's source repository is the same two files.

Four files the driver depends on are NOT in the package, because they are
Windows' own, unmodified, and no Microsoft file is in this download:

  usbd.sys     The USB 2.0 root hub imports it on every target. Without it
               the USB ROOT HUB fails: Code 2 on Windows 98 and Windows ME,
               error 0xc0000034 naming usbhub20.sys on Windows 2000 and
               Windows XP.

  usbhub.sys   On Windows 98, the driver for devices that are more than one
               thing at once - a sound card with a volume knob, a headset
               with buttons, a keyboard with media keys. Without it every
               such device stops at USB Composite Device with Code 2 and
               does nothing at all (under NUSB's stack; SweetLow's brings
               its own composite driver). On Windows 2000 it is the USB hub
               driver.

  usbport.sys  The USB stack this driver plugs into. Without it the
               controller shows Code 39 and the driver never runs. On Windows
               98 and ME the USB 2.0 stack installed first (NUSB or
               SweetLow's) supplies it.

  usbui.dll    The one file here that is only cosmetic. It adds an extra USB
               property page in Device Manager.

UP TO WINDOWS XP, WINDOWS ONLY INSTALLS ITS USB FILES WHEN SETUP FINDS A USB
CONTROLLER IT RECOGNISES, and on an xHCI-only machine it never does, so on
such a machine none of them is there. The install in step 4 therefore asks
Windows to copy them from its own installation source. Each is copied only
if it is absent, so a machine that already has them - one that ever had a USB
controller Windows recognised - keeps its own files and is asked for nothing.
Windows Vista and Windows 7 always have all four.

  WINDOWS 98 SE   HAVE THE WINDOWS 98 SE INSTALLATION CD AT HAND. Unless the
                  Windows CABs are on the hard disk (C:\WINDOWS\OPTIONS\CABS,
                  as on OEM and Windows 98 QuickInstall installs), the
                  install shows "Insert Disk" asking for the Windows 98
                  Second Edition CD-ROM: insert it and click OK, and if it
                  then asks where to copy from, give it the CD's WIN98
                  folder. It is asking for usbd.sys, usbhub.sys and
                  usbui.dll, not for anything of this driver's.

  WINDOWS ME      The same as Windows 98 SE, with the Windows ME CD. The
                  machine tried (a virtual one) had the CABs on its hard
                  disk from its own Setup and asked for nothing.

  WINDOWS 2000    Nothing to do: all four come from the driver cache every
  AND XP          Windows 2000 or XP installation has (Driver Cache\i386).
                  On 32-bit Windows XP all four are in sp3.cab; on Windows
                  2000 three are in sp4.cab and usbui.dll in driver.cab
                  beside it, two cabinets in one pass and still no prompt.
  WINDOWS XP x64  Nothing to do either, from Driver Cache\amd64: usbport.sys
                  and usbhub.sys out of sp2.cab, usbd.sys and usbui.dll out
                  of driver.cab beside it. The machine tried (a virtual one,
                  which had never had a USB controller) asked for nothing.

  WINDOWS VISTA   Nothing to do: every installation already has all four,
  AND 7           and the install copies none of them.

If the prompt is cancelled the driver still installs, but the root hub fails
as described above. That reads as a fault in this driver and is not one: put
the CD in and install the driver again, or copy usbd.sys (and, on Windows 98
with NUSB, usbhub.sys) out of the CD's WIN98 CABs into
C:\WINDOWS\SYSTEM32\DRIVERS yourself.


==============================================================================
 4. INSTALL
==============================================================================

INSTALL FROM THE RELEASE DIRECTORY:

      RELEASE-X86 or RELEASE-X64\

This package carries BOTH builds side by side, RELEASE and DEBUG, each a
complete set of files with the same names, so the directory you point
Windows at is what decides which driver you get. RELEASE is the one you
want. DEBUG (DEBUG-X86 or DEBUG-X64\) is the same driver built so that a
crash on it can be traced further back. It records nothing more than RELEASE
does, and it is there only for troubleshooting a machine that has already
gone wrong. It prints nothing as it runs. Section 8 describes both, and
nothing about a copied file says which one it is - so point at a directory,
never at a loose xhci98.sys.

There are two of each, one per architecture: the -X86 directories are for
32-bit Windows and the -X64 ones for 64-bit Windows. If you pick the wrong
one nothing breaks - Windows finds no driver in it and says so - so try the
other.

Put the whole unzipped package somewhere the machine can read - a floppy, a
CD, a shared folder - then:

  WINDOWS 98 SE
      A USB 2.0 stack (usbport.sys + usbhub20.sys) has to be there first:
      either NUSB 3.3 or the newer SweetLow stack, your choice.

        NUSB 3.3 - the configuration this driver is tested against.
        Install it first. NUSB 3.6 carries the same stack and also works.

        SWEETLOW'S STACK - the newer Windows XP lineage of the same port
        driver, under which disabling, removing and upgrading this driver
        do NOT crash Windows 98 (section 5). A system installed with
        Windows 98 QuickInstall 1.0.1 or later already has it. On any other
        Windows 98 SE, download
        http://sweetlow.orgfree.com/download/usb20_win9x.zip (the same
        files win98-driver-lib-base carries as [MBD]_sweetlow_usb2.0),
        unzip it, right-click the USB2.INF at its root, choose Install,
        and reboot. If NUSB is already installed,
        first remove its USB 2.0 stack through Add/Remove Programs ("Remove
        Unofficial Universal USB 2.0 Stack"), then install SweetLow's before
        rebooting.

      Then open Device Manager and find the unrecognised xHCI controller:
      it sits unclaimed with a yellow mark, usually under "Other devices".
      Then
          Properties -> Driver -> Update Driver -> Specify a location
      and point it at the RELEASE-X86\ directory.
      During the copy, on a machine that never had a USB controller Windows
      recognised, "Insert Disk" asks for the Windows 98 Second Edition
      CD-ROM: that is Windows fetching its own usbd.sys, usbhub.sys and
      usbui.dll (section 3). Insert it and click OK. Reboot when asked.

      (If Windows finds the controller for you first, the Add New Hardware
      Wizard asks the same question - give it the same directory.)

  WINDOWS ME
      SweetLow's stack has to be there first, and only that one: NUSB is a
      Windows 98 SE package and is not for Windows ME. Download
      http://sweetlow.orgfree.com/download/usb20_win9x.zip, unzip it,
      right-click the USB2.INF at its root, choose Install, and reboot.
      Then the same Device Manager route as Windows 98 SE:
          Properties -> Driver -> Update Driver -> Specify a location
      pointed at the RELEASE-X86\ directory.
      Without the stack the driver installs and the controller shows Code 2.
      Windows ME has only been run in a virtual machine.

  WINDOWS 2000 SP4
      Open Device Manager and find the unrecognised xHCI controller, then
          Properties -> Driver -> Update Driver -> Have Disk
      and point it at the RELEASE-X86\ directory.
      Nothing else is asked for; usbport.sys, usbd.sys, usbhub.sys and
      usbui.dll come from the driver cache every installation has. If the
      Found New Hardware wizard is used instead, it ends by asking for a
      restart; No is fine, the driver is already running.

  WINDOWS XP (32-BIT)
      The same route as Windows 2000 SP4:
          Properties -> Driver -> Update Driver -> Have Disk
      pointed at the RELEASE-X86\ directory.
      Choose "Continue Anyway" at the unsigned-driver warning. Nothing else
      is asked for. Windows XP has only been run in a virtual machine.

  WINDOWS VISTA AND WINDOWS 7 (32-BIT)
      Open Device Manager and find the unrecognised xHCI controller, then
          Update Driver Software -> Browse my computer for driver software
      and point it at the RELEASE-X86\ directory.
      If Windows warns that it cannot verify the publisher, install the
      driver anyway. Do NOT right-click xhci98.inf and choose Install on
      these systems: it asks for usbport.sys, which you cannot supply, and
      installs no driver. Both have only been run in virtual machines.

  64-BIT WINDOWS
      The same route as the 32-bit edition of the same Windows, pointed at
      RELEASE-X64\. On Windows Vista x64 and Windows 7 x64 the install works
      on an ordinary start, but the driver runs only while driver signature
      enforcement is disabled, as it is unsigned; otherwise the controller
      shows Code 39. Windows XP x64 needs none of that.

It installs as "USB 2.0 eXtensible Host Controller (xhci98)", with a "USB
Root Hub" underneath it. Neither should carry a warning mark.

>> ON WINDOWS 98 WITH NUSB, READ SECTION 5 BEFORE YOU EVER DISABLE, REMOVE <<
   OR UPGRADE THIS DRIVER IN DEVICE MANAGER. Each of those blue-screens that
   system, and there is a way round it. It is not this driver - Microsoft's
   own USB drivers do the same on the same machine, and under SweetLow's
   stack the same driver survives all three - but it is easier to know
   before than after.


==============================================================================
 5. USING IT
==============================================================================

Plug devices in and they are found and installed the usual way. Keyboards,
mice, flash drives, USB Ethernet adapters and hubs all work through the
system's own drivers - this driver only replaces the controller layer
underneath them.

Two things are specific to this driver and worth knowing in advance:

  * USB 3.0 PORTS STILL WORK, AT USB 2.0 SPEED. Every USB 3.0 connector also
    carries the USB 2.0 wires, and that is the path used. The SuperSpeed half
    of each connector is deliberately left switched off.

  * THIS CONTROLLER NEVER GOES TO SLEEP, SO IT DRAWS SLIGHTLY MORE POWER.
    Windows normally puts an idle USB controller to sleep - Windows 98 within
    about half a second of the last transfer, Windows XP within about half a
    minute of a start with nothing attached, 32-bit Windows 7 within about
    ten seconds - and a sleeping controller of this kind cannot notice
    anything plugged in afterwards. The driver tells Windows not to, for this
    controller only. Nothing else in the machine is affected and nothing is
    written outside the device's own settings. There is no switch to turn it
    back on.

  WINDOWS 98 WITH NUSB: STOPPING A RUNNING USB CONTROLLER CRASHES THE MACHINE
  ..........................................................................

  DISABLING OR REMOVING ANY USB HOST CONTROLLER IN DEVICE MANAGER BLUE-SCREENS
  WINDOWS 98 WHEN NUSB'S USB 2.0 STACK IS INSTALLED - the fatal-exception
  screen, "A fatal exception 0E ... at 0028:C00312EE", which on that system
  means a reboot and whatever was unsaved.

  THIS IS NOT THIS DRIVER - it happens identically with Microsoft's own
  usbehci.sys on the same machine. The fault is in NUSB's usbport.sys, the
  Windows 2000 build of the USB 2.0 stack: under SweetLow's build of that
  stack (section 4) the same driver on the same machine disables,
  re-enables, removes and upgrades without crashing, and so does Windows
  2000. Disabling the USB ROOT HUB is fine on either stack. Everything
  below this line applies to NUSB systems.

  Everything that stops the running driver reaches that same crash, which on
  Windows 98 means all three of these:

    DISABLE      crashes.

    UNINSTALL    crashes, AND THE REMOVAL DOES NOT HAPPEN. The next boot
    (Remove)     comes back with the driver still installed and working, so
                 you have paid a crash and are no further forward.

    UPGRADE      crashes. The new file is copied BEFORE the crash, so the
    (installing  new driver does load afterwards - but nothing after that
    over an      copy runs, so the machine still reports the OLD version
    existing     and any registry setting the new package introduces is
    install)     never written. See "TO UPGRADE WITHOUT CRASHING" below.

  There is no Roll Back Driver on Windows 98, so a rollback is an uninstall
  followed by a reinstall - two of the above.

  BEFORE YOU SPEND ONE OF THESE CRASHES, HAVE A WAY BACK. One of them left
  a test machine unable to reach the desktop on the next boot, with ScanDisk
  reporting the disk perfectly clean.

  TO REMOVE THE DRIVER WITHOUT CRASHING, UNLOAD IT FIRST
  .....................................................

    1. From an MS-DOS Prompt:
           ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
    2. Reboot. The controller comes up with a yellow mark and no USB Root
       Hub under it - that is the driver not loading, and it is what makes
       the next step safe.
    3. Back in Windows, rename it back:
           ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SAV XHCI98.SYS
       DO NOT press Refresh in Device Manager - that would load it again.
    4. Device Manager -> the controller -> Remove. It completes, with no
       crash.

  A Windows 98 uninstall then removes REGISTRY ENTRIES ONLY. xhci98.sys, the
  usbd.sys, usbhub.sys and usbui.dll the install had Windows copy from its
  CD (section 3) and the setup engine's cached copy of xhci98.inf (under
  C:\WINDOWS\INF\OTHER) all stay behind. Delete them by hand if you want them
  gone; the three Windows files are Windows' own and harmless where they are.

  TO UPGRADE WITHOUT CRASHING, OR AFTER AN UPGRADE THAT CRASHED
  .............................................................

  Start with the same rename, so that nothing is running to be stopped:

    1. From an MS-DOS Prompt:
           ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV
    2. Shut the machine down and switch it on again - not Restart: a warm
       restart leaves Windows 98 stuck at its starting screen. The
       controller comes up with a yellow mark, as above.
    3. Device Manager -> the controller ->
           Properties -> Driver -> Update Driver -> Specify a location
       and point it at the new package's RELEASE-X86\ directory. With no
       driver loaded there is no controller to stop, so it finishes
       normally and writes the new package's settings.
    4. Shut down and switch on again.

  That is the only route measured to deliver a new package's registry
  settings on this stack. If an upgrade has already crashed, take the same
  four steps: the crashed upgrade did copy the new xhci98.sys, so the file
  is already in place, and what it lost is the settings, which step 3
  writes.

  DO NOT RELY ON RIGHT-CLICKING xhci98.inf AND CHOOSING INSTALL FOR THIS.
  Earlier copies of this file said to, and it does not do the job: it
  copies files and writes no registry value at all, and it could never
  write a setting that belongs to the device itself, which is the kind the
  crash loses.


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
  way to get anything out. On Windows Vista and Windows 7 it finds and sets
  the driver's setting, but reading the log back has not been tried there.
  When you are asked, it is four steps, and none of them is REGEDIT:

      1. XHCISNAP -verbosity 2
      2. restart the machine
      3. make the problem happen again
      4. XHCISNAP -o C:\MYDUMP

  Then send C:\MYDUMP.TXT. Attach C:\MYDUMP.BIN as well if you are asked
  for it. Step 1 finds the right registry key for you, on every controller
  this driver runs - see section 9 for what it sets and why finding that key
  by hand is easy to get wrong.

  STEP 2 IS NOT OPTIONAL. The driver reads that setting once, when it
  starts, and nothing re-reads it while it is running. Without the restart the
  driver is still at whatever it read last time - which on a fresh install is
  OFF, and then XHCISNAP gets no answer at all rather than an empty one.

  If nothing comes back at all, run XHCISNAP -probe. It checks the route to
  the driver separately from whether this driver answers on it.

  USE THE XHCISNAP.EXE FROM THIS PACKAGE, not a copy kept from an earlier
  release. This driver's report is snapshot schema 5, which grew by the
  interrupt moderation setting and the virtual hub's switch and ids
  (section 9), so an older XHCISNAP refuses it
  with "schema mismatch" and reports nothing, and this one refuses an older
  driver the same way.

  XHCISNAP.EXE changes nothing about how the driver behaves on the bus, and
  writes no file it was not asked to. It does READ the controller's port
  registers, which is a hardware access - it just does not write one, and it
  deliberately does not clear the "something changed here" flags it finds, so
  it takes no evidence away from the driver either.

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
  That is the arrangement because a driver on Windows 98 has no reliable way
  to open a file at all.

  DEBUGVIEW (Sysinternals), with "Capture Kernel" switched on, captures this
  driver's stop-time dump if XhciLogDebugView is set. LIKE XHCISNAP, USE IT
  ONLY WHEN THE MAINTAINER ASKS FOR IT. Windows 2000 runs any
  current version. WINDOWS 98 NEEDS v4.64 - later versions do not run on it
  at all - and on Windows 98 it does not help anyway: the dump happens when
  the driver stops, the only stop on that system is the shutdown, and Windows
  closes the capture program before it gets there. Use XHCISNAP.

      !! Do not run DebugView on Windows 98 on real hardware while
         capturing. Plugging in a device while it captures can crash the
         machine, and neither build in this package has been shown safe
         there. Inside a virtual machine it is fine. You do not need
         DebugView to send a report.


==============================================================================
 7. KNOWN LIMITATIONS
==============================================================================

The ones called out above are those you are most likely to meet. The full
measured list, including USB Audio on Windows 98, is in the project's
docs/using/release-notes.md.

Read it before reporting a problem, and then report it anyway if it is not
there - the reports are what fix it:

      https://github.com/yeokm1/xhci98/issues

That file also records what has and has not been tested on real hardware as
opposed to in a virtual machine, and each entry says which.

SEVERAL OF THEM ARE NOT IN THIS DRIVER. They are in the USB stack it plugs
into, which on Windows 98 is NUSB 3.3's back-port of the Windows 2000 stack
plus that system's own class drivers. They are listed anyway, because you
meet them through this driver and have no other way to find out. The two
measured so far, each established by reproducing the same failure without
this driver involved, and the two you are likeliest to meet:

  * THE CONTROLLER TEARDOWN CRASH of section 5 - the same crash, at the same
    address, with Microsoft's own usbehci.sys.

  * USB AUDIO PLAYBACK ON WINDOWS 98 IN A VIRTUAL MACHINE fails inside that
    system's own USBAUDIO.VXD, and does so at the same address through a
    completely different USB controller with this driver idle. That is not
    a statement about Windows 98 itself: one physical USB audio device played
    clean on a real machine, on a root port and behind a hub, on clips of
    seconds. See the release notes' "Known limitations", the USB Audio entry.

THREE THAT ARE THIS DRIVER'S, from how it reports speeds, AND THE
EXPERIMENTAL VIRTUAL HUB SWITCH ADDRESSES ALL THREE. Every device on a root
port is reported to Windows as High Speed (the release notes say why). The
switch of section 9, at 1 or 2, puts a virtual hub between the root port
and the device. It is off by default, so on a normal install these apply,
with the workarounds below. All three were measured in virtual machines:

  * WINDOWS VISTA AND 7, 32-BIT AND X64: A USB 1.1 HUB ON A ROOT PORT CRASHES
    THE MACHINE (STOP 0x7E in USBPORT.SYS) once a mouse, keyboard or other
    Full or Low Speed device is used behind it. Plug such devices into a
    root port directly, or behind a USB 2.0 hub (measured on one real
    32-bit Windows 7 machine: no crash). The same hub works on Windows 98,
    2000, XP and XP x64.

  * A MOUSE OR KEYBOARD ON A ROOT PORT POLLS AT 1, 2 OR 4 MS ONLY, whatever
    it asks for, and a polling-rate tool shows no effect inside one of those
    steps. Behind a hub a device is reported at its true speed and polls at
    its own interval (on Vista and 7 use a USB 2.0 hub, see above).

  * WINDOWS XP AND LATER: A FULL-SPEED USB AUDIO DEVICE ON A ROOT PORT PLAYS
    NOTHING, though Windows shows it playing. Behind a hub it played on
    32-bit XP; on Vista and 7 use a USB 2.0 hub (measured on one real
    32-bit Windows 7 machine: silent on a root port, plays behind the hub).
    Windows 2000 plays on a root port.

ONE WHOSE CAUSE IS NOT KNOWN YET, found on the one real Windows 7 machine
tried (32-bit, a ThinkPad E460):

  * WINDOWS 7: DISABLING THE USB CONTROLLER IN DEVICE MANAGER CAN HANG. The
    first Disable never finished, and the next restart hung until the
    machine was switched off at the power button; after that the controller
    started disabled, and enabling it brought USB back. Uninstalling or
    upgrading the driver stops the controller too and was not tried; expect
    the same. The Vista and 7 virtual machines did not show it. Until it is
    understood, do not disable, uninstall or upgrade the controller on
    Vista or 7 with unsaved work open, and be ready to power off if the
    restart that follows does not finish.

ONE THAT COMES WITH THE MODERATION SETTING, found on one real Windows 98 SE
machine (a ThinkPad P14s Gen 1, NUSB 3.3):

  * WINDOWS 98: USB AUDIO CAN STUTTER WHILE A USB DRIVE IS READ AT FULL
    SPEED. A Full-Speed audio device on a root port stuttered from the
    2048 KB reads onwards in a disk benchmark at the install's 500, and the
    same at 1000; at 4000 only on the last, 8192 KB write. It follows the
    doubled read speed that 500 brings. If audio matters more than read
    speed, raise the value towards 4000 or delete it (section 9).

COMPOSITE DEVICES ON WINDOWS 98 - HANDLED BY THIS PACKAGE
.........................................................

A device that is more than one thing at once - a headset with buttons, a
keyboard with media keys - stops at "USB Composite Device", Code 2, with
nothing loading above it, on a Windows 98 machine that is missing one file.
The install asks Windows for that file (section 3), so it is worth knowing
what it is if you ever see that symptom on a machine this package did not set
up, or on one where the Insert Disk prompt was cancelled.

NUSB does not ship the composite parent, but that is not an NUSB defect:
the parent is Windows 98 SE's own usbhub.sys, and Windows 98 setup only
places its USB driver FILES when it finds a USB controller it recognises,
so on an xHCI-only machine that file was simply never put there. Under
SweetLow's stack the parent is its own usbccgp.sys and the file is not
needed.


==============================================================================
 8. WHAT IS IN THIS DIRECTORY
==============================================================================

  RELEASE-X86\  - INSTALL THIS ONE, on 32-bit Windows

  The normal driver. This is the one you want.

      xhci98.inf
      xhci98.sys   100,235 bytes
      SHA-256
      2D53B5F45D7CC6E55C4D7F23111C8B5D2628341CCE7065360432D48EA9DDEB35

  DEBUG-X86\  - only when diagnosing a problem

  The same driver, built so that a crash on it can be traced
  further back. It records nothing more than RELEASE-X86\ does, and
  it prints nothing as it runs. It is here only so that it can be
  installed at this exact version if something goes wrong. Do not
  install it otherwise - and note that BOTH builds answer
  XHCISNAP, so you do not need this one to send a report.

      xhci98.inf
      xhci98.sys   100,971 bytes
      SHA-256
      143D1EB920873341393F2C9AC43C645862A02A10E602429F3A72CC3403B3663C

  RELEASE-X64\  - INSTALL THIS ONE, on 64-bit Windows

  The normal driver. This is the one you want.

      xhci98.inf
      xhci98.sys   113,664 bytes
      SHA-256
      DB80A6D338AE2FAAFED0C3119C4536AF2FA3D71964F8372A818C6BAA5118DD4E

  DEBUG-X64\  - only when diagnosing a problem

  The same driver, built so that a crash on it can be traced
  further back. It records nothing more than RELEASE-X64\ does, and
  it prints nothing as it runs. It is here only so that it can be
  installed at this exact version if something goes wrong. Do not
  install it otherwise - and note that BOTH builds answer
  XHCISNAP, so you do not need this one to send a report.

      xhci98.inf
      xhci98.sys   211,456 bytes
      SHA-256
      AB7268D70C29080B18A6BC9F455E46AB00965BC1D06129FE9FEDA594E261ED47

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

Every driver binary in this download is called xhci98.sys and every one
carries driver version 1.2.0.0, so a copy taken out of its directory cannot
be identified by name or by version.
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

Every registry value this driver reads. There are six, and the driver
writes none of them: the installer creates all six.

  YOU SHOULD NOT NEED THIS SECTION FOR A LOG. If the maintainer asks for one,
  XHCISNAP -verbosity 2 sets the value that matters, on every controller, and
  finds the key itself. The first two values below are here so you can check
  what is in the key if you are asked to. The third, XhciImodInterval250ns,
  is the one setting here you may want to change yourself. The last three
  are the virtual High-Speed hub, which is EXPERIMENTAL and off unless you
  turn it on: ONLY USE IT IF YOU KNOW WHAT YOU ARE DOING.

  XhciLogVerbosity  -  the whole switch
  .....................................

  DWORD, default 0. Level 0 is off outright; above it each level is the one
  below plus one thing:

      0   OFF. The driver does not answer XHCISNAP at all - it replies
          exactly as a build without the channel would, which is deliberate
          and is why -probe cannot tell you which of the two you have. This
          is the default, so this is what a fresh install does.
      1   the channel, plus the counters. The log of what happened is still
          off, so this is the cheapest reading there is.
      2   plus the log of what happened.  USE THIS ONE.
      3   plus the USB port register table.  Still no internal addresses -
          the driver refuses to record one below level 4, so this is a
          property of what it wrote rather than a promise about what you
          are reading.
      4   plus everything, including internal addresses. Only if asked -
          it is more than you would want to paste in public.

  A value outside 0-4 is REFUSED rather than treated as the nearest one: the
  driver falls back to 0, which is off, so a mistyped level leaves the channel
  shut rather than opening it at some level nobody asked for.

  XhciLogDebugView  -  the stored log to a capture tool, when the driver stops
  ...........................................................................

  DWORD, default 0. Set it to 1 to have the log handed to DebugView when the
  driver stops. This is ONE DUMP AT THE STOP, not continuous output. It is
  useful on Windows 2000, where disabling the controller is a real stop with
  a capture program still running; on Windows 98 the only stop is the
  shutdown and Windows closes the capture first. It does not affect what
  XHCISNAP reads, which is a different route entirely. Leave it at 0 unless
  the maintainer asks for a DebugView capture.

  XhciImodInterval250ns  -  how long the controller holds back an interrupt
  .........................................................................

  DWORD, counted in UNITS OF 250 NANOSECONDS. It sets how long the
  controller waits after one interrupt before raising the next. A shorter
  interval makes USB mass storage faster at the cost of more interrupts.

      500    written by the install: 125 microseconds, at most 8,000
             interrupts a second.
      4000   used when the value is MISSING, UNREADABLE, OR OUTSIDE
             10-4000: 1 ms, at most 1,000 a second.
      10     the lowest accepted: 2.5 microseconds, at most 400,000 a
             second.

  A value outside 10-4000 is REPLACED BY 4000, not rounded to the nearest
  limit, so a mistyped 0 cannot turn moderation off. 4000 is the
  controller's own power-on value.

  ATTO Disk Benchmark with an MSSU10-128GSR flash drive at 500 (125
  microseconds), on a ThinkPad P14s Gen 1 under Windows 98 SE, gives about
  33 to 34.6 MB/s read and write from 64 KB transfers upward where the
  previous default 4000 gave about 18 MB/s.

  Linux's xHCI driver defaults to 160 (40 microseconds). This package ships
  500 to be more conservative since this is a generic driver.

  FEEL FREE TO TUNE IT. Lower towards 160 for the last few percent of
  storage speed, or raise it towards 4000 (or delete it) if you get audio
  stutter or instability under load. 500 may produce audio stuttering while
  a USB drive is being read at full speed, so if you want to prioritise
  audio over bandwidth, raise the value (section 7). Enter it as a decimal
  DWORD (500), or in hexadecimal (1f4) - Registry Editor lets you choose.
  The driver reads it when it starts, so a change takes effect after a
  restart; after the restart, XHCISNAP's report shows under "registry
  values" the value it read, the interval in force, and what the controller
  took.

  ON WINDOWS 98 WITH NUSB, AN UPGRADE DOES NOT SET IT. An upgrade over an
  existing install crashes before the value is written (see section 5), so
  the driver runs at 4000 until you set it by hand here - or take section
  5's "TO UPGRADE WITHOUT CRASHING" steps, which write it, even after an
  upgrade that has already crashed.

  XhciVirtualHSHub  -  the virtual High-Speed hub switch
  ......................................................

  EXPERIMENTAL AND OFF BY DEFAULT. Only use it if you know what you are
  doing.

  Every device plugged directly into a root port is reported to Windows as
  High Speed, because the USB stack this driver plugs into crashes the
  machine when a Full or Low Speed device is reported there at its true
  speed. That is what costs a mouse on a root port its polling rate, a
  Full-Speed audio device its sound from Windows XP on, and Windows Vista
  and 7 a crash behind a USB 1.1 hub (section 7). This switch puts a
  virtual USB 2.0 hub, answered by the driver itself, between the root port
  and the device, which is then reported at its true speed.

  DWORD:

      0   OFF, the default and what the install writes. Root ports are
          reported exactly as with no switch at all.
      1   ON DEMAND. When a Full or Low Speed device is plugged into a root
          port, a virtual hub appears above it, and it goes away when the
          device is unplugged. A High-Speed device gets no hub.
      2   ALWAYS ON. Every USB 2.0 port carries a virtual hub from start-up,
          plugged or not, and every device on a root port sits behind one,
          High Speed included.

  Any other value is REFUSED, not rounded: the driver applies 0.

  THE EXTRA HUB IS VISIBLE. At 1 a slower device brings a hub with it, a
  second entry in Device Manager that comes and goes with the device, and
  takes about two seconds longer to become usable. At 2 every USB 2.0 port
  carries one from start-up. On Windows 98 the first appearance of the hub
  on each port may run the Add New Hardware wizard once. The hub calls
  itself "xHCI98 virtual HS Hub".

  THE VIRTUAL HUB IS A HUB TIER. USB allows five hubs in a chain below a
  root port. With the switch at 1 or 2, a chain of external hubs on a root
  port can be one hub shorter than that before the devices at its end stop
  enumerating, because Windows counts the virtual hub as one of the five.
  At 1 only when the device on the root port is a Full-Speed (USB 1.1) hub,
  which is what puts that port in virtual-hub mode. Measured at 2 on
  Windows 2000, in a virtual machine: a mouse at the end of a chain of five
  hubs was never addressed, and the driver refused nothing.

  XhciVirtualHSHubVid and XhciVirtualHSHubPid  -  the virtual hub's id
  ....................................................................

  STRINGS (REG_SZ), not DWORDs: the hub's USB vendor and product id, as
  four hexadecimal digits each, either case, optionally prefixed 0x. The
  install writes "1209" and "0001": pid.codes' SHARED TEST ID 1209:0001,
  which pid.codes reserves for private testing and which is NOT an id
  allocated to this project. A vendor id of 0000 is refused.

  Change them only if another device's driver on the machine claims
  USB\VID_1209&PID_0001 and binds itself to the virtual hub. A new id is a
  new device to Windows, so it installs the hub again on each port. With
  the switch at 1 or 2, a missing or invalid id turns the virtual hub off
  for that start - the driver has no id of its own to fall back on. With
  the switch at 0 neither value is read.

  Set the three in Registry Editor - the switch as a DWORD, the ids as
  String Values - and restart: the driver reads them only when it starts.
  Reinstalling the package writes the install's values back, which turns
  the switch off. After the restart, XHCISNAP's report shows under
  "registry values" what the driver read, what it applied, and why it
  refused anything.

  Everything measured with it on was measured in virtual machines; the
  virtual hub has never run on real hardware. The project's
  docs/using/release-notes.md says what it was measured to change.

  THOSE SIX ARE THE WHOLE LIST. This driver reads no other setting of its
  own, and no registry value makes it write a file.

  FOUR ARE DWORDS AND TWO ARE STRINGS. The two log values and the virtual
  hub switch default to 0, the moderation interval to 500, the two ids to
  1209 and 0001, and all six are created by the installer, so they are
  already there and only their data changes. A value that is missing
  entirely is not an error either - the driver starts normally, with the log
  off, the interval at 4000 and the virtual hub off, and the report says
  whether it read nothing or read a value. They live in the device's own
  driver key, which is spelled one way on the NT targets and another on the
  9x ones:

    Windows 2000, XP, Vista and 7
      HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\
        {36FC9E60-C465-11CF-8056-444553540000}\0002

    Windows 98 SE and Windows ME
      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0002

  THE LAST PART OF THE PATH IS ASSIGNED BY THE MACHINE AND WILL NOT
  NECESSARILY BE 0002 ON YOURS. That is also why no ready-made .REG file
  ships here: a .REG file cannot name a key whose last part differs per
  machine.

  DO NOT IDENTIFY THAT KEY BY ITS DESCRIPTION, AND DO NOT ASSUME THERE IS
  ONLY ONE. If the controller has ever been enumerated at more than one PCI
  slot - the card was moved, or the machine's slots were re-ordered - there
  is one such key per slot, all carrying this driver's name, and two of them
  have been measured carrying an IDENTICAL DriverDesc.
  Values typed into a stale one are read by nothing, and the driver reports
  no error: it cannot tell "no such value" from "the key would not open".

  Ask the device itself which key it uses. Find your controller under

    Windows 98    HKEY_LOCAL_MACHINE\Enum\PCI
    Windows 2000  HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\PCI
    and later

  open the subkey for the slot it occupies, and read its Driver value. It
  names the key to edit - for example USB\0004 - and that is the key the
  driver will actually read, by definition.

  SET THE TWO LOG VALUES ONLY WHILE DIAGNOSING SOMETHING, AND RUN
  XHCISNAP -DISABLE WHEN YOU HAVE SENT THE CAPTURE. That is not
  housekeeping. While the channel is enabled, anyone using this machine can
  read the driver's own diagnostic state through it - counters, the log, the
  port table, and at level 4
  internal addresses. It is this driver's own state and nothing else: no
  documents, no passwords, no other program's memory. But this driver cannot
  put a lock on that door - the door belongs to Windows' own USB port driver,
  which opens it to anyone - so the value you just set IS the lock. On
  Windows 2000 and later you need administrator rights to set it (on Vista
  and 7, a Command Prompt started with "Run as administrator"); Windows 98
  has no such distinction.

  The driver keeps a 16 KB buffer whether or not you set anything; what
  XhciLogVerbosity 2 and above adds is a small amount of work each time
  something happens on the bus. Level 1 adds none of that and still lets
  XHCISNAP read the counters, which is why it exists.

  ON WINDOWS 98 THE SNAPSHOT ROUTE IS THE ONE THAT WORKS. XhciLogDebugView
  delivers nothing there, for the reason given above, and there is no
  driver-written log file. XhciLogVerbosity plus XHCISNAP is how a Windows 98
  machine produces a report - see section 6. On Windows 2000 both routes
  work.

  SLEEP
  .....

  THERE IS NOTHING TO SET HERE. The driver tells Windows, as it registers,
  never to put THIS controller to sleep. It is part of the driver and there
  is no registry value behind it. THE CONTROLLER NEVER IDLES, SO IT DRAWS
  SLIGHTLY MORE POWER, and there is no switch to turn it back on.


==============================================================================
 10. RELEASE HISTORY
==============================================================================

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
applies to xhci98.sys and xhci98.inf, which are this driver's own work.

No Microsoft file is in this download. The usbd.sys, usbhub.sys, usbui.dll
and (on Windows 2000 and XP) usbport.sys the install needs are copied by
Windows from your own Windows installation source (section 3); nothing here
grants you any right in them, and nothing here redistributes them.

The provenance record for everything the project depends on but does not own
is in docs/contributing/legal-provenance.md, in the project's source
repository rather than here.
