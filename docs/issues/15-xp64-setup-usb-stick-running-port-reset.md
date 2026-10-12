# Issue 15 - Windows XP x64 Setup booted from a USB stick on an Intel xHCI stops with STOP 0x7B: the firmware's port reset was still running, and the driver gave the port up

Status: **fixed in source for the next release (roadmap-hcd task 37.2);
read on the owner's E7250 on 2026-10-12 up to text-mode Setup's partition
screen.** Reported on a Lenovo ThinkPad X230 against `2.3.0.0` and
reproduced the same day on the owner's Dell Latitude E7250 with the
reporter's own install ISO. The X230 itself has not been read on the fixed
driver; the owner's reading is that its stop is the same one (the same
ISO, the same stop code and parameter, and no optical drive on either
machine). Two defects stood in the way, one behind the other (sections 4 and
5); the first is in every release since `2.0.0.0`, and the second since UAS
transport selection (31-A.3).

Machines affected: Intel xHCI controllers whose firmware boots from a USB
stick through its own xHCI driver: the E7250 (Wildcat Point-LP `8086:9CB1`)
read, and the X230 (Panther Point) reported. Targets affected: Windows XP x64
text-mode Setup run from a stick, in practice from WinSetupFromUSB or a
similar tool on a machine with no optical drive, with `xhci98.sys`
integrated into the install source. The same path on Windows 2000 and
32-bit XP is expected to behave the same and has not been read. An
installed system was never affected, and neither is Setup run from an
optical drive or from a stick the firmware reaches through EHCI.

## 1. Symptom

The reporter integrated `2.3.0.0`'s `release-x64` driver into an XP x64
SP2 ISO with nLite (as a `scsi` component, with exactly `txtsetup.oem`'s
ids), and installed from it on an X230. Text-mode Setup stopped with:

    STOP 0x0000007B (0xFFFFFADF338393C0, 0xFFFFFFFFC0000034, 0, 0)

INACCESSIBLE_BOOT_DEVICE with STATUS_OBJECT_NAME_NOT_FOUND. The same driver
on the finished XP x64 system worked.

The owner's E7250, booting the reporter's ISO from a WinSetupFromUSB 1.x
stick (a SanDisk `0781:55AB`, SuperSpeed and UAS-capable), stopped with the
same STOP, at the start of text mode.

## 2. What the reading shows

**The boot device is the stick, not the disk.** WinSetupFromUSB maps a
small ISO (`TXTSETUP.SIF`, `SETUPLDR.BIN` and `NTDETECT.COM`) into memory
through grub4dos, and its `TXTSETUP.SIF` makes `usbehci`, `usbohci`,
`usbuhci`, `usbhub` and `usbstor` boot drivers and names the stick the boot
and source device (`multi(0)disk(0)rdisk(1)partition(1)`). So the kernel
must have the stick as an NT disk before it resolves the boot device's ARC
name, very early in text mode. A 0x7B with `c0000034` at text-mode start is
that name not found.

**It is the xHCI path.** With the BIOS's "Enable USB 3.0 Controller" off
(the stick on EHCI) text mode started. With it on and `xhci98.sys` patched
out of Setup's list in memory (a grub4dos `cat --replace` on the mapped
`TXTSETUP.SIF`), the same 0x7B came: the stick is on xHCI, and only
`xhci98.sys` can present it.

**QEMU passes.** The same boot rebuilt in QEMU 11.1.0 (the mini-ISO on IDE,
the setup tree on a `usb-storage` disk behind `qemu-xhci` as BIOS disk 1)
reached the partition screen through `xhci98.sys`. QEMU ends a port reset
at once, and SeaBIOS leaves none running.

