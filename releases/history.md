# Release history

One entry per version, newest first, written for the person installing the
driver rather than for the person who built it.

`scripts\package\make-release.ps1` requires an entry for the version being cut
and embeds everything from the first `##` heading down into that release's own
`readme.txt`. So a release cannot be published without a changelog entry, and
every published directory carries the history up to and including itself.

(`readme.txt`, not `README.md`: the generated guide is plain text at 78
columns because it is read on the target machine, in Windows 98 Notepad or DOS
EDIT, where a `.md` file renders as nothing and its markup is just noise.)

## 1.0.0.0 - 2026-08-30

Re-cut on 2026-08-30 under the same number, before anything had been
uploaded, so there is no earlier `1.0.0.0` in anyone's hands to tell this one
apart from. Between the first cut on 2026-08-29 and this one a repository
audit found and fixed a set of driver defects, none of which had been seen
on a machine: the PCI Bus Master restore now runs before the controller is
declared initialised on resume; an all-ones register read (a controller that
has dropped off the bus) is refused in every phase of a register wait rather
than only the first; a failed control-endpoint quiesce no longer survives a
device's re-enumeration; transfer events with codes the driver never asks for
are refused and counted instead of acted on; a Command Ring Stopped event
whose pointer sits on the ring's Link TRB is mapped to the right entry; a
lost Enable Slot on a device that has already gone is abandoned instead of
released twice; the resume-from-U3 pass writes U0 only to ports it actually
resumed. The DOS qualifier's legacy-handoff writes now preserve the
controller's reserved bits, and `XHCISNAP` refuses a snapshot whose declared
size does not fit. The installer's own comments and every guide were
corrected where they had drifted from the code. The release date moved with
the cut, as it always does.

The first release. There is nothing before it to compare against: the builds
this project cut while the work was going on were numbered `0.x`, none was
uploaded anywhere or given to anyone, and they are gone. If you are holding a
copy of this driver, this is the version of it.

### What it is

`xhci98.sys` is a USB host controller driver for xHCI (USB 3.0) controllers on
Windows 98 SE and Windows 2000 SP4. It gives those systems working USB on a
machine whose only USB controller is xHCI, which is what most x86 PCs built
from around the mid 2010s onward have. One binary serves both systems, and
the installer carries an install path for each.

What you get is USB 2.0: High-, Full- and Low-Speed devices, on the USB 2.0
ports an xHCI controller exposes alongside its SuperSpeed ones. SuperSpeed is
out of scope, so a USB 3.0 device trains at High Speed rather than not
connecting at all. Keyboards, mice, flash drives, USB Ethernet adapters, hubs
with devices behind them and USB audio have all run through it.

On Windows 98 it is not standalone. NUSB 3.3 has to be installed first, since
that is what puts Microsoft's USB port driver on the machine; the driver plugs
in underneath it rather than replacing it. Windows 2000 SP4 already has its
own.

### What is in the download

- `release\` and `debug\`, the same driver built two ways. Install from
  `release\`. `debug\` is there for diagnosing a machine that misbehaves, and
  it is the same version, so the two are kept in the directories they arrived
  in rather than copied together.
- `XHCIQUAL.EXE`, a DOS tool that answers "will this driver work on this
  machine" before anything is installed. Run it first; one of the ways a
  machine can fail cannot be fixed in software, and finding that out takes
  thirty seconds.
- `XHCISNAP.EXE`, which reads the driver's own log off a running machine and
  writes a report you can send. On Windows 98 it is the only route there is:
  the usual kernel capture tool crashes that system on real hardware.
- `readme.txt`, a standalone install and usage guide that assumes you have the
  directory and nothing else, and `LICENSE`.
- The three Microsoft files the installer needs and an xHCI-only machine has
  never been given: Windows 98's and Windows 2000's own `usbd.sys`, and
  Windows 98's `usbhub.sys`, which is what multi-function devices bind
  through. Each is copied without overwriting a file you already have.

### What 1.0.0.0 claims, and what it does not

Final means the driver does what this project says it does and that its limits
are written down, not that nothing is left to do.

On Windows 98 SE the driver is validated on real hardware behaviourally:
devices enumerate, work, and survive being unplugged, on a physical machine
rather than only in an emulator. What it is not on that target is
continuously instrumented. There is no running trace to be had on Windows 98
on real hardware and no way to capture anything from a crash, so a machine
that goes down takes what the driver was holding with it. What can be had is a
report on demand, with `XHCISNAP.EXE`, after the fact.

On Windows 2000 SP4 every result this project has comes from a virtual
machine. Windows 2000 has never run on real hardware here: Setup bugchecks
during installation on both machines it was tried on, a ThinkPad E460 and a
ThinkPad P14s Gen 1, and no other candidate machine is available. Nothing
about this driver caused that, since it never got as far as loading. If you
already run Windows 2000 SP4 on a machine with an xHCI controller, the install
path is written for you and you would be the first to walk it.

Every xHCI controller this project has ever read is an Intel one, in those two
laptops. No AMD controller has been tried.

The known limitations are published rather than summarised. Several of them
are faults in the USB stack this driver plugs into rather than in the driver,
and each says how that was established. Two matter enough to name here:
stopping this driver in Device Manager crashes Windows 98, which makes
disabling, uninstalling and upgrading it on that system cost a crash; and
plugging a device in and out repeatedly, several times a second for minutes,
can freeze Windows 98, which is this driver's own defect and has no
explanation yet. The release notes (`docs/using/release-notes.md`, "Known
limitations", which section 7 of `readme.txt` points at) have the full list,
with what was measured and on which machine.
