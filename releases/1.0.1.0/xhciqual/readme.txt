==============================================================================
 XHCIQUAL - will this machine run the xhci98 driver?
==============================================================================

This is a small DOS program that looks at the machine's USB controller and
tells you whether the xhci98 driver can work on it. It is read-only: it takes
ownership of nothing and changes no setting.

Running it is optional. It is worth doing because one of the ways a machine
can be unsuitable CANNOT BE FIXED IN SOFTWARE, and this tells you before you
spend an afternoon installing an operating system.


HOW TO RUN IT
------------------------------------------------------------------------------

  1. Copy XHCIQUAL.EXE onto a DOS boot disk - a floppy, a CD, or a bootable
     USB key with plain MS-DOS or FreeDOS on it.

  2. Boot the machine from it.

  3. Type one command, with no arguments:

           XHCIQUAL

  4. Read the verdict on the last line.


>> IT MUST BE RUN FROM REAL DOS, NOT A DOS WINDOW INSIDE WINDOWS. <<

   Do not run it from a "MS-DOS Prompt" inside Windows 98, and not from
   CMD.EXE on Windows 2000 or later. The program talks to the controller
   hardware directly and needs memory it can address one-to-one. A DOS box
   inside Windows does not give it either of those, so the answers would be
   wrong or it would simply fail.

   For the same reason, boot without EMM386 or any other memory manager
   loaded. A bare boot disk is exactly right.

   HIMEM.SYS IS THE EXCEPTION, AND ON SOME MACHINES IT IS NEEDED. It is not
   a memory manager in the sense above - it does not put the processor into
   the mode that breaks this program - and this program runs in 32-bit mode
   through an embedded DOS extender, so it needs the extended memory HIMEM
   provides. If it will not run at all on a bare boot, add one line to
   CONFIG.SYS and try again:

         DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V

   Use whatever path HIMEM.SYS is actually at - C:\WINDOWS\ on a Windows 98
   machine, the root of the disk on a boot floppy. /M:1 fixes how it enables
   the A20 line instead of letting it guess, and /V makes it say at boot
   whether it loaded.


WHAT THE VERDICT MEANS
------------------------------------------------------------------------------

  LOOKS QUALIFIED
      Nothing this pass can see disqualifies the machine. Go ahead and
      install the driver.

      One thing it does not cover, on either verdict. This tool runs under
      DOS, so when it tests interrupt delivery it tests the old PIC path.
      That is exactly the path Windows 98 uses, and the one Windows 2000
      uses with a PIC HAL. Windows 2000 on a multi-core machine normally
      installs an APIC HAL instead, which routes interrupts a different way
      that DOS cannot exercise. So a pass here is proof for Windows 98, and
      strong but not complete evidence for Windows 2000. Nothing about the
      controller is being hidden from you - it is a limit of testing from
      DOS, and the same limit applies to every machine.

  DISQUALIFIED
      Something it can see rules the machine out. It will say which:

        - no xHCI controller present
        - NO LEGACY INTERRUPT PIN. This is the one that cannot be worked
          around. Neither Windows 98 nor Windows 2000 can use the modern
          interrupt mechanism such a controller would need.
        - the controller's memory window is unusable, or sits above 4 GB
        - no USB 2.0 ports on the controller

  CANNOT SAY
      Something the tool is not allowed to change is in the way: the
      controller is powered down, or its memory access is switched off.
      Look for xHCI, "USB 3.0 Mode", or Legacy USB settings in the BIOS,
      then run it again.


COMMAND LINE
------------------------------------------------------------------------------

The no-argument command above is the one to use, and it is the whole of what
most people need. The program does take arguments, and they are listed here
so that a log someone asks you for can be produced without guesswork.

  XHCIQUAL                          the read-only quick scan, one screen
  XHCIQUAL [xhci|ehci|ohci|all] [options]
  XHCIQUAL --scan TYPE [--scan TYPE ...] [options]
  XHCIQUAL --help

  Options may be written in any order, before or after a family word.


