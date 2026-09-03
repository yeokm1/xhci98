==============================================================================
                              x h c i 9 8   1.0.1.0
    USB 2.0 for Windows 98 SE, ME, 2000 SP4 and XP on xHCI-only machines
==============================================================================

Released 2026-09-04.

Most x86 PCs made from around the mid 2010s onward have USB 3.0 (xHCI)
controllers and nothing else. Windows 98 SE, Windows ME and Windows 2000
have no support for those, and 32-bit Windows XP has it only for add-in
cards, never for the controller built into these machines. This driver
fills that gap.

It gives you USB 2.0 speeds: High Speed, Full Speed and Low Speed. USB 3.0
SuperSpeed is out of scope. A USB 3.0 device still works, at USB 2.0 speed,
through the same physical connector.


WHY ONLY USB 2.0, WHEN THE CONTROLLER IS A USB 3.0 ONE
------------------------------------------------------------------------------

The USB stack these systems already have - usbport.sys and everything above
it - does not support USB 3.0 at all. This driver is only the bottom layer, so
SuperSpeed would mean rewriting that whole stack on both systems: far more
work than this driver, for a speed that most machines running Windows 98 or
Windows 2000 could not make much use of anyway.

Nothing is lost but speed. Every USB 3.0 socket also carries the USB 2.0
wires, and the controller presents them as two separate ports; this driver
drives the USB 2.0 one, so a USB 3.0 device falls back to it and runs at High
Speed. USB4 and Thunderbolt sockets are no different: USB4 carries USB 3.0,
DisplayPort and PCIe through its tunnel but leaves USB 2.0 on the ordinary
wires, so those sockets still have a USB 2.0 port behind them. What can differ
on such a machine is which controller that port belongs to, so the machine may
show more than one unrecognised USB controller - install on the one XHCIQUAL
reports USB 2.0 ports for.


WHAT THE VERSION NUMBER MEANS, AND WHAT IT DOES NOT
------------------------------------------------------------------------------

It means the driver does what this file says it does and that its limits are
written down in section 7. It does not mean nothing is left. This is a hobby
driver for two operating systems that left support two decades ago, it has run
on a small number of machines, and BUGS ARE NOT UNEXPECTED.

Please report what you find, on the project's GitHub page:

      https://github.com/yeokm1/xhci98/issues

The form there asks for what a report needs. Section 7 says what is known
already, so read that first.


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. Check the machine first (optional, but recommended)
  2. What you need
  3. Check the media is complete
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

  READ-ONLY OPTIONS - these change nothing on the machine.

      --quick           the no-argument quick scan, asked for explicitly
      --probe-only      read-only discovery, fuller than --quick. It reads
                        the controller's memory window only if the firmware
                        has already switched it on, and switches nothing on
                        itself
      --no-active       another name for --probe-only
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

IF YOU ARE ASKED FOR A LOG, this one command reads everything the read-only
path can see, writes nothing to the machine, and leaves PROBE.LOG beside it:

      XHCIQUAL --probe-only --no-page --log PROBE.LOG

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
                     ME and 32-bit Windows XP (SP3) in virtual machines only
                     (neither has been run on a real machine).

  On Windows 98      NUSB 3.3e or the newer SweetLow USB 2.0 stack, your
                     choice, installed BEFORE this driver (section 4).

  On Windows ME      SweetLow's USB 2.0 stack, installed BEFORE this driver
                     (section 4). NOT NUSB: that is a Windows 98 SE package.
                     Windows ME's own USB stack has no usbport.sys, and on
                     it this driver installs and shows Code 2.

  On Windows 2000    SP4's own USB stack, or the standalone USB 2.0 update
                     KB319973. DO NOT install NUSB on Windows 2000.

  On Windows XP      XP's own USB stack; nothing to install. DO NOT install
                     NUSB on Windows XP.

  Controller         xHCI, PCI class code 0C0330, at least one USB 2.0 port,
                     a memory window below 4 GB, and a legacy interrupt pin.


==============================================================================
 3. THE FILES WINDOWS SUPPLIES
==============================================================================

xhci98.inf names two files of its own, xhci98.inf and xhci98.sys, and they
are in release\ and in debug\. Nothing else is in the package, and there is
nothing to complete: a copy taken from the project's source repository is
the same two files.

