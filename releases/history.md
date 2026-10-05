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

## 2.1.1.0 - 2026-10-06

An interim update: the install now writes every registry value the driver
reads, the root hub's Power page shows a SuperSpeed device's current
correctly up to 510 mA, and on Intel 7-, 8- and 9-series chipsets the driver
moves the switchable connectors to itself. Every system `2.1.0.0` supports
is supported, from the same four directories. The registry values were
read in QEMU virtual machines; the Power page and the port switchover on
the owner's own machines, under Windows 98 SE; the release notes say what
was read on which system.

### What changed

- The install writes all eight registry values the driver reads, each at
  its default (`XhciImodInterval250ns` at `160`), so each is where a user
  looks for it, and writes each only where it is missing: a value you set
  survives an install or update. `2.1.0.0` wrote three, and wrote them over
  a value you had changed. A machine updated straight from `1.2.0.0` keeps
  that release's `XhciImodInterval250ns` of `500`; set it by hand. Read,
  before the eighth value was added, on Windows 98 SE and 2000 (a fresh
  install and an update) and 32-bit Windows 7 (an update) in virtual
  machines.
- Fixed: under `2.1.0.0` the Power page showed a SuperSpeed device's
  current as a quarter of its draw, on every system. It now shows the
  device's own figure up to 510 mA, and 510 mA for anything above; every
  other reader of the device's descriptor still gets the device's own
  bytes. A bus-powered hub is still charged (ports + 1) x 100 mA, at most
  500 mA, by the page itself. `XHCISNAP`'s report gives each SuperSpeed
  device's exact declared current (`dev.ss.maxpower.ma`, with
  `XhciLogVerbosity` raised). Read on a ThinkPad P14s Gen 1 under Windows
  98 SE: a drive declaring 896 mA, which read 224 mA, reads 510 mA.
- On Intel 7-, 8- and 9-series chipsets (Ivy Bridge to Broadwell) and
  C610/X99 - xHCI device ids `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` and
  `8D31` - the driver moves the switchable connectors, usually the blue
  ones, from the USB 2.0 controller to itself at each start and resume, and
  hands them back when its controller stops or the machine shuts down. With
  the firmware on Auto these connectors used to stay on the USB 2.0
  controller, where this driver saw nothing on them. A device on one of them
  under a running USB 2.0 driver is disconnected there and found again
  under this driver, and the reverse when the controller stops, so do not
  have a drive busy on a blue connector at those moments. The new value
  `XhciIntelPortSwitch` (`1`; `0` turns it off) is the opt-out. Read on the
  owner's Lenovo B490 (`1E31`) under Windows 98 SE with NUSB, firmware on
  Auto and Smart Auto; standby, the other five ids and the other systems are
  untested ground.
- Updating from `2.1.0.0`: the same steps as from `2.0.0.0` (the release
  notes, "Updating from 2.y.y.y"); the registry values already there are
  kept and the missing ones added.
- Known limitations: those of `2.1.0.0`, with the SuperSpeed Power-page
  quarter replaced by the 510 mA ceiling, plus the devices that move between
  controllers on Intel 7-, 8- and 9-series chipsets. The release notes have
  the full list.

## 2.1.0.0 - 2026-10-05

The first update of the host controller driver, a minor version because it
adds features: external hubs in Device Manager, devices known by their
serial numbers and named by their product names, a floppy for Windows 2000
and XP Setup and their Recovery Console, and an opt-in for polling Low- and
Full-Speed mice above 1000 Hz. It also fixes the Windows ME controller
re-enable and the root hub's Power page. Every system `2.0.0.0` supports is
supported, from the same four directories, each now holding `txtsetup.oem`
beside the two drivers. Its changes were read in QEMU virtual machines, on
the `2.1.0.0` code before the cut, and on no real hardware; the release
notes say what was read on which system.

### What changed

