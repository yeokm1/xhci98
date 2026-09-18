# xhci98 - Release Notes

This file describes package version `1.1.0.0`
(`DriverVer=09/18/2026,1.1.0.0`), the fifth release. Where this file and
`docs/contributing/roadmap.md`, `docs/contributing/build-and-test.md` or
`xhciqual/README.md` disagree, the other document wins and this one is the
copy to fix.

---

## What this is

`xhci98.sys` is a USB host controller driver that gives Windows 98 SE,
Windows 2000 SP4, Windows ME and 32-bit Windows XP working USB on machines
whose only USB controller is xHCI. One binary serves all four, and the INF
carries both install paths (Windows ME reads the Windows 98 one, Windows XP
the Windows 2000 one). Since `1.1.0.0` the same binary also serves 32-bit
Windows Vista and Windows 7, through a third install path in the same INF.

Since `1.1.0.0` the download also carries a **64-bit** driver, for Windows XP
Professional x64, Windows Server 2003 x64, Windows Vista x64 and Windows 7
x64. That one is a separate build and a separate pair of directories with an
INF of its own; it is not the same file under another name, and the 32-bit
driver will not install on a 64-bit Windows or the other way round. Picking
the wrong one is harmless - Windows finds no driver in that directory and
says so. **On Vista x64 and Windows 7 x64 it loads only while driver signature
enforcement is disabled**; see below.

It is a miniport for `usbport.sys`, not a whole USB stack. It plugs in
underneath Microsoft's USB port driver the same way the in-box `usbehci.sys`
does. Everything above the controller (the root hub, enumeration, hubs, class
drivers) is the operating system's own code, unchanged.

What you get is USB 2.0: High-, Full- and Low-Speed devices, on the USB 2.0
protocol ports an xHCI controller exposes alongside its SuperSpeed ones. A
controller with no USB 2.0 protocol port at all is out of reach; see
"Requirements".

The two targets are not equally tested. Every Windows 98 result comes from
real machines as well as virtual ones. Every Windows 2000 result comes from
virtual machines only: Windows 2000 Setup bugchecks on both physical machines
this project has tried it on, so the driver has never run on Windows 2000 on
real silicon. If you have Windows 2000 SP4 running on an xHCI machine, the
driver is meant to work there and the install path is written for you, but
you would be the first. Windows ME stands where Windows 2000 does: supported
in virtual machines only, observed once (2026-09-02) under SweetLow's USB 2.0
stack, the only stack it is supported with, with the driver loading and a
HID mouse, a mass-storage device and a composite audio device binding. It
has never run on real hardware either. So does 32-bit Windows XP, since
`1.0.1.0`: supported in virtual machines only, observed in one QEMU guest (XP
Professional SP3, 2026-09-03) on which the package installed with the xHCI
alone and no prompt, the driver started under XP's own USB stack, a HID
mouse, a mass-storage device and a composite audio device bound, and the
disable, enable, remove and rescan sequence survived. It has never run on
real hardware.

Windows XP x64 stands there too, since `1.1.0.0`, and it is the thinnest
record of the five: supported in virtual machines only, observed in one QEMU
guest (XP Professional x64 SP2, 2026-09-09) on which the 64-bit package
installed with the xHCI alone and no prompt, the driver started under that
system's own USB stack, a HID mouse, a mass-storage device and a composite
audio device bound, and the disable, enable, remove and rescan sequence
survived. It has never run on real hardware. Windows Server 2003 x64 is the
same operating system as Windows XP x64 and the same driver is meant for it,
but no Server 2003 machine has been tried at all.

Windows Vista and Windows 7 stand there too, since `1.1.0.0`, in both
architectures: supported in virtual machines only, observed in four QEMU
guests (Vista Business SP2 and Windows 7 Professional SP1, each 32-bit and
64-bit, 2026-09-13) on which the package installed, the driver started under
that system's own USB stack, a HID mouse, a mass-storage device and a
composite audio device bound, and five disable and enable cycles, a remove
and a rescan survived. None of them has run on real hardware. These systems
expect a newer interface from a USB host controller driver than Windows 2000
and XP do, and the driver presents that interface on them and only on them.