Three files the driver depends on are NOT in the package, because they are
Windows' own, unmodified, and this download redistributes nothing of
Microsoft's:

  usbd.sys     The USB 2.0 root hub imports it on both systems. Without it
               the USB ROOT HUB fails: Code 2 on Windows 98, error
               0xc0000034 naming usbhub20.sys on Windows 2000.

  usbhub.sys   On Windows 98, the driver for devices that are more than one
               thing at once - a sound card with a volume knob, a headset
               with buttons, a keyboard with media keys. Without it every
               such device stops at USB Composite Device with Code 2 and
               does nothing at all (under NUSB's stack; SweetLow's brings
               its own composite driver). On Windows 2000 it is the USB hub
               driver.

  usbport.sys  WINDOWS 2000 AND XP. The USB stack this driver plugs into.
               Without it the controller shows Code 39 and the driver never
               runs. On Windows 98 the USB 2.0 stack installed first (NUSB
               or SweetLow's) supplies it.

WINDOWS ONLY INSTALLS ITS USB FILES WHEN SETUP FINDS A USB CONTROLLER IT
RECOGNISES, and on an xHCI-only machine it never does, so on such a machine
none of them is there. The install in step 4 therefore asks Windows to copy
them from its own installation source. Each is copied only if it is absent,
so a machine that already has them - one that ever had a USB controller
Windows recognised - keeps its own files and is asked for nothing.

  WINDOWS 98 SE   HAVE THE WINDOWS 98 SE INSTALLATION CD AT HAND. Unless the
                  Windows CABs are on the hard disk (C:\WINDOWS\OPTIONS\CABS,
                  as on OEM and Windows 98 QuickInstall installs), the
                  install shows "Insert Disk" asking for the Windows 98
                  Second Edition CD-ROM: insert it and click OK, and if it
                  then asks where to copy from, give it the CD's WIN98
                  folder. It is asking for usbd.sys and usbhub.sys, not for
                  anything of this driver's.

  WINDOWS ME      The same as Windows 98 SE, with the Windows ME CD. The
                  machine tried (a virtual one) had the CABs on its hard
                  disk from its own Setup and asked for nothing.

  WINDOWS 2000    Nothing to do: all three come from the driver cache every
  AND XP          Windows 2000 or XP installation has (Driver Cache\i386).

If the prompt is cancelled the driver still installs, but the root hub fails
as described above. That reads as a fault in this driver and is not one: put
the CD in and install the driver again, or copy usbd.sys (and, on Windows 98
with NUSB, usbhub.sys) out of the CD's WIN98 CABs into
C:\WINDOWS\SYSTEM32\DRIVERS yourself.


==============================================================================
 4. INSTALL
==============================================================================

INSTALL FROM THE RELEASE\ DIRECTORY. This package carries BOTH builds side by
side - RELEASE\ and DEBUG\, each a complete set of files with the same names -
so the directory you point Windows at is what decides which driver you get.
RELEASE\ is the one you want. DEBUG\ is the same driver built so that a
maintainer can get more out of it if you are asked for a report, and there
only for troubleshooting a machine that has already gone wrong. It prints
nothing as it runs. Section 8 describes both, and nothing
about a copied file
says which one it is - so point at a directory, never at a loose xhci98.sys.

Put the whole unzipped package somewhere the machine can read - a floppy, a
CD, a shared folder - then:

  WINDOWS 98 SE
      A USB 2.0 stack (usbport.sys + usbhub20.sys) has to be there first:
      either NUSB 3.3e or the newer SweetLow stack, your choice.

        NUSB 3.3e - the configuration this driver is tested against.
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
      and point it at the RELEASE\ directory. During the copy, on a machine
      that never had a USB controller Windows recognised, "Insert Disk"
      asks for the Windows 98 Second Edition CD-ROM: that is Windows
      fetching its own usbd.sys and usbhub.sys (section 3). Insert it and
      click OK. Reboot when asked.

      (If Windows finds the controller for you first, the Add New Hardware
      Wizard asks the same question - give it RELEASE\ too.)

  WINDOWS ME
      SweetLow's stack has to be there first, and only that one: NUSB is a
      Windows 98 SE package and is not for Windows ME. Download
      http://sweetlow.orgfree.com/download/usb20_win9x.zip, unzip it,
      right-click the USB2.INF at its root, choose Install, and reboot.
      Then the same Device Manager route as Windows 98 SE:
          Properties -> Driver -> Update Driver -> Specify a location
      pointed at the RELEASE\ directory. Without the stack the driver
      installs and the controller shows Code 2. Windows ME has only been
      run in a virtual machine.

  WINDOWS 2000 SP4
      Open Device Manager and find the unrecognised xHCI controller, then
          Properties -> Driver -> Update Driver -> Have Disk
      and point it at the RELEASE\ directory. Nothing else is asked for;
      usbport.sys, usbd.sys and usbhub.sys come from the driver cache every
      installation has.

  WINDOWS XP (32-BIT)
      The same route as Windows 2000 SP4:
          Properties -> Driver -> Update Driver -> Have Disk
      pointed at the RELEASE\ directory; choose "Continue Anyway" at the
      unsigned-driver warning. Nothing else is asked for. Windows XP has
      only been run in a virtual machine.

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

  * WINDOWS 98 ONLY: INSTALLING CHANGES ONE MACHINE-WIDE SETTING. It writes
    DisableSelectiveSuspend = 1, which stops the USB stack putting the
    controller to sleep. Without it the controller sleeps within about half a
    second and cannot notice anything plugged in afterwards. It affects ANY
    USB controller in the machine, and uninstalling does NOT remove it. See
    section 9.

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
    install)     never written. See "after an upgrade" below.

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
  usbd.sys and usbhub.sys the install had Windows copy from its CD (section
  3), the setup engine's cached copy of xhci98.inf (under
  C:\WINDOWS\INF\OTHER) and the DisableSelectiveSuspend value of section 9
  all stay behind. Delete them by hand if you want them gone; the two Windows
  files are Windows' own and harmless where they are.

  AFTER AN UPGRADE ON WINDOWS 98, RUN THE INF ONCE BY HAND
  .......................................................

  Right-click xhci98.inf in the package directory and choose Install. That
  writes the machine-wide settings the crashed upgrade never reached -
  including DisableSelectiveSuspend, without which a device plugged in
  afterwards is not noticed. It touches no device, so it cannot hit the
  crash.