**The diagnostic screen.** No dump is written in text mode, so a throwaway
build (never in the tree; `out\p37-3\diag\`, git-ignored) registered a
bugcheck callback that printed the bus's state onto the 0x7B screen, through
ntoskrnl's exported `Inbv*` routines. It was proven first in QEMU by a
variant that called `KeBugCheckEx` itself; an HMP `nmi` is no test, because
the HAL's NMI halt never runs bugcheck callbacks. The E7250 read:

- `USBLEGSUP 01000801`: the BIOS had let go and the OS owned the
  controller;
- `cmd 1/1`: the start's No Op was the only command ever issued; no Enable
  Slot;
- the root hub's first BusRelations answer went out after 125 ms, empty;
- root port 13 FAILED, `XHCI_ENUM_FAIL_RESET`, after its one retry, no
  slot - while its PORTSC read `00001203`: connected, enabled, U0,
  SuperSpeed.

## 3. Two wrong turns

**A missing AHCI driver.** STATUS_OBJECT_NAME_NOT_FOUND on the boot device
is the classic sign of a SATA controller XP has no driver for, and the
roadmap first recorded that theory. The ISO carried a generic AHCI driver
(AHCINT, by ages2001, on `PCI\CC_010601`), and the stop came at text-mode
start, before any disk mattered; the boot device was the stick.

**UAS.** An `XHCISNAP` of the installed XP x64 with the stick present at
boot showed it running under UAS (four endpoints), and text mode has no
driver for UAS ids (section 5). A fix for that alone was built and read on
the E7250: the same 0x7B. The diagnostic screen then showed the
enumeration never reached a device at all, so UAS was not this stop's
cause; it was the next one.

## 4. Cause: a reset already running

At the driver's first look the port's PORTSC had **PR set**: a port reset
in progress that the driver had not started, almost certainly the
firmware's own at the handoff. The installed system's `XHCISNAP` shows the
same PR at its first look. A root-port reset in `hcdResetPort` asks
`XhciLinkDecide` how to reset a USB3 port, and a link with PR set is
"resetting", for which a reset is refused ("a reset already running");
`hcdUsb3ResetWrite` returned 0, the reset failed at once without waiting,
and its one retry, at once again, failed the same way. The port was given
up inside one 100 ms debounce, and nothing looked at it again before the
root hub's first answer.

On the installed system the firmware's reset had ended by the time the
debounce was over, so the driver's own reset succeeded first time; and a
port lost there is found again later, while the boot disk is SATA. In text
mode the firmware had been reading Setup's files from the stick up to the
handoff, its reset was still running, and the stick was the boot device,
looked for once.

**Fix (37.2, `hcd_enum.c`):** `hcdAwaitRunningReset` waits a reset already
running out, polling every 10 ms within the warm reset's budget (1 s),
acknowledges its PRC (and WRC on a USB3 port) so its end is not taken for
the driver's own, and `hcdResetPort` then goes on as before; if PR never
clears the port fails as before. The log note `enum.port.prwait` records the
port, whether PR cleared and the time waited.

## 5. The next stop: UAS ids text mode cannot bind

With the reset fixed, this stick would still not have been a disk. Since
31-A.3 a storage interface offering UAS gets one transport at its PDO's
creation, and at SuperSpeed on a streaming controller that is UAS, whose PDO
reports only UAS's ids (`USB\Class_08&SubClass_06&Prot_62`). Text-mode
Setup's `TXTSETUP.SIF` maps only the Bulk-Only ids to `usbstor`, and
`xhciuas.sys` is not in text mode (design record 13 section 5.6). The
choice is the `XhciForceBulkOnly` value, which text mode never writes.

**Fix (37.2, `dfb56bb4`, `hcd_ctl.c`):** an unread `XhciForceBulkOnly` is
1, Bulk-Only where the device offers it. Every INF install path writes 0,
so an installed system keeps UAS. A UAS-only device, with no Bulk-Only
alternate setting, is still not usable in text mode; that case, and
installing onto a USB UAS disk that stays the boot disk, are what folding
the UAS driver into `xhci98.sys` would answer, moved to "Planned work"
on 2026-10-12 (owner).

## 6. Reading

2026-10-12, the owner's E7250, BIOS A17, SATA in AHCI mode, "Enable USB
3.0 Controller" on, the reporter's ISO from the WinSetupFromUSB stick with
both fixes (and the diagnostic screen) in place of `XHCI98.SY_`: text mode
reached the partition screen. Owed: the install carried to the desktop, the
release build read the same way, and the X230 reporter's reading.

## 7. Rules kept

- **A reset already running is waited out, not refused.** Firmware hands a
  port over in whatever state its own driver left it; a running reset is
  one of them.
- **Setup's boot device is looked for once.** A device the bus presents
  late is not late in text mode; it is missing, and the stop names the
  boot device, not the bus.
- **Text mode runs on built-in defaults.** No INF has run, so every
  driver-key value is absent; a default that only an installed system can
  bind is wrong there.
- **QEMU does not leave resets running.** A first-look state on metal that
  QEMU cannot produce needs a metal reading; when nothing can be dumped,
  print the state on the stop screen.

## Sources

- `out\p37-3\` (git-ignored, development host A): the QEMU kit
  (`run.cmd`, `mkiso.ps1`, the mini-ISO and stick tree), screenshots
  `r2-a.png` (the partition screen through `xhci98.sys`) and `r5-b.png`
  (the diagnostic screen in QEMU), the diagnostic sources in `diag\`, and
  the stick's original `XHCI98.SY_` in `stick-backup\`.
- `temp\bsod\plugged.*` (git-ignored): the installed system's `XHCISNAP`
  with the stick present at boot.
- The owner's photograph of the diagnostic 0x7B screen on the E7250,
  2026-10-12.
- Roadmap `roadmap-hcd.md` task 37.2; design record 13 sections 5.6 and
  11.5; 31-A.3.
