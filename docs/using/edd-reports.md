# Request for reports: how does your BIOS name a USB stick?

On Windows 98 SE and ME, a USB stick left plugged in when the computer is
switched on can keep some computers from reaching the desktop once xHCI98
has taken over the USB controller (README, "Known limitations/issues").
To find a fix that works on many computers, we need to know how different
BIOSes describe a USB stick to MS-DOS. XHCIQUAL's `--edd` mode reads that
description. It only reads: it writes nothing but the log file you name,
and it stops before it touches a USB controller.

We would like reports from as many BIOS makers as we can get (AMI,
Insyde, Phoenix, Award and others), on laptops and desktops from different
makers. Reports where the stick is **not** named, or where the tool stops,
help as much as reports where everything works.

## What you need

- `XHCIQUAL.EXE` from xHCI98 3.4.2.0 or later, in the package's
  `TOOLS\XHCIQUAL\` folder.
- A USB stick. Its contents do not matter; nothing is written to it.
- A computer that can start MS-DOS: Windows 98 SE's "Command prompt only",
  or an MS-DOS boot disk or boot stick.

## Run it

Run it from pure MS-DOS only, never in an MS-DOS Prompt window inside
Windows (the tool refuses there).

1. Copy `XHCIQUAL.EXE` to the computer's own disk (for example `C:\`),
   not to the USB stick.
2. Shut the computer down completely. Plug the USB stick in.
3. Switch the computer on. On Windows 98 SE, hold Ctrl (or press F8) while
   it starts and choose **Command prompt only** (not "Restart in MS-DOS
   mode"). On Windows ME, or with no Windows, start from an MS-DOS boot
   disk. Do not load EMM386.
4. Type:

       C:\XHCIQUAL --edd --log C:\EDD.LOG

   Press a key at each `-- More --`.
5. Shut the computer down. Unplug the USB stick.
6. Switch on again, as in step 3, and type:

       C:\XHCIQUAL --edd --log C:\EDD0.LOG

If the computer stops while the tool runs, the last line on the screen
names the disk and the step it was on. Take a photo of the screen and send
it in place of the log.

## What to send

- The computer's maker and model, and its BIOS version (shown at the top
  of the log, or in the BIOS setup screen). The BIOS maker too (AMI,
  Insyde, Phoenix, Award, ...) if you know it.
- Both files, `C:\EDD.LOG` (stick in) and `C:\EDD0.LOG` (stick out), or a photo
  if the tool stopped.
- Whether the BIOS setup has a setting for USB storage or "legacy USB",
  and how it was set.
- On Windows 98 SE or ME with xHCI98 installed: does the computer stop or
  wait a long time at start-up when the stick is plugged in at power-on?
  Roughly how long, and does it reach the desktop in the end?
- On a computer where Windows 98 SE or ME is not installed, say so; the
  first part is still useful.

The logs contain no serial numbers: the tool leaves out the part of the
reply that could hold the stick's serial number. They do contain the
computer's model, BIOS version, processor and memory size; remove
anything you do not want to share.

Please attach everything to a [new issue](https://github.com/yeokm1/xhci98/issues/new/choose)
titled "EDD report: <maker> <model>".
