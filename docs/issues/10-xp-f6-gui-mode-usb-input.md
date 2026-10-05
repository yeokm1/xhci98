# Issue 10 - Windows XP installed from the F6 floppy: the USB keyboard is dead in GUI-mode Setup while Setup asks about this driver's signature, because Setup copies Windows' own HID and USB helper files only with its own host controller drivers

Status: **a known limitation of `2.1.0.0`, not fixable from this
package.** Found on roadmap task 33.3's legs, 2026-10-04, on the first
package to carry `txtsetup.oem`. The release notes carry it beside the F6
section and in "Known limitations"; the README carries a row.

Targets affected: Windows XP SP3 (32-bit) and Windows XP x64 SP2, both
supported in virtual machines only; every reading here is a QEMU reading
(QEMU 11.1.0, TCG, development host A). Windows 2000 SP4 is not affected
in this way (section 4). Machines affected: those whose only keyboard is
USB on an xHCI controller - typically a desktop with no PS/2 port. A
laptop's built-in keyboard, connected inside the machine as PS/2 (the
ThinkPad E460 and P14s Gen 1 among them), and any PS/2 keyboard answer the
prompts.

## 1. Symptom

With `xhci98.sys` loaded at text-mode Setup's F6 prompt from the
`release-x86` (or `release-x64`) directory, text mode works: the USB
keyboard answers at Welcome and the licence, and a USB stick is listed at
the partition screen. After the text-mode reboot, GUI-mode Setup reaches
"Installing Devices" and shows three dialogs, in this order, each with the
focus on **No**:

1. "Software Installation ... has not passed Windows Logo testing", with a
   blank product name;
2. "Hardware Installation: xHCI98 USB 3.x eXtensible Host Controller";
3. "Hardware Installation: xHCI98 USB 3.x Root Hub".

