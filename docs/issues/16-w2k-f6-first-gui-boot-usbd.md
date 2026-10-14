# Issue 16 - Windows 2000 installed from the F6 floppy stops at the first GUI-mode boot with STOP c000026c, because text mode lays down hidusb.sys and usbstor.sys without usbd.sys, hidclass.sys and hidparse.sys

Status: **fixed in `2.4.0.0` (roadmap-hcd 38.final) by a second F6 entry
for Windows 2000, "xHCI98 USB 3.x Host Controller (32-bit Windows 2000,
files from CD)", which copies three files the user puts on the F6 floppy
from their own Windows 2000 CD.** Read in QEMU only (QEMU 11.1.0, TCG,
development host A), on Windows 2000 SP4, through text mode and GUI-mode
Setup to the desktop and one ordinary restart. Real hardware is unread.
The first entry, "(32-bit Windows 2000/XP)", still stops on Windows 2000
as described here, and the release notes keep that as a known limitation.

Targets affected: Windows 2000 (read on SP4) installed through the F6
floppy on a machine whose USB is on an xHCI controller, when a USB
keyboard, mouse or storage device on that controller was used in text
mode. Releases affected: every release with F6 support (from `2.1.0.0`)
as far as read - `2.3.1.0` and `2.4.0.0` were read with the first entry.
Windows XP is not affected in this way: its text mode leaves the same
files out, and its first start goes on without them; the USB input
problem it has in GUI-mode Setup is issue 10. An install with no USB
keyboard, mouse or storage device on the controller during text mode (a
hub and an audio device only) went into GUI-mode Setup with no stop.

## 1. Symptom

Text mode works through `xhci98.sys`: the USB keyboard answers from F6 on,
behind a hub too, and a USB stick is listed at the partition screen. At
the text-mode restart, the first start of the new system (the start of
GUI-mode Setup) stops with:

    STOP: c000026c {Unable to Load Device Driver}
    \SystemRoot\System32\Drivers\hidusb.SYS device driver could not be
    loaded. Error Status was 0xc0000034