==============================================================================
 6. IF SOMETHING GOES WRONG
==============================================================================

  RUN XHCISNAP. FOUR STEPS, AND NONE OF THEM IS REGEDIT
  .....................................................

  XHCISNAP.EXE is in the XHCISNAP directory of this package. It reads the
  driver's own log straight out of the running machine and writes a report
  you can paste into a bug report. It works the same way on both systems,
  and on Windows 98 it is the ONLY way to get anything out.

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
  to open a file at all: three path spellings were tried and none of them
  produced a file on a real machine, on either system.

  DEBUGVIEW (Sysinternals), with "Capture Kernel" switched on, captures this
  driver's stop-time dump if XhciLogDebugView is set. Windows 2000 runs any
  current version. WINDOWS 98 NEEDS v4.64 - later versions do not run on it
  at all - and on Windows 98 it does not help anyway: the dump happens when
  the driver stops, the only stop on that system is the shutdown, and Windows
  closes the capture program before it gets there. Use XHCISNAP.

      !! Do not run DebugView on Windows 98 on real hardware while
         capturing. Plugging in a device while it captures crashes the
         machine - measured on three device classes. Inside a virtual
         machine it is fine.

         That was measured with earlier debug builds, which printed a line
         per event as they ran. NEITHER BUILD IN THIS PACKAGE PRINTS
         ANYTHING AS IT RUNS - and whether that makes DebugView safe on a
         Windows 98 machine has NOT been tested, because the one boot that
         would tell was never taken. Not tested is not cleared. Treat the
         warning as standing for both builds; you do not need DebugView to
         send a report.


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

  RELEASE\  - INSTALL THIS ONE

  The normal driver. This is the one you want.

      xhci98.inf
      xhci98.sys   82,539 bytes
      SHA-256
      F264EA2F09D0D7231C9A2D521E25B6AF2815AD07B1CF8298FD8A8E184503BC32

  DEBUG\  - only when diagnosing a problem

  The same driver, built so a maintainer can get more out of it.
  It prints nothing as it runs. It is here only so that it can be
  installed at this exact version if something goes wrong. Do not
  install it otherwise - and note that BOTH builds answer
  XHCISNAP, so you do not need this one to send a report.

      xhci98.inf
      xhci98.sys   83,147 bytes
      SHA-256
      27593B62650776CBB1548DF135C97AFC9E8FCF4DC1A579124B5CE90B6C1A8C2E

  XHCIQUAL\  - the DOS machine checker from step 1

      XHCIQUAL.EXE 115,644 bytes
      XHCIQUAL.MAP keep it beside the EXE; see xhciqual\readme.txt
      readme.txt
      NOTICE.TXT   third-party notices this EXE carries

  XHCISNAP\  - what to run if something goes wrong

      XHCISNAP.EXE 69,632 bytes
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