- External hubs appear in Device Manager as entries of their own, "xHCI98
  USB Hub", with the devices behind them beneath them, as on Microsoft's own
  USB stacks; a USB 3 hub appears twice, its SuperSpeed half as "xHCI98 USB
  3.x Hub". Each hub has a Power page, and USBView and Device Manager's view
  by connection can walk into it. A hub installs from the driver already
  installed: on Windows 98 SE and 2000 with nothing to answer, on Windows XP
  with the Found New Hardware wizard and the unsigned-driver warning for
  each newly plugged hub. Disabling and enabling an external hub in Device
  Manager brings back the devices behind it (read on 98 SE, 2000 and XP). The
  driver still runs every hub itself. Read in QEMU on its USB 1.1
  Full-Speed hub; a High-Speed hub's entry and the "xHCI98 USB 3.x Hub"
  were read by the owner on real hardware after the cut, and work.
- A device with a serial number is known by it, as Microsoft's hub driver
  knows it: moved to another port, it keeps its Device Manager entry and is
  not found again as new hardware, and two identical devices plugged into
  the same port in turn no longer share one entry. A device without a serial
  number is still known by its port.
- A device that none of Windows' own INFs names is named by its own product
  name, in Windows 98's Add New Hardware wizard and in Device Manager,
  instead of "USB Device"; where a Windows INF names the device, its name
  shows, as over Microsoft's own stack. On Windows 98 SE and ME a character
  outside plain ASCII shows as `?`.
- Fixed on Windows ME: re-enabling the controller in Device Manager with a
  USB mouse attached no longer makes Windows ME stop responding.
  The devices on the controller are now kept while it is disabled and come
  back as the same Device Manager entries when it is enabled again, as under
  Microsoft's own hub driver, instead of being removed and found again. Read
  with a mouse, a mouse and a stick, a USB keyboard alone, and devices
  behind a hub.
- Fixed: the root hub's Power page in Device Manager showed every device's
  power as unknown, on every system, because the driver refused the request
  Windows sends for it; it now shows each device's power in mA. The Advanced
  page's bandwidth figure counts only isochronous pipes in use, as
  Microsoft's own stack does, so a mouse, a keyboard or a drive adds nothing
  to it; on Windows Vista and 7 it stays at zero, and a SuperSpeed device's
  power reads a quarter of its draw, both known limitations.
- `txtsetup.oem` in every flavour directory, so that Windows 2000, XP and
  XP x64 text-mode Setup, and the Recovery Console of Windows 2000 and XP,
  can load the driver from a floppy at the F6 prompt on a machine whose
  keyboard or install medium is on an xHCI controller. Setup's own HID and
  storage drivers then run above this one. Limits: pressing F6 needs the
  firmware's own USB keyboard support, the floppy must be drive A:, a disk
  the driver runs as UAS is not usable until GUI-mode Setup, installing
  Windows onto a USB disk is not supported, and a USB drive left plugged in
  at the partition screen takes `C:`. When installing Windows XP and XP
  x64, GUI-mode Setup asks about the unsigned driver before the USB
  keyboard works, so a PS/2 keyboard or a laptop's built-in one is needed
  to answer it. In virtual machines Windows 2000 and XP text mode worked
  with a USB keyboard and stick, the Recovery Console of both logged in and
  ran commands with the USB keyboard alone, and XP and XP x64 installed
  this way to the desktop.
- The root hub's, and each hub's, first report of its devices waits up to
  5 seconds for the devices plugged in at start, because Windows 2000's
  text-mode Setup uses only the devices in that first report. Two new
  values, `XhciFirstEnumWaitMs` (default `5000`, `0` off, at most `30000`)
  and `XhciFirstEnumPortMs` (default `2000`), set it. In virtual machines
  the first report went 20 to 30 ms after the start with nothing plugged in
  and 0.3 to 0.9 s after it with devices plugged in.