for a USB keyboard or mouse, and the same naming `usbstor.SYS` for a USB
stick with no keyboard or mouse (runtime: `out\p38-matrix\legs\f6w2k\`,
shots `g01`, `c1-18`, `c2-08`, `y12`, `b08`, `k09`). Both stops are
older than `2.4.0.0`: each was read on the published `2.3.1.0` with the
same devices on a blank disk, `hidusb.SYS` with a keyboard, a mouse and
an audio device behind a hub (`c1-18`) and `usbstor.SYS` with a stick at
a root port and an audio device behind a hub (`k09`, control `c3`).

## 2. Root cause

Read on the disk at the text-mode restart (runtime, 7-Zip and a registry
hive reader on the paused target disk; `drivers-c2-textmode.txt`,
`files-textmode-4.txt`, `files-4b.txt`): `system32\drivers` holds
`xhci98.sys`, `hidusb.sys`, `kbdhid.sys`, `mouhid.sys` and `usbstor.sys`,
and no `usbd.sys`, `hidclass.sys` or `hidparse.sys`. `hidusb.sys` imports
`HIDCLASS.SYS` and `USBD.SYS`, `usbstor.sys` imports `USBD.SYS`, and the
kernel names the importer with `0xc0000034` when an import is missing.

Why (static, the SP4 CD's `I386\TXTSETUP.SIF`, a text read): text mode
binds the class drivers through its `[HardwareIdsDatabase]` and copies
their files, but lists `hidclass.sys`, `hidparse.sys` and `usbd.sys` only
in `[files.openhci]` and `[files.uhcd]`, the file sections of Microsoft's
own USB controller drivers, which an xHCI-only machine never uses. Text
mode itself has the three loaded for its own use, which is why the
keyboard works there. It is the same gap issue 10 found on XP; on Windows
2000 the boot stops instead of going on.

## 3. What was tried

All on 2026-10-14, Windows 2000 SP4 in QEMU, the `2.4.0.0` dry cut's
`release-x86` with only `txtsetup.oem` changed unless named (runs 61-66
in `out\p38-matrix\legs\f6w2k\notes.txt`; the reading of the binaries is
in `docs/contributing/legal-provenance.md` section 4).

- **The Windows CD as a second `[Disks]` entry** (`d2 = "Windows 2000
  Professional CD", \CDROM_NT.5, \i386`). Static: text mode reads every
  file of an F6 option from `\device\floppy0`; its disk prompt looks for
  the disk's tag file there and otherwise shows "Insert the disk labeled:
  ... into drive A:" with ENTER and F3 only. Runtime (`x1b-06` to `-08`):
  that prompt, with the CD in the CD drive, ESC doing nothing and ENTER
  asking again. A dead end.
- **`driver` lines without a service key.** `setupldr` refused the option
  ("File caused an unexpected error (0) at line 1273 in ...\oemdisk.c",
  `x1-03`): its parser requires the key for `driver`, `port` and `class`
  lines. With keys, `setupdd` makes a boot-start service of each, which
  `[Config.<key>]` can set to `Start` 3.
- **The three files on the floppy itself**, copied from the CD's `I386`
  as `USBD.SY_`, `HIDCLASS.SY_` and `HIDPARSE.SY_`, a second disk `d2`
  tagged `\usbd.sy_` at the floppy root: no prompt, the files expanded into
  `system32\drivers` (SHA-256 equal to `expand.exe`'s output), the first
  start went on, with a hub, keyboard and mouse and with a stick alone
  (`x2a-g05`, `x2b-g03`). This is the fix's form.
- **A slipstream** (an `[InputDevicesSupport]` entry for `xhci98` with its
  own file section in a copy of the CD's `TXTSETUP.SIF`): text mode did not
  load the driver (runs 71 and 72), and it was not pursued. The release
  notes say what a slipstream must carry and that none was carried
  through.

## 4. The fix, and its limits

The 32-bit `txtsetup.oem` has a second option, `XHCI98W2K`: the first
option's lines and ids, then `driver = d2, usbd.sys, usbd`,
`driver = d2, hidclass.sys, hidclass` and
`driver = d2, hidparse.sys, hidparse`, disk `d2` tagged `\usbd.sy_` at the
floppy root, and `[Config.usbd]`, `[Config.hidclass]` and
`[Config.hidparse]` each `value = "", Start, REG_DWORD, 3`. The release
notes' "Installing Windows 2000 from the F6 floppy" tells the user to copy
the three compressed files from the CD's `I386` folder to the floppy root
and to pick that entry. The gate's `OEM-W2K` rule holds the shape
(`scripts/inf-gate/check-txtsetup-oem.ps1`); design record 13 section 5.6,
limit 7, has it as built.

Limits:

- The user must copy the three files; they come from the CD being
  installed (read with the SP4 CD's).
- Without them, the Windows 2000 entry stops text mode at the file copy
  with "Insert the disk labeled: USBD.SY_ HIDCLASS.SY_ HIDPARSE.SY_ from
  the 32-bit Windows 2000 CD into drive A:" and F3 or ENTER only; a floppy
  holding them put in drive A: and ENTER let the copy go on (run 85).
- The entry is for Windows 2000 only: on XP its disk prompt would have no
  way past it. XP keeps the first entry, unchanged.
- Windows 2000 keeps three service entries, `usbd`, `hidclass` and
  `hidparse`, at `Start` 3 (group `SCSI miniport`), which nothing starts;
  the kernel loads the files as `hidusb.sys`'s and `usbstor.sys`'s
  imports.

## 5. What was read

On the branch build of `2.4.0.0` (`p38-w2kf6`, `release` flavour,
unsigned), following the release notes' steps as written (runtime, runs
81-85 in `out\p38-matrix\legs\f6w2k\notes.txt`; `out\p38-matrix\legs\f6xp\`
for XP):

- Both entries listed at Windows 2000's F6 screen (`wa-01-list`).
- A hub with a USB keyboard, mouse and audio device: every key from F6 on
  the USB keyboard, the three files and services laid down
  (`files-wa-textmode.txt`), no stop, the USB keyboard and mouse working
  in GUI-mode Setup (QEMU trace `qtrace-82.txt`, no PS/2 key event), to the
  product key page (`wa-g04-key`); `setupapi.log` installed the controller,
  root hub, hub, composite device, keyboard and mouse.
- A USB stick at a root port and no USB keyboard or mouse: the stick
  listed at the partition screen, no stop, `USBSTOR\DISK` installed, to the
  product key page (`wb-g06-key`).
- The Windows 2000 entry with the files missing: the prompt above, and the
  copy going on once a floppy holding them was put in (`wc-02` to `wc-04`).
- 32-bit XP SP3 with the first entry on the same package: GUI-mode Setup
  installed the controller from `OemDir` with no copy error (`xw-g06`).

The two Windows 2000 disks were then carried on from the product key
page, on the same package, to the desktop and one ordinary restart
(runtime, runs 86-92, the key typed into the key page only):

- The hub with the USB keyboard, mouse and audio device: every remaining
  Setup page and the desktop driven from the USB keyboard and mouse (QEMU
  trace, no PS/2 key event); at the first boot and after the restart,
  Device Manager with no problem (controller, root hub, hub, composite
  device, the HID keyboard and mouse, the USB audio device), the System
  log holding only the event log service's own events, and `XHCISNAP`
  `2.4.0.0` reading the driver `2.4.0.0` (`wad-*`, `snap-86\WAD.*`).
- The stick at a root port, with PS/2 input: the stick readable from the
  desktop at both boots, Device Manager with no problem, the System log
  clean, `XHCISNAP` `2.4.0.0` (`wbd-*`, `snap-86\WBD.*`). Text mode had
  lettered the stick `C:` at the partition screen, so Windows went onto
  `E:` and the stick was `F:` on the desktop.
- Read off both disks afterwards: `usbd`, `hidclass` and `hidparse` at
  `Start` 3 as in section 4, `xhci98` at `Start` 3 (GUI-mode Setup's
  install of the controller rewrote text mode's 0, and both boots went
  on with it), and `xhcihlpr` at `Start` 0 with its file installed
  (`XhciPollFineTimer` is 0, so the driver did not look for it); the
  installed files are the floppy's, and the three Windows files the
  expansion of the CD's.
- The stick-only run reset the guest once by itself during GUI-mode
  Setup's "Installing Components"; no stop screen was caught and no
  dump written that early. It did not recur in a watched re-run from
  the same disk or at any later boot; the cause is unknown.

## 6. What is not read

- An unattended install through `$OEM$\TEXTMODE`, whose files text mode
  reads from the install source rather than drive A:.
- A slipstreamed CD carried through.
- Real hardware.
- A Windows 2000 CD older than SP4.

## 7. The media

The release carries no Microsoft file. `txtsetup.oem` names the three
files, and the release notes tell the user to copy them from their own
CD; the package gates (`PKG-MSFILE` in `check-inf.ps1` and
`check-txtsetup-oem.ps1`) refuse a staged package holding any of them, by
name or compressed name. `docs/contributing/legal-provenance.md` section 5
records the decision as fact.

## Sources

- `out\p38-matrix\legs\f6w2k\` (git-ignored, development host A):
  `notes.txt` (runs 1-8 and 41-49, the stop; 61-66, the investigation;
  71-72, the slipstream attempt; 81-85, the built package; 86-92, its
  desktop and restart), the shots
  named above, the hive and file listings;
  `out\p38-matrix\legs\f6xp\` for the XP leg.
- `docs/contributing/lessons.md`, "Windows 2000 from the F6 floppy with a
  USB keyboard, mouse or stick stops at the first GUI-mode boot ..." and
  "A `txtsetup.oem` disk is always read from drive A: ...".
- Design record 13 section 5.6, limit 7; `docs/contributing/legal-provenance.md`
  sections 4 and 5; issue 10.
