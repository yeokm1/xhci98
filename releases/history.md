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

## 1.0.1.0 - 2026-09-02

Windows XP joins the targets supported in virtual machines, the Windows 2000
and Windows XP install now has the operating system supply every file the
driver depends on, and one driver code change rides with them, for a fault
the first XP guest showed. Windows 98 SE and Windows ME install as they did
in `1.0.0.1`.

### What changed

- 32-bit Windows XP (SP3) is supported, in virtual machines only, the
  standing Windows ME has. On 2026-09-03 an XP guest whose only USB
  controller was the xHCI installed the package from its directory with no
  prompt for media, loaded the driver on the first boot under XP's own USB
  stack, and bound a HID mouse, a USB mass-storage device and a composite
  audio device; disable, enable, remove and rescan in Device Manager all
  survived. XP reads the INF's Windows 2000 half, shows its unsigned-driver
  warning (choose Continue Anyway) and asks for nothing else. NUSB is a
  Windows 98 SE package and is not for XP. Nothing has run on XP on real
  hardware.
- Windows 2000 and Windows XP: `usbport.sys`, the USB stack this driver
  plugs into, now comes from the operating system's own driver cache
  (`sp4.cab`, `sp3.cab`), the way `usbd.sys` already did, and `usbhub.sys`
  with it; the install asks for no media. Windows Setup places none of the
  three unless it finds a USB controller it recognises, so a Windows 2000
  or XP machine that has never had another USB controller has none of them
  on its disk. Until this release the package's NT install named only
  `usbd.sys`, and on such a machine the driver installed but could not load
  (Code 39 on XP). A machine that ever had a USB 1.1 or 2.0 controller
  already has the files and sees no difference.
- Windows 2000 and Windows XP: the install now writes
  `DisableSelectiveSuspend = 1` under
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB`, as the Windows
  98 install has since `1.0.0.0`. XP's USB stack idles a
  controller with nothing attached about half a minute after start, and a
  sleeping xHCI cannot report a newly plugged device; Windows 2000's never
  idles this controller, so there the value changes nothing you can see. It
  is a machine-wide setting, and an uninstall does not remove it; the
  release notes' "Known limitations" say what it does to other controllers.
- The driver: when the hub driver re-creates a device in the middle of its
  enumeration through a second device handle and then removes the first, as
  Windows XP does on the first attach of a mass-storage or composite
  device, the removal of the superseded handle's control endpoint is no
  longer taken for the live one closing. Before this release such a device
  failed on its first attach on XP and worked when unplugged and plugged in
  again (`docs/issues/04-xp-restore-device-ep0-remove.md`). Windows 98 SE
  and Windows 2000 never provoke it and read unchanged on the same binary.
## 1.0.0.1 - 2026-09-02

The driver is unchanged. This release changes how it is installed: the
package no longer carries any Microsoft file, and the two Windows files the
driver depends on come from Windows itself.

### What changed

- `1.0.0.0` shipped `usbd98.sys`, `usbd2k.sys` and `usbhub98.sys` beside the
  driver: Windows 98 SE's and Windows 2000 SP4's own `usbd.sys` and Windows
  98 SE's own `usbhub.sys`, because Windows only places its USB files when
  Setup finds a USB controller it recognises and an xHCI-only machine has
  none of them. The INF now asks Windows to copy those files from its own
  installation source instead (the `LayoutFile` directive Windows' own INFs
  use), still without overwriting a file that is already there. The download
  is this project's two files per flavour, the tools and the readmes.
- What you see: on an xHCI-only Windows 98 SE machine the install asks for
  the Windows 98 Second Edition CD-ROM ("Insert Disk") unless the Windows
  CABs are on the hard disk, as on OEM and Windows 98 QuickInstall installs.
  Have the CD at hand; `readme.txt` section 3 says what is being fetched and
  what happens if the prompt is cancelled. A machine that ever had a USB 1.1
  controller already has the files and is not asked. Windows 2000 asks for
  nothing.
- Windows ME is a supported target, in virtual machines only and under
  SweetLow's USB 2.0 stack only, the standing Windows 2000 has. On
  2026-09-02 a Windows ME guest loaded and started the driver and bound a
  HID mouse, a USB mass-storage device and a composite audio device. Its
  stock USB stack has no `usbport.sys`, so on a stock Windows ME machine the
  driver installs and shows Code 2 until SweetLow's stack is installed;
  NUSB is a Windows 98 SE package and is not for Windows ME. The INF is
  unchanged by this: Windows ME reads its Windows 98 half.
- `xhci98.sys` is rebuilt only so that its version resource matches; no
  driver code changed between `1.0.0.0` and this release.

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