- SweetLow's hidusbf sets a mouse's polling rate under this driver, up to
  1000 Hz for a Low- or Full-Speed device: read at 1000, 500 and 250 Hz at
  a root port and behind a hub on Windows 98 SE under NUSB 3.6 and on ME,
  at a root port on XP, and loading on a stock Windows 98 SE too, whose own
  `usbd.sys` has the routine it needs. It works behind a hub on XP as well
  (read on XP). Its setting must be applied again after an
  upgrade from `1.x.x.x`.
- `XhciFastPollFsLs`, a new value, off by default: at `2` or `3` a Low- or
  Full-Speed device on a root port that hidusbf sets to "31 Hz" or "62 Hz"
  is polled at 2000 and 4000 Hz, or 4000 and 8000 Hz. It is outside the
  xHCI specification and untested ground, read on no real controller and in
  no virtual machine; a controller that refuses it is caught and counted.
- Updating from `2.0.0.0`: install over it with Update Driver. On Windows
  98 SE and ME restart afterwards, although Windows does not ask: `2.0.0.0`
  keeps running until then. On Windows 2000 use Have Disk, since a search
  keeps `2.0.0.0`. The root hub's Driver tab keeps showing `2.0.0.0` until
  the root hub is updated too. Each device is found once more as new
  hardware, exactly once.
- Known limitations: those of `2.0.0.0`, less the Windows ME controller
  re-enable and the device with a serial number moved to another port,
  plus the XP F6 GUI-mode prompt that needs a PS/2 or built-in keyboard and
  the `?` in a non-ASCII name on Windows 98 SE and ME. The release notes
  have the full list.

## 2.0.0.0 - 2026-10-04

The driver is rewritten as a whole USB host controller driver. `xhci98.sys`
no longer plugs in underneath Windows' own USB port driver: it runs the
controller, the root hub, every hub and the splitting of composite devices
itself, and Windows' own class drivers sit on top of it unchanged. Because
nothing of the USB 2.0-era stack is left underneath it, it drives SuperSpeed
devices and hubs, and a second driver in the same package, `xhciuas.sys`,
runs UAS storage. Every system `1.2.0.0` supports is supported, from the same
four directories, each now holding both drivers.

### What changed

- SuperSpeed (USB 3.x, 5 Gbit/s) devices on the root ports, and SuperSpeed
  hubs. A link that trains faster (SuperSpeedPlus) is accepted at its trained
  rate, untested. Device Manager on these systems still shows a SuperSpeed
  device as High Speed at most; the interface it reads predates SuperSpeed.
- UAS storage, through `xhciuas.sys`, with streams at SuperSpeed and without
  at High Speed. A drive that offers both UAS and Bulk-Only gets UAS unless
  the new `XhciForceBulkOnly` value is set to `1`.
- Every device is reported to Windows at its true speed, on a root port and
  behind a hub. The virtual High-Speed hub of `1.2.0.0` is gone, and its
  three values (`XhciVirtualHSHub`, `XhciVirtualHSHubVid`,
  `XhciVirtualHSHubPid`) have no effect.
- Windows 98 SE needs no USB 2.0 stack for the controller, hubs, mice,
  keyboards and audio. USB storage there, UAS included, still needs NUSB's
  mass-storage component (NUSB 3.3 or 3.6, or its five storage files on their
  own); the release notes and the readme's section 3 have the details.
- The install writes an interrupt moderation interval of `160` (40
  microseconds) instead of `500`. On the one machine measured, a UAS drive
  at SuperSpeed lost 15 to 22% of its throughput at `500`.
- In Device Manager the controller is "xHCI98 USB 3.x eXtensible Host
  Controller" and the root hub "xHCI98 USB 3.x Root Hub", with every device
  beneath it; external hubs no longer appear as entries of their own.
- Gone under `2.0.0.0`: the Windows 2000 audio device unplugged during
  playback that was never fully removed, the Windows 7 controller disable
  that hung, the Windows 98 freeze on fast repeated plugging, and the Windows
  98 crash under NUSB when the controller was stopped - except on one path:
  updating in place over a running `1.2.0.0` under NUSB still crashes,
  because NUSB stops the old driver before the new one runs.
