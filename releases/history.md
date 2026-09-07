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

## 1.0.2.0 - 2026-09-07

A fix release. An audit found no critical defect and nineteen things worth
fixing across the driver, the two tools and the package. All of them are
closed (`docs/contributing/roadmap.md`, Phase 20).

The install changes in one way: Windows now supplies `usbui.dll` as well,
which brings back the USB Root Hub's Power tab on Windows 2000 and Windows
XP. Everything else about it is as `1.0.1.0` left it.

The device matrix on Windows 98 SE and Windows 2000 was re-read on this
driver and is no worse than `1.0.1.0`'s. One change here has no machine
behind it, the control-endpoint refusal below: nothing has been built to
produce that state on purpose, and a test on the development machine is what
covers it.

### What changed

- The driver: an endpoint handle the hub driver has already replaced can no
  longer act on the endpoint that replaced it, and a device's endpoint table
  is reset under the lock the endpoint callbacks take. Read on a two-CPU
  Windows 2000 guest under Driver Verifier, the controller killed from
  outside the guest four times and back each time with every device, and on
  the Windows XP sequence `1.0.1.0`'s fix was for.
- The driver: a device this driver has given up on can no longer have its
  control endpoint opened, or reopened after the failure. Covered by a host
  test; no machine has shown it.
- The driver: after an in-place controller recovery the health poll's fatal
  latch reopens, so a second fault is recovered too. Until now only the
  first after boot was.
- The driver: a recovery whose delivery is lost no longer stays armed for
  ever. It ages out after twenty health polls, counts as one of the bounded
  attempts, and a late delivery from the expired request is ignored.
- The driver, three smaller ones: a Command Ring Stopped event still naming
  the abandoned command is resolved with a No Op rather than by adopting
  that command's own entry; the BIOS handoff write preserves the reserved
  bits of `USBLEGCTLSTS`; and the restore from standby puts back the
  interrupt moderation value it saved instead of zero. The last is
  host-model only, since the virtual machines fail every restore.
- The tools: `XHCISNAP` exits nonzero on a report it could not finish
  writing instead of calling it written, and refuses a snapshot whose
  extension size does not match the driver's. `XHCIQUAL`'s EHCI clean-up no
  longer writes the controller's write-one-to-clear status bits back.
- The install: Windows supplies `usbui.dll` too, from its own installation
  source and only if the file is absent, by the same rule as `usbd.sys` and
  `usbhub.sys`. On Windows 2000 and Windows XP that brings back the USB Root
  Hub's Power tab, showing the hub's power budget and what is attached:
  those systems' own installer asks for that page and names this file as its
  provider, so on a machine that never had a USB controller it was silently
  missing. On Windows 98 and Windows ME nothing you can see changes.
  Upgrading a Windows 98 or Windows ME machine may ask for the Windows CD
  where the last install did not, because this file is new here; it sits on
  the same cabinet as the other two, so the same CD answers it. No Microsoft
  file is in the download.
- The download: `readme.txt` and `LICENSE` no longer describe Microsoft
  files it stopped carrying in `1.0.0.1`, and the "Windows 2000 never idles
  this controller" statement carries the measurement that qualified it
  (`1.0.1.0`'s correction below). The checks that produce the download were
  tightened; its layout is unchanged.

## 1.0.1.0 - 2026-09-04

Windows XP joins the targets supported in virtual machines, the Windows 2000
and Windows XP install now has the operating system supply every file the
driver depends on, and one driver code change rides with them, for a fault
the first XP guest showed. Windows 98 SE and Windows ME install as they did
in `1.0.0.1`.

### What changed

- 32-bit Windows XP (SP3) is supported, in virtual machines only, the
  standing Windows ME has. An XP guest whose only USB controller was the
  xHCI installed the package from its directory with no prompt for media,
  loaded the driver on the first boot under XP's own USB stack, and bound a
  HID mouse, a USB mass-storage device and a composite audio device;
  disable, enable, remove and rescan in Device Manager all survived. XP
  reads the INF's Windows 2000 half, shows its unsigned-driver warning
  (choose Continue Anyway) and asks for nothing else. NUSB is a Windows 98
  SE package and is not for XP. Nothing has run on XP on real hardware.
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
- Correction: the `DisableSelectiveSuspend` entry above says Windows 2000's
  USB stack never idles this controller and the value changes nothing there.
  That was generalised from the Phase 3 spike's observation window and was
  never measured; the owner's checks contradict it. Whether and when Windows
  2000 idles the controller is unestablished until a reading is recorded
  (roadmap Phase 20, F18). The value is written on every install path
  regardless, and that is unchanged.

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
  SweetLow's USB 2.0 stack only, the standing Windows 2000 has. A Windows ME
  guest loaded and started the driver and bound a HID mouse, a USB
  mass-storage device and a composite audio device. Its stock USB stack has
  no `usbport.sys`, so on a stock Windows ME machine the driver installs and
  shows Code 2 until SweetLow's stack is installed; NUSB is a Windows 98 SE
  package and is not for Windows ME. The INF is unchanged by this: Windows
  ME reads its Windows 98 half.
- `xhci98.sys` is rebuilt only so that its version resource matches; no
  driver code changed between `1.0.0.0` and this release.

## 1.0.0.0 - 2026-08-30

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