The USB keyboard and mouse do nothing at that point. In QEMU the dialogs
were answered with the PS/2 keyboard (HMP `sendkey`), after which device
installation went on and the USB keyboard and mouse worked; both XP
installs then completed to the desktop with the controller, the root hub
and the USB stick working (step 7 of the F6 procedure, `xp32\`, `xp64\`).
On a machine with no PS/2 keyboard nothing can answer them, and Setup
stays there.

## 2. What the readings show

**The driver is running.** The QEMU xHCI trace at GUI-mode first boot shows
`xhci98.sys` addressing all three devices (descriptor reads), so the
controller and the root hub started from what text mode installed. There is
no SET_CONFIGURATION and no interrupt-IN transfer on the keyboard's slot
until "Installing Devices" has run.

**Text mode bound every device.** The `xp32c` target disk, paused at the
text-mode reboot and read offline (7-Zip and a registry hive reader):

- `Enum\XHCI98\ROOT_HUB` has `Service=xhci98`; the keyboard and tablet
  (`Enum\USB\VID_0627&PID_0001`) have `Service=hidusb`; the stick
  (`VID_46F4`) has `Service=usbstor`; each with `ConfigFlags` 0x400.
- `Services\hidusb` (Start 3), `usbstor` (3), `kbdhid` (1) and `mouhid`
  (1) exist; `Services\xhci98` is Start 0, Group "SCSI miniport".
- The CriticalDeviceDatabase holds only the i8042, `pci#cc_0604`, `gendisk`
  and `gencdrom` entries. None for USB is needed: the devnodes already name
  their services.

**Three files are missing.** `WINDOWS\system32\drivers` holds
`xhci98.sys`, `hidusb.sys`, `usbstor.sys`, `kbdhid.sys` and `mouhid.sys`,
but not `hidclass.sys`, `hidparse.sys` or `usbd.sys`. `hidusb.sys`
imports `hidclass.sys` and `usbd.sys`, so it cannot load. XP's
`TXTSETUP.SIF` lists those three files only in its own host controllers'
sections, `[files.usbohci]`, `[files.usbuhci]` and `[files.usbehci]`;
`[files.hidusb]` is `hidusb.sys` alone. Text mode loads them into memory
for its own use - which is why the keyboard works there - and copies them
to the target disk only when one of Microsoft's controllers was used. With
`xhci98.sys` as the controller nothing copies them.

**The causal check.** On a throwaway copy of that disk, only those three
files were expanded from the guest's own XP SP3 CD into
`system32\drivers`, with no registry, INF or policy change. At the next
GUI-mode boot the keyboard's interrupt-IN reports flowed before "Installing
Devices" (`xp32k\` trace), and the first unsigned-driver dialog was
answered with USB keystrokes alone (`xp32k\k01-usb-focus-yes.png`,
`k02-after-usb-enter.png`). The copy was deleted; no Microsoft file
entered the repository, `out\` or a package.

## 3. Why it cannot be fixed from this package

- The three files are Microsoft's. The package carries no Microsoft file
  (`legal-provenance.md` section 5; the packager's `PKG-MSFILE` gate).
- `txtsetup.oem` cannot reach the Windows source. `[Disks]` names an OEM
  disk by tag and directory with no drive, `[Files.*]` lines read from
  that disk (a familiar file name is still read from it), and `[Config.*]`
  writes values under the OEM service's key. No documented line makes an
  OEM component depend on an inbox one (Microsoft's `txtsetup.oem`
  reference; a Codex second opinion, 2026-10-04, found none either).
- The prompts are XP's "Warn" driver-signing policy meeting an unsigned
  driver; silencing them needs a signed driver (WHQL), which this project
  cannot obtain.
- An answer file on the F6 floppy does not help. With any `WINNT.SIF` on
  A: (tried: `[Data] MsDosInitiated=0` and `[Unattended]
  DriverSigningPolicy=Ignore`), `setupldr` ignores F6 altogether and Setup
  skips Welcome and the licence, so `xhci98.sys` is never loaded
  (`xp32s\`). An unattended install with `[MassStorageDrivers]`,
  `[OemBootFiles]` and `$OEM$\TEXTMODE` needs a rebuilt install CD, not the
  floppy.
- The firmware's USB keyboard emulation does not survive: `xhci98.sys` has
  taken the controller in text mode.

## 4. Windows 2000

Windows 2000 SP4's GUI-mode Setup showed no prompt (its default signing
policy), and its USB keyboard was live during "Installing Devices" (`w2kb\`
trace). Its target hive is barer still - no `hidusb`, `usbstor`, `kbdhid`
or `mouhid` service at all - and device installation installs them from the
CD unprompted. Windows 2000 has a different F6 problem, its text mode
binding only the devices of the first enumeration; that one is this
driver's to fix and is fixed for `2.1.0.0` (the lesson "Windows 2000
text-mode Setup binds only the USB devices of the first enumeration", in
`docs/contributing/lessons.md`).

## 5. What the floppy is still good for

Everything that stays in text mode is unaffected: on XP the USB keyboard
and stick work in text-mode Setup, and the F6 floppy serves the same text
mode that runs the Recovery Console and the repair install. Whether the
Recovery Console takes typing from the USB keyboard through `xhci98.sys`
is owed a reading on the `2.1.0.0` package (task 33.3); until it is read,
nothing says it does.

## 6. Rule kept

**Loaded in text mode is not copied.** What text-mode Setup can use and
what it leaves on the target disk are two lists; a bus driver that replaces
Microsoft's host controller driver inherits the first and loses the second.
Read the target disk at the text-mode reboot, not the text-mode screen,
before claiming a device works through GUI mode.

## Sources

- `out\phase33\f6\` (git-ignored, development host A): `xp32\`, `xp64\`,
  `xp32c\` (the offline hive and file listing), `xp32k\` (the causal
  check), `xp32s\` (the answer file), `w2kb\`, and `report.md`.
- Roadmap `roadmap-hcd.md` task 33.3; design record 13 section 5.6.
- Microsoft's `txtsetup.oem` reference: the `[Disks]`, `[Files.HwComponent.ID]`
  and `[Config.DriverKey]` sections (learn.microsoft.com, previous-versions
  driver documentation).