- Upgrading from `1.2.0.0`: on Windows 98 SE with NUSB, rename the old
  `XHCI98.SYS` and cold-boot before updating; on every system, pick the
  driver from a list with Have Disk rather than let Windows search. The
  readme's section 4 has the steps for each system.
- Known limitations: the driver never puts an idle device or hub port to
  sleep; Windows 98 SE can wedge when a USB audio device is plugged in soon
  after a cold boot, as it could under `1.2.0.0`; on Windows ME, re-enabling
  the controller with a USB mouse or keyboard attached hangs the machine; a
  device moved to a different port is found again as new hardware. The
  release notes have the full list and the untested ground.

## 1.2.0.0 - 2026-10-02

The driver gains an optional virtual USB 2.0 hub that lets a Full- or
Low-Speed device on a root port be reported to Windows at its true speed. It
is experimental and off by default: only use it if you know what you are
doing. With it off the driver reports root ports exactly as `1.1.1.0` does.
Every system `1.1.1.0` supports installs as it did, from the same four
directories.

### What changed

- The virtual High-Speed hub switch. `XhciVirtualHSHub`, a `DWORD` in the
  controller's driver key beside `XhciImodInterval250ns`, is `0` (off, what
  the install writes), `1` (a virtual hub appears above a Full- or Low-Speed
  device plugged into a root port, and goes with it) or `2` (every USB 2.0
  port carries one from start to stop). Behind it a mouse on a root port
  polls at the rate it asks for, a Full-Speed audio device on a root port
  plays from Windows XP on (the release notes say where it was read), and on
  Windows Vista and 7 a USB 1.1 hub on a root port no longer crashes the
  machine. The hub is one more entry in
  Device Manager and one more tier in a chain of hubs: with the switch on, a
  chain of external hubs can be one hub shorter than USB's five. Its id is
  pid.codes' shared test id `1209:0001`, set by two string values the install
  also writes, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`; a missing or
  invalid id turns the switch off for that start. Measured in virtual
  machines only; the virtual hub has never run on real hardware. The release
  notes' "The virtual High-Speed hub switch" and the readme's section 9 have
  the details.
- Fixed, whatever the switch is set to: a Low-Speed device behind a hub
  showed Code 10 under SweetLow's stack on Windows 98 at 250 Hz and faster
  (GitHub issue 4).
- Fixed, whatever the switch is set to: a High-Speed interrupt device behind
  a USB 2.0 hub, a mouse for example, could be polled every 125 microseconds
  rather than at the interval usbport sets for it (the one the device asks
  for, up to usbport's 4 ms limit). It worked, but kept the bus busier than
  it needed to. Read in virtual machines on Windows 98 SE under
  SweetLow's stack, ME, 2000, 32-bit XP, XP x64 and Vista in both
  architectures.
- Fixed on Windows Vista and 7: the driver could arm one of usbport's timers
  without the lock usbport expects, a race with usbport's own timer code on
  another processor. It was found by reading the code and never observed. A
  root-port change with no hardware event behind it can now take up to one
  health-poll interval longer to be seen. Earlier systems are unchanged.
- `XHCISNAP` reports what the driver read and applied for the three new
  values. Its snapshot format moved to schema 5 for that, so an `XHCISNAP`
  from an earlier release refuses this driver with "schema mismatch", and
  this one refuses an earlier driver. Use the copy in this package.
- Not changed with the switch off: every device on a root port is still
  reported to Windows as High Speed, and the known limitations `1.1.1.0`
  listed still apply. The release notes' "Known limitations" says which of
  them the switch addresses.

## 1.1.1.0 - 2026-09-24

The xHCI controller's properties in Device Manager gain an Advanced tab, and
the install sets a shorter interrupt moderation interval, which on the one
machine measured nearly doubled large reads from a USB stick. Every system
`1.1.0.0` supports installs as it did, from the same four directories.

### What changed

- The controller's Advanced tab. Its properties in Device Manager now carry
  the tab Windows' own USB controllers have: a "Disable USB error
  detection" checkbox and a "Bandwidth Usage" button. Both are Windows' own;
  the package adds one line to its INF naming the page. Read in virtual
  machines on every supported system: Windows 98 SE under NUSB 3.3 and under
  SweetLow's stack, Windows ME, Windows 2000, Windows XP in both
  architectures, and Windows Vista and Windows 7 in both. The bandwidth
  figures cost a Full-Speed device on a root port as a High-Speed one; the
  release notes' "Known limitations" say why.
- The interrupt moderation interval. `XhciImodInterval250ns`, a `DWORD` in
  the controller's driver key in units of 250 ns, sets how long the
  controller waits after one interrupt before raising the next. The install
  writes `500` (125 microseconds, at most 8,000 interrupts a second). A
  missing value, one the driver cannot read, or one outside `10` to `4000`
  means `4000` (1 ms, the controller's own power-on value and what every
  earlier release ran at); an out-of-range value is replaced, not rounded,
  so a mistyped `0` cannot turn moderation off. A lower value raises the
  interrupt rate. On a ThinkPad P14s Gen 1 under Windows 98 SE, ATTO Disk
  Benchmark read and wrote about 33 to 34.6 MB/s at `500` from 64 KB
  transfers upward, where `4000` gave about 18 MB/s. The value in force was
  read back from the controller in Windows 98 SE and Windows 2000 virtual
  machines. The release notes' "The interrupt moderation setting" and the
  readme's section 9 say where the key is and how to change it.
- On Windows 98 with NUSB, upgrading over an installed xhci98 still crashes
  that stack before the install's registry step, so an upgrade gets neither
  the tab nor the moderation value. The readme's section 5 has the route that
  delivers both, even after an upgrade that has already crashed; with
  SweetLow's stack an ordinary Update Driver is enough. Right-clicking
  `xhci98.inf` and choosing Install, which earlier readmes suggested, copies
  the files and writes no registry value at all.
- `XHCISNAP` reports the moderation value it read, the interval in force and
  what the controller took. Its snapshot format moved to schema 4 for that,
  so an `XHCISNAP` from an earlier release refuses this driver with "schema
  mismatch", and this one refuses an earlier driver. Use the copy in this
  package.
- Not changed: the polling rates of Full- and Low-Speed devices, and every
  device on a root port being reported to Windows as High Speed. The known
  limitations `1.1.0.0` listed all still apply.
- A known limitation, found on real hardware on 2026-09-23. On Windows 98, a USB audio device can stutter while a USB drive
  is read at full speed: on the P14s, a Full-Speed audio device on a root
  port stuttered from the 2048 KB reads of a disk benchmark onwards at `500`
  and at `1000`, and only on the last write at `4000`. It follows the
  doubled read speed. If audio matters more than read speed, raise the value
  towards `4000` or delete it. The readme's section 7 and the release notes'
  "Known limitations" have it.

## 1.1.0.0 - 2026-09-18

Windows Vista and Windows 7 join the targets supported in virtual machines,
in both architectures, and the download gains a 64-bit driver for them and
for Windows XP x64. The package no longer writes any machine-wide registry
value. Windows 98 SE, Windows ME, Windows 2000 and
32-bit Windows XP install as they did in `1.0.2.0`, from a directory with a
new name.

Every 64-bit, Vista and Windows 7 result below comes from virtual machines.
Of the new systems only 32-bit Windows 7 has run on real hardware, once, after
this release was cut (a ThinkPad E460, 2026-09-19): the install and devices at
a root port and behind USB 2.0 hubs worked, and the first disable of the
controller hung. Three limitations were found after the cut and are listed in
the last item below.

### What changed

- The download has four driver directories instead of two:
  `release-x86\` and `debug-x86\` hold the 32-bit driver, the one earlier
  releases carried in `release\` and `debug\`, and `release-x64\` and
  `debug-x64\` hold a separate 64-bit driver with an INF of its own. The
  32-bit driver does not install on a 64-bit Windows or the other way
  round; picking the wrong directory is harmless, Windows finds no driver
  there and says so.
- Windows XP x64 is supported through the
  64-bit driver, in virtual machines only. An XP Professional x64 SP2 guest
  whose only USB controller was the xHCI installed it with no prompt for
  media, and bound a HID mouse, a USB mass-storage device and a composite
  audio device; disable, enable, remove and rescan in Device Manager all
  survived.
- Windows Vista (SP2) and Windows 7 (SP1) are supported, 32-bit through the
  same driver as Windows 98 to XP and x64 through the 64-bit one, in virtual
  machines only. On each of four guests the package installed, the three
  devices above bound, and five disable and enable cycles, a remove and a
  rescan survived. **On Vista x64 and Windows 7 x64 the driver loads only
  while driver signature enforcement is disabled**, because it is not signed. Install it from
  Device Manager; right-clicking `xhci98.inf` and choosing Install does not
  work on these systems.
- The idle suspend fix no longer writes the registry. Until now the install
  set `DisableSelectiveSuspend = 1` under
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB`, which stopped
  every USB controller on the machine idling and outlived an uninstall. The
  driver now tells the USB stack itself, as it registers, that this
  controller must not be idled, which affects no other controller and
  leaves nothing behind. Read in virtual machines on all ten supported
  systems against a build that does idle. An upgrade leaves the old value
  in place on purpose; the release notes say how to delete it.