Both driver binaries are called xhci98.sys and both carry driver version
1.0.1.0, so a copy taken out of its directory cannot be identified by name
or by version. The one thing that tells them apart is the "debug" flag shown
on the Version tab of the file's properties.

(In Windows driver-kit terms, RELEASE is what the DDK calls a "free" build
and DEBUG is what it calls a "checked" build. This project says release and
debug throughout, in its build scripts and its documentation alike.)


==============================================================================
 9. REGISTRY SETTINGS
==============================================================================

Every registry value this driver reads or writes. There are three - two
the driver reads, and one the Windows 98 installer writes machine-wide.

  YOU SHOULD NOT NEED THIS SECTION. XHCISNAP -verbosity 2 sets the one that
  matters, on every controller, and finds the key itself. It is here so you
  can check what is in the key if you are asked to.

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
  XHCISNAP reads, which is a different route entirely.

  THOSE TWO ARE THE WHOLE LIST. This driver reads no other setting of its
  own, and no registry value makes it write a file.

  BOTH ARE DWORDS AND BOTH DEFAULT TO 0. Both are created by
  the installer, so both are already there and only their data changes.
  A value that is missing entirely is not an error either - the driver starts
  normally with everything off, and the report says whether it read nothing
  or read a zero. They live in the device's own driver key, which is spelled
  differently on the two systems:

    Windows 2000
      HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\
        {36FC9E60-C465-11CF-8056-444553540000}\0002

    Windows 98
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

  open the subkey for the slot it occupies, and read its Driver value. It
  names the key to edit - for example USB\0004 - and that is the key the
  driver will actually read, by definition.

  SET THEM ONLY WHILE DIAGNOSING SOMETHING, AND RUN XHCISNAP -DISABLE WHEN
  YOU HAVE SENT THE CAPTURE. That is not housekeeping. While the channel is
  enabled, anyone using this machine can read the driver's own diagnostic
  state through it - counters, the log, the port table, and at level 4
  internal addresses. It is this driver's own state and nothing else: no
  documents, no passwords, no other program's memory. But this driver cannot
  put a lock on that door - the door belongs to Windows' own USB port driver,
  which opens it to anyone - so the value you just set IS the lock. On
  Windows 2000 you need administrator rights to set it; Windows 98 has no
  such distinction.

  The driver keeps a 16 KB buffer whether or not you set anything; what
  XhciLogVerbosity 2 and above adds is a small amount of work each time
  something happens on the bus. Level 1 adds none of that and still lets
  XHCISNAP read the counters, which is why it exists.

  ON WINDOWS 98 THE SNAPSHOT ROUTE IS THE ONE THAT WORKS, and that is the
  whole of what changed in this version. XhciLogDebugView still delivers
  nothing there, for the reason given above, and the driver-written log file
  is gone. XhciLogVerbosity plus XHCISNAP is how a Windows 98 machine produces
  a report - see section 6. On Windows 2000 both routes work.

  DisableSelectiveSuspend  -  both systems
  ........................................

  DWORD, written as 1 by the install on both systems, in

      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB

  It stops the USB stack putting the controller to sleep, which is what makes
  hot-plug work without a Device Manager Refresh. Three things about it are
  deliberate, and none is hidden:

    * IT IS MACHINE-WIDE, not per-controller, so it also stops any OTHER USB
      controller idling. On the machines this driver exists for - where it is
      the whole USB stack - that is the intent.

    * THE CONTROLLER NEVER IDLES, SO IT DRAWS SLIGHTLY MORE POWER. That is
      the trade, and it is the same one Microsoft's own
      HcDisableSelectiveSuspend setting exists to let an administrator make.

    * UNINSTALLING DOES NOT REMOVE IT. It does not live with the device, so
      nothing takes it away. Delete it by hand and reboot if you want the
      previous behaviour back - this driver's own devices then go back to
      needing Refresh.

  Until 1.0.0.1 the Windows 2000 install withheld it, because that system's
  USB stack never idles this controller and the value would have changed
  nothing. Windows XP's stack does idle it, about half a minute after start,
  so since 1.0.1.0 the install writes it on Windows 2000 and XP as well as
  on Windows 98. On Windows 2000 it still changes nothing you can see; it is
  the same machine-wide setting, with the same three consequences.


