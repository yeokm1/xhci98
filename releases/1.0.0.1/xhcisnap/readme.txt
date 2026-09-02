==============================================================================
 XHCISNAP - getting a report out of the xhci98 driver
==============================================================================

If USB is not working properly with this driver, this is what to run. It reads
the driver's own log straight out of the running machine and writes a report
you can paste into a bug report.

On Windows 98 it is the ONLY way to get anything out. That is not a gap in this
driver - it is the price of how it plugs into Windows. The usual ways a driver
writes a log are closed to it, and this route goes through the Microsoft USB
driver it sits underneath, which does have them.

It changes nothing about how the driver behaves on the bus, and writes no file
it was not asked to. It does READ the controller's port registers, which is a
hardware access - it just does not write one, and it deliberately leaves the
"something changed here" flags it finds standing, so it takes no evidence away
from the driver either.

What step 1 DOES change is ONE of this driver's own settings - that is the
point of it, and it is why step 2 is a restart. It prints it as it writes it,
and XHCISNAP -disable puts it back. Run that once you have sent the capture:
while it is on, anyone using this machine can read the driver's diagnostic
state. See the registry section for what that does and does not mean.


 THE FOUR STEPS
------------------------------------------------------------------------------

  1.  XHCISNAP -verbosity 2
  2.  restart the machine
  3.  make the problem happen again
  4.  XHCISNAP -o C:\MYDUMP

Then send C:\MYDUMP.TXT. Attach C:\MYDUMP.BIN as well if you are asked for it.

You never have to open REGEDIT. Step 1 does the whole of that for you, on every
xHCI controller the machine has.


 WHY STEP 1 IS NOT OPTIONAL
------------------------------------------------------------------------------

The driver answers nothing until it is asked to, and it reads that setting once
when it starts. So without step 1 and the restart this tool gets no answer at
all - which is right, not broken.

Level 2 is the one to use. The others exist and a maintainer may ask for one:

  0   OFF. The driver does not answer this tool at all. This is the default.
  1   answers, with counters only. The cheapest reading there is.
  2   plus the driver's own log of what happened. USE THIS ONE.
  3   plus the USB port register table.
  4   plus everything, including internal addresses. Only if asked - it is
      more than you would want to paste in public.


 WHAT THE THREE FILES ARE
------------------------------------------------------------------------------

  MYDUMP.TXT   The report. Plain text. Below level 4 it holds no internal
               addresses - the driver refuses to record one at those levels,
               so that is a property of the file and not a promise about it.
               This is the one to paste.
  MYDUMP.BIN   The driver's raw internal state. A maintainer can decode it
               against the exact build you are running; nobody else can. Send
               it as an attachment if asked.
  MYDUMP.PSC   The raw port register values, for the same audience.


 IF NOTHING COMES BACK
------------------------------------------------------------------------------

  XHCISNAP -probe

That checks whether the route to the driver works at all, separately from
whether this driver answers on it. If it says the request reached a driver and
that driver declined, the usual cause is simply that step 1 has not been done -
or that the machine has more than one USB controller and this is not the right
one, in which case try -c 1 and -c 2.

If it cannot open the device at all, no xHCI controller is started on this
machine, and there is nothing for this tool to read.


------------------------------------------------------------------------------
Part of the xhci98 1.0.0.1 release. See the readme.txt in the directory above
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file in the
directory above. XHCISNAP.EXE is not only this project's code: the Microsoft
Visual C++ 6.0 C runtime is statically linked into it, which is why it is one
file that runs on a machine with nothing installed on it. That linkage is
recorded in NOTICE.TXT beside this file.