**On Windows Vista x64 and Windows 7 x64 driver signature enforcement must be
disabled for the driver to load.** Those systems refuse to load a kernel-mode
driver that is not signed, and this one is not. On any start where enforcement
is in force, the driver does not load, the controller shows an error in Device
Manager (Code 39, on the Vista x64 machine where that boot was looked at), and
nothing plugged into it works inside Windows, a USB keyboard or mouse
included. 32-bit Windows Vista and Windows 7 do not refuse unsigned drivers,
and ask none of this.

## What this is not

- It is not USB 3.0. SuperSpeed is out of scope and unreachable: the USB
  2.0-era `usbport.sys` this driver reuses has no SuperSpeed path. The driver
  leaves USB 3.x ports unpowered, so a SuperSpeed-capable device trains on the
  USB 2.0 companion port of the same connector and runs at High-Speed. That is
  the intended behaviour. The same holds for USB4/Thunderbolt connectors,
  whose USB 2.0 side still terminates at an xHCI USB 2.0 port; on such
  machines it may be a different xHCI PCI function from the one exposing the
  SuperSpeed side. If the machine presents more than one unrecognised xHCI,
  install on the one the qualifier reports USB 2.0 protocol ports for.
- It is not signed. `xhci98.sys` carries no Authenticode signature. Windows 98
  SE does not check; Windows 2000 SP4 and Windows XP show an unsigned-driver
  warning during install and then install it (on XP, choose Continue Anyway),
  and Windows XP x64 installs it too, being the last 64-bit Windows that does
  not require kernel-mode drivers to be signed. 32-bit Windows Vista and
  Windows 7 install and load it too. Every 64-bit Windows
  from Vista onward requires the signature before it loads a driver: on Vista
  x64 and Windows 7 x64 this one loads only while driver signature
  enforcement is disabled, as "What this is" describes,
  and no 64-bit Windows after Windows 7 is supported.
- On Windows 98 it is not standalone. Windows 98 has no `usbport.sys` of its
  own. **A USB 2.0 stack must be installed first**, NUSB (the one this
  project tests against) or SweetLow's; it is what places `usbport.sys` and
  `usbhub20.sys`. Without one the driver will not load, with no useful
  diagnostic. NUSB 3.3 is the version this project tests against. NUSB 3.6
  carries the same USB 2.0 stack byte for byte and has been observed working
  with this driver (HID and mass storage, in a virtual machine only). A third
  option is SweetLow's USB 2.0 stack, the one Windows 98 QuickInstall 1.0.1
  and later bundle: it is built from the newer Windows XP lineage of the same
  port driver, and with it the disable, uninstall and upgrade crash listed
  under "Known limitations" does not occur. HID and mass storage have been
  run on it; it is not the tested configuration.
- It does not write to your disk. The driver creates no file. Its log is read
  out of the running driver by `XHCISNAP.EXE` when you ask for a report; see
  "The log, and how to send one".

## Requirements