==============================================================================
 10. RELEASE HISTORY
==============================================================================

  1.0.1.0 - 2026-09-04

  Windows XP joins the targets supported in virtual machines, the Windows 2000
  and Windows XP install now has the operating system supply every file the
  driver depends on, and one driver code change rides with them, for a fault
  the first XP guest showed. Windows 98 SE and Windows ME install as they did
  in 1.0.0.1.

  What changed

    * 32-bit Windows XP (SP3) is supported, in virtual machines only, the
      standing Windows ME has. On 2026-09-03 an XP guest whose only USB
      controller was the xHCI installed the package from its directory with no
      prompt for media, loaded the driver on the first boot under XP's own USB
      stack, and bound a HID mouse, a USB mass-storage device and a composite
      audio device; disable, enable, remove and rescan in Device Manager all
      survived. XP reads the INF's Windows 2000 half, shows its
      unsigned-driver warning (choose Continue Anyway) and asks for nothing
      else. NUSB is a Windows 98 SE package and is not for XP. Nothing has run
      on XP on real hardware.
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
      SweetLow's USB 2.0 stack only, the standing Windows 2000 has. On
      2026-09-02 a Windows ME guest loaded and started the driver and bound a
      HID mouse, a USB mass-storage device and a composite audio device. Its
      stock USB stack has no usbport.sys, so on a stock Windows ME machine the
      driver installs and shows Code 2 until SweetLow's stack is installed;
      NUSB is a Windows 98 SE package and is not for Windows ME. The INF is
      unchanged by this: Windows ME reads its Windows 98 half.
    * xhci98.sys is rebuilt only so that its version resource matches; no
      driver code changed between 1.0.0.0 and this release.

  1.0.0.0 - 2026-08-30

  Re-cut on 2026-08-30 under the same number, before anything had been
  uploaded, so there is no earlier 1.0.0.0 in anyone's hands to tell this one
  apart from. Between the first cut on 2026-08-29 and this one a repository
  audit found and fixed a set of driver defects, none of which had been seen
  on a machine: the PCI Bus Master restore now runs before the controller is
  declared initialised on resume; an all-ones register read (a controller that
  has dropped off the bus) is refused in every phase of a register wait rather
  than only the first; a failed control-endpoint quiesce no longer survives a
  device's re-enumeration; transfer events with codes the driver never asks
  for are refused and counted instead of acted on; a Command Ring Stopped
  event whose pointer sits on the ring's Link TRB is mapped to the right
  entry; a lost Enable Slot on a device that has already gone is abandoned
  instead of released twice; the resume-from-U3 pass writes U0 only to ports
  it actually resumed. The DOS qualifier's legacy-handoff writes now preserve
  the controller's reserved bits, and XHCISNAP refuses a snapshot whose
  declared size does not fit. The installer's own comments and every guide
  were corrected where they had drifted from the code. The release date moved
  with the cut, as it always does.

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

No Microsoft file is in this download. The usbd.sys, usbhub.sys and (on
Windows 2000) usbport.sys the install needs are copied by Windows from your
own Windows installation source (section 3); nothing here grants you any
right in them, and nothing here redistributes them.

The provenance record for everything the project depends on but does not own
is in docs/contributing/legal-provenance.md, in the project's source
repository rather than here.
