==============================================================================
 XHCISNAP - getting a report out of the xhci98 driver
==============================================================================

If USB is not working properly with this driver, this is what to run. It reads
the driver's own log straight out of the running machine and writes a report
you can attach to a bug report.

It talks to the driver directly, through the controller's own device, on
every Windows version this driver supports. Nothing else needs installing,
and no log file is written unless you ask for one.

It changes nothing about how the driver behaves on the bus, and writes no file
it was not asked to. It does READ the controller's port registers, which is a
hardware access - it just does not write one, and it deliberately leaves the
"something changed here" flags it finds standing, so it takes no evidence away
from the driver either.

What step 1 DOES change is ONE of this driver's own settings - that is the
point of it, and it is why step 2 is a restart. It prints it as it writes it,
and SNAPOFF.BAT (XHCISNAP -disable) puts it back. Run that once you have sent
the capture: while it is on, anyone using this machine can read the driver's
diagnostic state.


 THE FOUR STEPS
------------------------------------------------------------------------------

Three batch files beside XHCISNAP.EXE do the typing. Double-click them, or
run them at a prompt in this directory; on Windows Vista and 7, right-click
each one and choose Run as administrator. Copy this directory somewhere
writable first if it is not (a CD is not).

  1.  SNAPON.BAT     XHCISNAP -on (-verbosity 2): the log channel on
  2.  restart Windows
  3.  make the problem happen again
  4.  SNAPGET.BAT    XHCISNAP -report: the report and the dump

SNAPGET.BAT writes its files in the directory it runs in, not in C:\, under a
name made from the date and time, and prints the names. Attach the .DEV and
.TXT files to the bug report. Attach the .BIN and .PSC as well only if you
are asked for them. It leaves the channel on, so you can run it again after
the next attempt. At a prompt, SNAPON 3 sets level 3 rather than 2 (1 to 4;
SNAPON 0 is refused, SNAPOFF.BAT is off), and SNAPGET MYPC saves the files
as MYPC.DEV, .TXT, .BIN and .PSC (1 to 8 letters, digits, _ or -, not
starting with -, not a DOS device name such as CON, NUL, COM1 or LPT1, in
this directory; a name already taken is refused, never overwritten). The
readme.txt at the top of the download, section 6, says what each of
SNAPGET.BAT's endings means. When you are done:

  5.  SNAPOFF.BAT    XHCISNAP -disable: the channel off

You never have to open REGEDIT. Step 1 does the whole of that for you, on
every xHCI controller the machine has. Without the batch files, type the
commands they run; XHCISNAP -o NAME writes the dump alone under a name you
choose.


 WHY STEP 1 IS NOT OPTIONAL
------------------------------------------------------------------------------

The driver answers nothing until it is asked to, and it reads that setting
once when it starts. So without step 1 and the restart this tool gets no
answer at all - which is right, not broken.

Level 2 is the one to use. The others exist and a maintainer may ask for one:

  0   OFF. The driver does not answer this tool at all. This is the default.
  1   answers, with counters only. The cheapest reading there is.
  2   plus the driver's own log of what happened. USE THIS ONE.
  3   plus the USB port register table.
  4   plus everything, including internal addresses. Only if asked - it is
      more than you would want to paste in public.


 WHAT THE FILES ARE
------------------------------------------------------------------------------

Each name is mDDhhmmS: the month as one character (1 to 9, then A, B, C for
October to December), the day, the hour, the minute, and the first letter A
to Z not yet taken, so 3141022A is 14 March at 10:22 and a second run in the
same minute is 3141022B. Nothing is ever overwritten.

  3141022A.DEV The controller as Windows sees it: its Device Manager status
               and problem code, the resources it was given, and the versions
               and SHA-256 hashes of xhci98.sys, xhciuas.sys, xhcihlpr.sys
               (and xhcimsi.sys, the earlier helper, where it is still on the
               disk), usbd.sys and usbui.dll as installed. Attach it.
  3141022A.TXT The report. Plain text. Below level 4 it holds no internal
               addresses - the driver refuses to record one at those levels,
               so that is a property of the file and not a promise about it.
               Attach it.
  3141022A.BIN The driver's raw internal state. A maintainer can decode it
               against the exact build you are running; nobody else can. Send
               it as an attachment if asked.
  3141022A.PSC The raw port register values, for the same audience.

The report's header names the system it was taken on - the Windows
version, BIOS, processor and memory - and never a serial number or a
name. If reading those details causes a problem on your machine, type
XHCISNAP -nosysinfo -report instead of step 4; the header then says they
were skipped.


 IF NOTHING COMES BACK
------------------------------------------------------------------------------

  XHCISNAP -probe

That checks whether the route to the driver works at all, separately from
whether this driver answers on it. If it says the request reached a driver and
that driver declined, the usual cause is simply that step 1 has not been
done - or that the machine has more than one USB controller and this is not
the right one, in which case try -c 1 and -c 2.

If it cannot open the device at all, no xHCI controller is started on this
machine, and there is nothing for this tool to read.


------------------------------------------------------------------------------
Part of the xhci98 3.0.0.0 release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file two
directories up. XHCISNAP.EXE is not only this project's code: the Microsoft
Visual C++ 6.0 C runtime is statically linked into it, which is why it is one
file that runs on a machine with nothing installed on it. That linkage is
recorded in NOTICE.TXT beside this file.