- The driver: a finished transfer is handed back to Windows' USB stack only
  in the context that stack expects, holding its lock for that endpoint.
  Handing it back from anywhere else could corrupt the stack's own lists on
  a machine with more than one processor; a four-processor XP x64 guest
  crashed from it, and the 32-bit USB stacks on every target were read to
  make the same assumption, so both drivers now do this. Read on a
  four-processor 32-bit XP guest, and on Windows 98, ME and 2000.
- The driver, from two audits: a device whose setup failed no longer keeps
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
- The tools: `XHCIQUAL`'s quick scan ends on its verdict; its legacy
  verdict can no longer print "NOT QUALIFIED" and "QUALIFIED (with
  warnings)" for one run; and its controller quirk table follows Linux's
  for ASMedia, NEC, VIA and Fresco. `XHCISNAP` finds the driver's settings
  on Windows Vista and Windows 7, refuses switch combinations it used to
  accept and then ignore, and deletes a report it could not finish
  writing.
- The download's `readme.txt` describes the current release only, and asks
  that `XHCISNAP` and the DebugView log be used only when the maintainer
  asks for them. The release notes add a Windows 2000 limitation: with a
  USB audio device attached, disabling the controller asks for a restart.
- Known limitations found after the cut, 2026-09-19, with the driver
  unchanged. On Windows Vista and Windows 7, 32-bit and x64, a USB 1.1 hub
  on a root port crashes the machine (`STOP 0x7E` in `USBPORT.SYS`) once a
  mouse, keyboard or other slower device is used behind it; use a root port
  directly, or a USB 2.0 hub. On Windows XP and later a Full-Speed USB audio
  device on a root port plays nothing, though Windows shows it playing;
  behind a hub it plays (on Vista and 7 a USB 2.0 hub), and Windows 2000
  plays on a root port. Both were measured in virtual machines, and the USB
  2.0 hub workaround on the one real 32-bit Windows 7 machine. On that same
  machine, disabling the controller in Device Manager hung, and so did the
  restart after it; the cause is not known yet. The readme's section 7 and
  the release notes' "Known limitations" have all three.

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