WHICH CONTROLLERS IT LOOKS AT

  With no family word it looks at all three. The driver only cares about
  xHCI; the other two are there because a machine's other controllers are
  part of the picture when something does not add up.

  xhci | ehci | ohci     one family only
  all                    all three - the default
  --xhci --ehci --ohci   the same three selectors, written as options
  --scan TYPE            the same again; repeat it to combine families


READ-ONLY OPTIONS - these change nothing on the machine

  --quick           the no-argument quick scan, asked for explicitly
  --probe-only      read-only discovery, fuller than --quick. It reads the
                    controller's memory window only if the firmware has
                    already switched it on, and switches nothing on itself
  --no-active       another name for --probe-only
  --no-page         do not stop at the end of each screenful
  --serial          mirror the output to COM1, 115200 8N1
  --log [FILE]      also write the report to a file, default XHCIQUAL.LOG.
                    A family word after --log is read as a selector, so
                    name the file explicitly if you pass both
  --done-flag FILE  create FILE only if the run finishes normally, so a
                    batch file can tell a crash apart from a bad verdict
  --help, -h, /?    a longer help text, printed by the program itself


ACTIVE OPTIONS - THESE TAKE OVER THE CONTROLLER

  Read A NOTE ON SAFETY below before using any of them. You do not need
  them to answer "will the driver work".

  --full            the full active run, across all three families
  --poll-only       active bring-up with no interrupt handler installed.
                    The mildest of these, and the one to try first
  --irq-selftest    xHCI only: an isolated, one-shot interrupt test
  --set-intel-ports try to route the Intel USB2 ports to xHCI and read the
                    result back. This one writes PCI configuration space
  --no-wait         do not wait 15 seconds for a device to be plugged in
  --no-devid        skip the xHCI device identification step


IF YOU ARE ASKED FOR A LOG

  This command reads everything the read-only path can see and leaves
  PROBE.LOG in the current directory. Nothing in it writes to the machine:

           XHCIQUAL --probe-only --no-page --log PROBE.LOG

  Send that file. Keep a note of the BIOS settings you had, and of any
  device that was plugged in.


WHAT IT RETURNS TO DOS

  0   the active tests passed
  1   not qualified, or a read-only run - which cannot pass tests it does
      not run, so 1 is the normal result of the commands above
  2   the command line was wrong, or no controller of that kind is here

  The verdict on screen is what to read. These are for batch files.


THE .MAP FILE
------------------------------------------------------------------------------

XHCIQUAL.MAP is not needed to run the tool, and you can ignore it. Keep it
beside XHCIQUAL.EXE anyway: if the program ever crashes with an address on
screen, that file is what turns the address back into a location in the
source, and it only matches THIS build of the EXE.


A NOTE ON SAFETY
------------------------------------------------------------------------------

The plain XHCIQUAL command only reads, and so do --quick and --probe-only:
they take ownership of nothing and write no PCI configuration register.

The active options are a different thing. They take ownership of the
controller, reset it, transfer data and reset ports, and a machine can stop
responding while they run. They are development instrumentation, listed
above because this one program contains them - not because installing the
driver involves them. The numbered batch files the project uses to drive
them in a fixed order are not included here.

If you run one anyway:

  - Boot real DOS with no memory manager, exactly as above - including the
    HIMEM.SYS exception, which the active options need just as much.
  - Use a PS/2 keyboard if the machine has one. A USB keyboard on the
    controller being tested can stop responding part-way through.
  - Do not boot the machine through that same controller, and do not write
    the log to a disk attached to it.
  - Unplug storage you care about. Ports get reset.

If the machine ever stops responding during a hardware test, power it off
completely and cold boot. Do not carry on from whatever state was left
behind.


------------------------------------------------------------------------------
Part of the xhci98 1.0.1.0 release. See the readme.txt in the directory above
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file in the
directory above. XHCIQUAL.EXE is not only this project's code: it is linked
as a DOS/32 Advanced DOS Extender executable and statically includes that
extender and the Open Watcom C runtime, which carry their own notices. Those
notices are in NOTICE.TXT beside this file, and this product uses DOS/32
Advanced DOS Extender technology.