| | |
|---|---|
| Operating system | Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows ME (4.90.3000), 32-bit Windows XP (SP3), Windows XP x64 / Server 2003 x64 (SP2), and Windows Vista (SP2) and Windows 7 (SP1) in both architectures, in virtual machines only, see "What this is". Vista x64 and Windows 7 x64 load the driver only while driver signature enforcement is disabled. Nothing after Windows 7. |
| USB stack | Windows 98: NUSB 3.3, installed before this driver (NUSB 3.6 ships the identical USB 2.0 stack and has been observed working, in a virtual machine only; so has the SweetLow stack that Windows 98 QuickInstall 1.0.1 and later bundle, which also removes the first known limitation below; see the README's installation steps). Windows ME: SweetLow's stack only; its own USB stack has no `usbport.sys`, and on it the driver installs and shows Code 2. Do not install NUSB on Windows ME, it is a Windows 98 SE package. Windows 2000: SP4's native stack, or the standalone USB 2.0 update KB319973. **Do not install NUSB on Windows 2000.** Windows XP, 32-bit or x64: its own USB stack, nothing to install; NUSB is not for it either. Windows Vista and Windows 7, either architecture: their own USB stack, nothing to install. |
| Controller | An xHCI controller presenting PCI class code `0C0330`, with at least one USB 2.0 protocol port, a BAR0 mapped below 4 GB, and a legacy interrupt pin. Neither target has an MSI path, so a controller reporting `Interrupt Pin = 0` cannot be driven at all. |
| Install media | Windows 98 SE on an xHCI-only machine: the Windows 98 SE installation CD at hand, or the Windows CABs on the hard disk (`C:\WINDOWS\OPTIONS\CABS`). The install copies Windows' own `usbd.sys`, `usbhub.sys` and `usbui.dll` from it. Windows ME: the same, from the Windows ME CD or the CABs its Setup leaves on the hard disk; the virtual machine tried asked for nothing. Windows XP: nothing; `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll` all come out of `sp3.cab` in the driver cache every install has. Windows XP x64: nothing either, and the guest asked for nothing; the same four come out of `Driver Cache\amd64`, `usbport.sys` and `usbhub.sys` from `sp2.cab` and `usbd.sys` and `usbui.dll` from `driver.cab` beside it. Windows 2000: nothing either; the same three out of `sp4.cab`, and `usbui.dll` out of `driver.cab` beside it in that cache. Windows Vista and Windows 7: nothing; every install already has all four files, and the package asks Windows to copy none of them there. |

Run the qualifier before installing anything; it answers all three of the
controller conditions in a single read-only pass.

## Before you install: check the machine

`XHCIQUAL.EXE` is a DOS tool that reads the machine's xHCI controller and says
whether this driver can work on it. Run it with no arguments for a read-only
quick scan. It writes no PCI configuration register and prints one of three
verdicts:

| Verdict | Means |
|---|---|
| `LOOKS QUALIFIED` | nothing a read-only pass can see disqualifies this machine; the active tests still decide |
| `DISQUALIFIED` | something a read-only pass genuinely sees: no `CC_0C0330` function, `Interrupt Pin = 0`, BAR0 unusable or above 4 GB, or no USB 2.0 protocol ports |
| `CANNOT SAY` | a state this pass may not change is in the way: the controller is not in D0, or Memory Space Enable is clear |

It is not on the driver install media itself, because it is a DOS executable
built by a different toolchain (Open Watcom), but the release download carries
it in its `xhciqual\` directory. Take it from there, or build it from the
`xhciqual/` directory (see `xhciqual/README.md`).

**It must be run from real DOS**, not a DOS box inside Windows, booted without
EMM386 or any other V86 or paging memory manager. `HIMEM.SYS` is allowed, and
on some machines it is needed: if the tool will not run at all on a boot that
loads nothing, add `DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` to `CONFIG.SYS`
(adjusting the path) and try again.

`xhciqual/hardware-testing.md` has the staged active tests, the safety notes,
and how to read each result.

## Installing

The package is a directory holding two files, `xhci98.inf` and
`xhci98.sys`, and no Microsoft file.

- Windows 98 SE: install NUSB 3.3 or the newer SweetLow stack first, your
  choice (README, installation steps). Then Device
  Manager -> the unrecognised xHCI device -> *Update Driver* -> *Specify a
  location* -> the package directory.
- Windows 2000 SP4 and Windows XP: Device Manager -> the unrecognised xHCI
  device -> *Update Driver* -> *Have Disk* -> the package directory. XP
  shows its unsigned-driver warning; choose *Continue Anyway*. If Windows
  2000's *Found New Hardware* wizard is used instead, it ends by asking for a
  restart; *No* is fine, the driver is already running.
- Windows ME: SweetLow's stack first, and only that one (NUSB is a Windows
  98 SE package): [usb20_win9x.zip](http://sweetlow.orgfree.com/download/usb20_win9x.zip)
  from SweetLow's site, unzipped; right-click the `USB2.INF` at its root,
  *Install*, reboot. Then the Windows 98 SE route above.
- Windows Vista and Windows 7: Device Manager -> the unrecognised xHCI
  device -> *Update Driver Software* -> *Browse my computer for driver
  software* -> the package directory. If Windows warns that the driver is not
  signed or its publisher cannot be verified, choose to install it anyway.
  Use Device Manager rather
  than right-clicking `xhci98.inf`: the right-click *Install* route is not
  supported on these systems. On 32-bit Vista it asks for `usbport.sys`,
  which you cannot supply, and cancelling ends it with no message and
  without installing the driver, although `xhci98.sys` is left in
  `System32\drivers`. On Vista x64 and Windows 7 x64 the install
  can be done on an ordinary boot, but the driver starts only while driver
  signature enforcement is disabled.

Four files the driver depends on are not in the package because they are
Windows' own: `usbd.sys`, which the USB 2.0 root hub imports on both
targets; `usbhub.sys`, the driver for composite devices on Windows 98 and
the hub driver on the NT systems; on Windows 2000 and XP,
`usbport.sys`, the
USB stack this driver plugs into (on Windows 98 NUSB or SweetLow's package
supplies it); and `usbui.dll`, the USB property-page DLL. Windows places its
USB files only when Setup finds a USB controller it recognises, and an
xHCI-only machine has none of them, so the INF asks Windows to copy each from
its own installation source, and only if it is absent; a machine that ever had
a USB controller Windows recognised keeps its own files and is asked for
nothing. Windows Vista and Windows 7 are different: every install of them has
all four files whether or not it ever saw a USB controller, and on those
systems the package asks Windows to copy none of them.

`usbui.dll` is new in this release and is the one that is purely cosmetic. On
Windows 2000 and Windows XP, Windows' own INF already asks for a Power tab on
the USB Root Hub's properties and names that DLL as the page's provider; on a
machine that never had a USB controller the file is missing, so the tab is
silently absent. Copying it back gives you the tab, showing the hub's power
budget and what is attached. On Windows 98 and Windows ME it changes nothing
visible: that page comes from `sysclass.dll` there, which those systems
already have.

On an xHCI-only Windows 98 machine that means an "Insert Disk" prompt naming
the Windows 98 Second Edition CD-ROM during the copy, unless the Windows
CABs are on the hard disk (OEM and Windows 98 QuickInstall installs). Insert
the CD and click OK; if it then asks where to copy from, give it the CD's
`WIN98` folder. If the prompt is cancelled the driver still installs, but the
USB 2.0 Root Hub sits at Code 2; that reads as a fault in this driver and is
not one. Put the CD in and install the driver again.

An upgrade from an earlier release can raise that prompt on a machine whose
previous install did not, and that is expected rather than a fault.
`usbui.dll` is new in `1.0.2.0`, so a Windows 98 or Windows ME machine that
already has `usbd.sys` and `usbhub.sys` from an earlier install may still not
have it. It sits on the same cabinet as those two, so the same CD answers it.

Windows 2000 and Windows XP take theirs from the driver cache
every install has and ask for nothing: on 32-bit Windows XP all four out of
`sp3.cab`, on Windows 2000 three out of `sp4.cab` and `usbui.dll` out of
`driver.cab` beside it, and on Windows XP x64 two out of `sp2.cab` and two out
of `driver.cab` in `Driver Cache\amd64`. Measured twice, each time on a
machine that had never had a USB controller: Windows 2000 on 2026-09-07 and
Windows XP x64 on 2026-09-09, and neither raised a prompt of any kind, so
there was nothing to cancel. Should the files be missing anyway, the failure
looks the same as the cancelled 9x prompt above, spelled as a `0xc0000034`
error naming `usbhub20.sys` rather than as Code 2.

`docs/contributing/build-and-test.md` has the full procedure, the recovery
rungs, and the bootstrap path for a machine that has no working USB until this
driver runs.

## The log, and how to send one

The driver keeps a small (16 KB) log of what happened on the bus, inside
itself, and `XHCISNAP.EXE` (in the download, beside `XHCIQUAL.EXE`) reads it
off the running machine. The log is off by default and stays off unless you
are diagnosing something.

```
  1.  XHCISNAP -verbosity 2
  2.  restart the machine
  3.  make the problem happen again
  4.  XHCISNAP -o C:\MYDUMP
```

Then send `C:\MYDUMP.TXT`, a plain-text report with no internal addresses in
it. `C:\MYDUMP.BIN` is written beside it: the driver's raw internal state,
which only a maintainer holding the exact build you are running can decode.
Attach it if you are asked for it.

Step 1 finds the right registry key for you, on every xHCI controller the
machine has; typing values into the wrong key by hand is the most likely
reason a log appears to do nothing. Step 2 is not optional: the driver reads
these settings once, when it starts.

The two values, both `DWORD`s in the device's driver (software) key, both
default `0`:

| Value | What it does |
|---|---|
| `XhciLogVerbosity` | `0` off (the driver does not answer `XHCISNAP` at all); `1` counters only; `2` adds the log of what happened (use this one); `3` adds the USB port register table; `4` everything, including internal addresses in the plain-text report, which a maintainer may ask for but which should not be pasted into a public issue unreviewed. (The raw `.BIN` file `XHCISNAP` writes carries internal addresses at any level from `1` up; it is only the `.TXT` that withholds them below `4`.) A value outside `0`-`4` is treated as `0`. |
| `XhciLogDebugView` | Set to `1` to hand the log to a debug-output capture tool (DebugView) when the device stops. Windows 2000 only in practice: on Windows 98 the capture tool is closed before the driver stops, so nothing is delivered there and `XHCISNAP` is the only route. |

If more happened than 16 KB holds, you get the most recent part and a line
saying how much was dropped. A problem that has already happened cannot be
captured after the fact, so an intermittent fault needs a second reproduction.
The log cannot capture a crash: a machine that has bugchecked is not running
for anything to read.

> **Run `XHCISNAP -disable` once you have sent the capture.** While the channel
> is on, anyone using the machine can read the driver's diagnostic state
> through it, including internal addresses in the raw dump at any level.

## DebugView

DebugView (Sysinternals, with *Capture Kernel* enabled) captures nothing from
this driver unless `XhciLogDebugView` is set, and then only one dump at a
Device Manager disable on Windows 2000. Windows 98 needs an old build (v4.64,
dated 2007; later versions do not run there), and it delivers nothing from
this driver either way.

> **Do not run DebugView on Windows 98 on real hardware while capturing this
> driver.** Plugging in a device crashed a ThinkPad E460 three times over with
> a development build, and no build of this release has been tested under
> DebugView on Windows 98 hardware.

## Known limitations

Each of these was measured, in a virtual machine unless it names a physical
machine. Several are defects in the USB stack this driver plugs into (Windows
98 with NUSB 3.3) rather than in this driver, established by reproducing the
same failure with Microsoft's own driver on the same machine; they are listed
because a user meets them through this driver.

- Windows 98: stopping a running USB host controller crashes the machine
  (`fatal exception 0E at 0028:C00312EE`, the same with Microsoft's own
  `usbehci.sys`), so disabling, uninstalling and upgrading this driver all
  crash it. An uninstall does not commit; an upgrade copies the new file but
  loses its registry phase. To remove the driver without a crash, rename
  `C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS` to `XHCI98.SAV` from an MS-DOS
  prompt, reboot, rename it back inside Windows without pressing *Refresh*,
  then use *Remove*. After an upgrade, right-click `xhci98.inf` -> *Install*
  to deliver the registry values the crashed phase did not. Windows 2000
  disables, re-enables, uninstalls and upgrades the same binary cleanly.
  The crash belongs to NUSB's `usbport.sys`, the Windows 2000 build: with
  SweetLow's XP-lineage build of the same stack (bundled in Windows 98
  QuickInstall 1.0.1 and later) the same Windows 98 system disables,
  re-enables, removes and reinstalls this driver without crashing.
- Windows 2000: installing a newer package over an older one is refused
  ("A suitable driver for this device is already installed") because the
  setup engine records no driver date for this unsigned package. Delete the
  cached `%SystemRoot%\inf\oemN.inf` and its `.pnf`, then install the new
  package; Setup picks it immediately.
- Windows 2000: disabling the controller in Device Manager while a USB audio
  device is attached and installed asks for a restart instead of applying.
  Say *Yes*, or unplug the audio device first and the disable applies at
  once. The refusal happens before anything reaches this driver: it has no
  transfer outstanding and is asked nothing, and two builds with different
  completion-delivery code behave the same. Which part of Windows holds the
  device is not known. Measured with QEMU's emulated USB audio device in a
  virtual machine on 2026-09-15/16; with mouse and storage devices alone the
  disable applies live, and Windows ME under SweetLow's stack, with the same
  audio device attached, disabled live in the same runs.
- Windows Vista x64 and Windows 7 x64: the driver is not signed, so it loads
  only while driver signature enforcement is disabled. On any other boot the
  driver is not loaded and nothing on the controller works
  (Device Manager showed Code 39 on Vista x64). See "What this is". Measured
  in virtual machines, 2026-09-10 to 2026-09-16.
- Windows 98: if the driver ever fails while starting the controller, the
  machine stops with `Windows protection error. You need to restart your
  computer.` (Windows 2000 simply reports Code 10.) Restart, press `F8`,
  choose Safe mode, put a working `XHCI98.SYS` back into
  `C:\WINDOWS\SYSTEM32\DRIVERS\` or remove the controller in Device Manager,
  then power-cycle. Recovery is complete and loses nothing.
- This controller never goes to sleep, so it draws slightly more power, and
  there is no way to turn that off. A sleeping xHCI controller cannot report
  a newly plugged device, and Windows otherwise idles it once nothing at all
  is on the bus: Windows 98 within a second, Windows XP within about half a
  minute of a start with nothing attached, 32-bit Windows 7 within about ten
  seconds of a start and again about half a minute after the last device is
  unplugged. (Any attached device keeps it awake, even one with no driver, so
  a laptop with internal USB devices never idles it and this changes nothing
  visible there. Windows Vista, 32-bit and x64, does not idle it on its own
  in five minutes, but the previous releases' controller halted within about
  five seconds of switching on "USB selective suspend setting" in the power
  plan, and this release's keeps running through that switch; 64-bit
  Windows 7 idles it before the desktop appears, and 64-bit Windows XP right
  after start. Windows 2000 SP4's own stack idles it only with a registry
  value set that nothing normally sets. All measured in virtual machines,
  2026-09-06, 2026-09-16 and 2026-09-17.) The
  driver tells Windows this as it registers, so **nothing outside the
  device's own settings is written and no other controller is affected**.
- **Upgrading from 1.0.0.0, 1.0.1.0 or 1.0.2.0: one machine-wide setting of
  theirs stays behind.** Those releases did the same job by writing
  `DisableSelectiveSuspend = 1` under
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB`. It sits outside
  the device's key, so neither an uninstall nor this upgrade removes it, and
  this package deliberately does not delete it - on a machine with more than
  one USB controller it may be doing a job for another one. It is harmless
  beside the new mechanism; both say the same thing. Delete the value in
  Registry Editor and restart if you want it gone. Why the change: it was
  machine-wide, so it also stopped every other USB controller idling, and it
  outlived the device that installed it. The obvious per-controller
  replacement was tried and does not hold - on Windows Vista the power plan's
  "USB selective suspend setting" rewrites it, and the Balanced plan enables
  that on battery by default, which would have put the fault back on any
  laptop that unplugged.
- Every device plugged directly into a root port is reported to Windows as
  High Speed, whatever it is; Device Manager and USB tools show it so. This
  is deliberate: the USB stack this driver plugs into crashes the machine
  when a Full or Low Speed device is reported at its true speed on a root
  port (it looks up a transaction translator that does not exist), so the
  driver keeps the real speed to itself and programs the controller with it,
  which is why such devices work. Two consequences. Windows sizes a Full or
  Low Speed device's interrupt polling interval on High-Speed rules, and the
  driver then raises it to the 1 ms minimum those speeds allow, so a mouse
  or keyboard on a root port polls in three bands: `bInterval` 1 to 4 at
  1 ms (1000 Hz), 5 at 2 ms (500 Hz), 6 and above at 4 ms (250 Hz), a stock
  mouse included. A polling-rate tool that changes `bInterval` within a band
  shows no effect and one that crosses a band does; nothing slower than 4 ms
  and nothing faster than 1 ms is reachable there. Devices behind a hub
  report their true speed and poll at the interval they ask for, so a mouse
  on a hub polls at its own 8 ms and a polling-rate tool works as on any
  controller. Measured in a virtual machine with SweetLow's hidusbf; the
  bands are documented in full in `docs/issues/06-full-speed-root-port-bugcheck.md`.
- Windows 98: plugging and unplugging a device very fast and repeatedly (one
  cycle every 0.6 s for minutes) can freeze the machine with no error. This
  one is this driver's own defect, with no explanation yet. Normal plugging
  and unplugging is fine. Also on Windows 98, unplugging a USB drive in the
  middle of a write and plugging it into a different port can freeze the USB
  layer until a restart (the same with Microsoft's EHCI driver); the same
  port is fine, and Windows 2000 handles it.
- On a controller that does not advertise Force Save Context (`FSC=0` in
  `XHCIQUAL xhci --probe-only`), waking from standby rebuilds the USB bus
  instead of restoring it: every device is dropped and found again, slower
  and visible but with nothing lost. The counter is `SavesDeclinedNoFsc`.
  A real standby and wake has not been run anywhere, and the other half
  of the same path is unobserved too: on a controller whose restore does
  succeed, the driver now restores the interrupt moderation it saved
  rather than leaving it at zero, and that has been read only through a
  host model, because the virtual machines fail every restore and rebuild
  the bus instead.
- Windows 98 shows no driver version on the Driver tab, only the file date;
  the four-part version is under *Driver File Details*.
- USB Audio on Windows 98 is uneven with the emulated device. The fresh-guest
  replug stopped on an installation-CD prompt in the 2026-08-30 run. Both
  arrival and replug subsequently passed in Phase 19 and in Phase 20's solo
  and paired runs with the audio group first. Earlier Phase 20 runs failed
  the replug without a prompt: the connect change was announced, but the
  stack never requested a port reset. Those failures depended on the VM
  run conditions; no driver defect was established. These later passes
  supersede the CD prompt as the latest result, not as a guarantee for every
  guest configuration. An older guest failed inside `USBAUDIO.VXD`; a
  physical UAC 1.0 device played clean on a ThinkPad E460, directly and behind
  a High-Speed hub. Roadmap task 19.8 and `docs/contributing/runs/run-20.md`
  retain the run details; `docs/contributing/lessons.md` has what the Phase 20
  failures were isolated to.
- Windows 98 on an xHCI-only machine: the driver install asks for the
  Windows 98 SE CD (an "Insert Disk" prompt naming the Windows 98 Second
  Edition CD-ROM) unless the Windows CABs are on the hard disk. That is
  Windows fetching its own `usbd.sys`, `usbhub.sys` and `usbui.dll`, which the
  package does not carry; see "Installing". An upgrade can raise it where the
  previous install did not, because `usbui.dll` is new in `1.0.2.0`. Cancelling the prompt leaves the USB 2.0
  Root Hub at Code 2 until the driver is installed again with the CD at
  hand. Measured on 2026-09-02 in a virtual machine with no CABs on disk.

## Licensing

This driver's own source is under the GNU General Public License, version 2
(`GPL-2.0-only`); see `LICENSE`. The full third-party material and provenance
record is `docs/contributing/legal-provenance.md`.

`xhci98.sys` and `xhci98.inf` are this project's own work, and they are the
whole package. The `usbd.sys`, `usbhub.sys` and `usbui.dll` the install needs,
and on Windows 2000 and Windows XP the `usbport.sys`, are Windows' own and are
copied by Windows from your own installation source; no Microsoft file is in
the download. (Release `1.0.0.0` carried the two
`usbd.sys` builds and Windows 98 SE's `usbhub.sys` under other names; that
was withdrawn before any upload. `docs/contributing/legal-provenance.md`
section 5 has the record.)
