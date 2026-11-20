# Issue 32 - Windows 98 SE: a blue screen in USBSTOR when a flash disk is plugged in, after the update to 3.5.0.0

Status: **closed (owner, 2026-11-17): reproduces with xhci98 disabled. Cause found in the emulator on 2026-11-20
(section 9): two driver keys name the storage stub in different letter case, Windows 98 starts it a second time,
and the call can land in USBSTOR. xHCI98 is not needed for it.** The owner's decision of 2026-11-17 rests
on the reporter's own test: on the reporter's machine the fault comes with the USB 3.0 controller, xhci98's only one
there, disabled in Device Manager, and, in a separate test, with the USB Tree's tray closed (section 6). Beside it
two points stayed unresolved on 2026-11-17: the reporter said `3.4.3.0` worked, and the one start with `3.4.3.0`'s
`xhci98.sys` was not made (section 7; made by 2026-11-19, section 8). No further emulator leg was to be run (owner, 2026-11-17; legs ran again on 2026-11-19 and 2026-11-20). GitHub issue 35, roadmap task 50.17,
which held the cut of `3.6.0.0` from 2026-11-15 and no longer holds it (owner, 2026-11-17). No defect of `3.5.0.0`
that reaches the faulting device was found, in the code or in virtual machines built to the reporter's storage
stack, `usbd.sys` and memory size. On 2026-11-19 the reporter answered again (section 8): with old device entries
under `HKLM\Enum\USB` and `HKLM\Enum\USBSTOR` present, the fault came with `3.5.0.0`'s `xhci98.sys` and with
the driver's files deleted, and not with `3.4.3.0`'s; with those entries removed, `3.5.0.0` gave no fault.
On 2026-11-20, from the reporter's registry exports (section 9), the fault was made in the emulator with no class
filter: one storage device's driver key names the stub `WDMSTUB.SYS` and another's `wdmstub.sys`; Windows 98
then makes a second driver object for the one stub image and calls its entry point again, and that call lands in
the first routine of `USBSTOR.SYS` where `USBSTOR.SYS` has been loaded over the stub's start-up code.

**Scope.** Windows 98 SE (Czech) with a third-party USB storage stack ("NUSB 3.3c CZ") that runs Windows 2000 SP4's
`usbstor.sys` through `wdmstub.sys`, on a desktop board with two Intel EHCI controllers and a VIA VL805 card
(`1106:3483`) under xhci98. Updated by Setup from `3.4.0.0` to `3.5.0.0`. The reporter says `3.4.3.0` still worked.

## 1. What was seen

Plugging a flash disk gives "A fatal exception 0E has occurred at 0028:FF0FDA0B in VXD USBSTOR(01) + 0000000B".
After a key Windows carries on; later plugs give no blue screen, but no flash disk starts, on an EHCI port or an
xHCI port.

## 2. What the reports show

Three report sets from the xHCI98 USB Tree (before the plug, after the blue screen, and a second session) show:

- The stick (`125F:0001`, High Speed) on the Intel EHCI controller, behind its `8087:0024` hub: Windows' EHCI stack
  from the storage package, not xhci98. xhci98's note ring shows nothing at the plug.
- The stick's devnode: driver "wdmstub", `USBSTOR.INF`, not started, Configuration 0, no pipes, problem 0. Plugged
  into the VL805 after the fault, xhci98 lists it on the same instance id (the serial form), and no function driver
  configures it.
- A card reader (`0BDA:0161`, a mass-storage function) already started on the same EHCI bus.
- `usbd.sys` 4.10.0.2226, 18,944 bytes: the file of Microsoft's Q281533 update.
- xhci98's devnodes on driver keys numbered after the stick's.

The storage package is the reporter's `NUSB33D.EXE`: `USBSTOR.SYS` 5.00.2195.6655 (21,552 bytes) and `WDMSTUB.SYS`
5.00.006 (12,767 bytes), `NTMPDriver = "wdmstub.sys,USBSTOR.SYS"`, the upper filter `USBNTMAP.SYS`.

## 3. Where the fault is

`USBSTOR(01) + 0000000B` is 0xB bytes into the first routine of that `USBSTOR.SYS`'s `.text` (98 SE loads each
section as its own object). That routine is the completion callback USBSTOR passes to `PoRequestPowerIrp` when a
system set-power IRP arrives: it takes its Context as the device object, and at 0xB reads the saved system IRP
through the device extension (a reading of that one routine alone, which the owner allowed on 2026-11-15).

What the guests showed of it (registers, stack words and documented structure fields only):

- **It runs at every shutdown.** At every 98 SE shutdown, on `3.4.0.0` and `3.5.0.0` alike, USBSTOR asks for D3 for
  each of its devices, and the callback runs at once with that device's own object as Context.
- **A fault there at shutdown shows nothing.** With the Context of that shutdown request changed to an address that
  is not mapped, the callback was entered, the processor took the page fault, and the guest then powered off
  normally with no blue screen. A fault in this routine at shutdown is taken and passed over.
- **The reporter's offset fits a deleted device.** A Context that is itself not mapped faults at 0x7, before
  0xB. The reporter's 0xB fits a Context that can still be read but whose device extension is gone:
  most likely a device object already deleted, not a wild pointer.
- **Nothing at a plug leads to it.** In the emulator, on `3.4.0.0` and `3.5.0.0`, the only power request made at a
  plug is the hub's own, one for a newly plugged device (none for a stick already known); USBSTOR makes none, so
  its callback does not run at a plug. Session-long traces from the desktop (the
  card reader plugged, five minutes idle, a new stick, the unplug, the known stick) show nothing outstanding before
  the first plug.
- **A removal racing the request did not reproduce it.** With the stick's first power request held for 20 s at
  shutdown and the stick removed 6 s into the hold, and again with the held request failed, the callback ran with
  its own intact Context.

So on the reporter's machine a system power IRP reached a USBSTOR device around the plug, and the callback's Context
then pointed at a device object that had most likely been deleted. What sent that IRP, and why the device was gone,
is not known. A suspend could not be forced in these guests while the desktop was up (the Shut Down dialog offers no
Stand by, and asking for one by program failed), so that path is unread.

## 4. What was tried

All on QEMU, 98 SE (ACPI install), `ich9-usb-ehci1` and `qemu-xhci`; the full table is in `runs/run-50.md`,
"50.17" and "50.17 (continued)". The stick started, with a drive letter, in every leg:

- NUSB33D installed by its own setup; `3.4.0.0`, then the update to `3.5.0.0`; and `3.4.0.0` to `3.4.3.0` to
  `3.5.0.0`; and an uninstall followed by a fresh `3.5.0.0`.
- A second mass-storage device (two LUNs) on the same EHCI bus, hot-plugged or present at power-on, and the stick
  plugged during a copy to it; the stick on the xHCI port.
- The Q281533 `usbd.sys` installed first by its own package, a large-memory patch and 2 GB (and 2.75 GB) of RAM,
  NUSB33D, `3.4.0.0`, then the update.

## 5. What the code says

Between `3.4.3.0` and `3.5.0.0`:

- `xhci98.sys` imports the same kernel routines and nothing from `usbd.sys`.
- Its new code on 98 SE sends no power request and changes no device state outside its own controller and hubs.
- The USB Tree's tray reacts to a device arrival as before. Its one new periodic action, the hold watch, reads
  xhci98's own controllers every 10 s.
- With the storage package installed, Setup leaves storage alone, in both releases. Its storage row exists since
  `3.2.0.0` and its code did not change between `3.4.3.0` and `3.5.0.0`. It never replaces a file that is there.
  Twelve guest runs (updates by the window and the console, the row ticked and unticked; NUSB 3.3's Czech build
  and the `USBSTACK.EXE` package alone, in both orders and after a removal) wrote no storage file, INF or driver
  key, and every stick got its letter (`runs/run-50.md`, "50.17 (Setup's storage row)").

No path from `3.5.0.0` to a system power IRP on an EHCI device was found.

## 6. The reporter's answers (2026-11-17)

The answers to the questions of 2026-11-15 (section 7), relayed on 2026-11-17. They are the reporter's account of
the reporter's machine; nothing in them was read on this project's side.

- **Memory.** A memory patch for 4 GB is installed, and `SYSTEM.INI` limits Windows to about 2 GB
  (`MaxPhysPage=80000`), without which the sound card does not work.
- **The installation.** Where the Czech 98 SE installation came from is not known: it was installed from its setup
  files under DOS years ago. A disk image was declined (it holds personal data).
- **The EHCI controllers.** One of the board's two Intel EHCI controllers had already been disabled by the reporter
  because of blue screens, which the reporter thinks may come from interrupt sharing. As reported: the disabled one
  on IRQ 10, shared with the SMBus controller, a PCI Express root port, a network adapter and the IRQ holder for PCI
  steering; the one in use on IRQ 6, shared with the IRQ holder alone; the VL805 on IRQ 3, shared with a PCI Express
  root port and a PCI card that shows no ids.
- **`usbd.sys`.** Where the 4.10.0.2226 file came from is not known (an update or the storage package, the reporter
  thinks).
- **The address.** The same at every fault with a given `USBSTOR.SYS`: 0028:FF0FDA0B with 2000 SP4's 5.00.2195.6655,
  the reporter's earlier file (section 1's fault), and 0028:FF0FDA09 with Windows ME's 4.90.3000.1, the file of
  NUSB 3.3's storage set that the xHCI98 download carries. Two of the tests below gave other addresses in
  `usbstor.sys`, the file's version not stated: 0028:FF162409 and 0028:FF09D329.
- **ACPI or APM.** Windows runs on the ACPI HAL; the board's BIOS has ACPI 1.0.
- **The ports and the order of plugging.** The card reader is a composite device (mass storage and a smart-card
  reader) with four drive letters reserved. The ports are the reporter's numbering on the EHCI controller in use;
  ports 1 and 2 are the case's front panel, and the card reader is normally at port 6. Where the reporter did not say
  what else was plugged in, the row says only what was:

  | What was plugged, and when | Result |
  |---|---|
  | The card reader at port 6 | No blue screen |
  | A flash disk at port 1 | Blue screen |
  | A flash disk at port 2 | Works |
  | A flash disk at port 6 | Works |
  | The card reader at port 1 | Works |
  | The card reader unplugged before the start; a flash disk at port 1 after the start; then the card reader at port 6 | The flash disk works; at the card reader's plug a blue screen at 0028:FF162409, and the flash disk still works after it |
  | The card reader at port 6 and a flash disk at port 1, both present at the start | Both work, and each can be unplugged and plugged again with no blue screen |

- **The tray closed.** The USB Tree's tray closed, then a flash disk plugged: still a blue screen.
- **xhci98 serving no controller.** The USB 3.0 controller (the VL805) disabled in Device Manager and Windows
  restarted, then a flash disk plugged: still a blue screen, at 0028:FF09D329 in `usbstor.sys`.
- **The storage package reinstalled.** NUSB 3.3a's Czech build: a blue screen at 0028:FF0FDA09, with `USBSTOR.SYS`
  4.90.3000.1. 3.3d's Czech build: at 0028:FF0FDA0B. The later Czech build, 3.5 (asked for on 2026-11-15,
  `roadmap-usb4.md`, the decisions table): after its install and restart, a Windows protection error before the
  graphical screen, and Windows no longer starts. The reporter thinks the package has no uninstaller.

**The addresses: an inference, not a reading.** Section 3's address, 0028:FF0FDA0B, is offset 0xB into the 2000
file's first object, loaded at FF0FDA00. The ME file's address, FF0FDA09, and the two later ones, FF162409 and
FF09D329, are each offset 09 if that object is loaded at FF0FDA00, FF162400 and FF09D320: if the later two are the
ME file too, one place in it at three load bases. That fits one routine at the start of the driver's code faulting
each time, as the 2000 file's fault is at the start of its first routine (section 3). It is inferred from the
addresses alone: the ME file was not read, the blue screen's own "+ offset" text was given for the 2000 file only,
and the two later addresses' file version was not stated.

What the tests showed:

- **It reproduces with xhci98 disabled.** The fault came with xhci98's only controller disabled in Device Manager,
  and, in a separate test, with the USB Tree's tray closed. Its cause is unknown, and whether the update played any
  part was not settled on 2026-11-17: the reporter said `3.4.3.0` worked, and the one start with `3.4.3.0`'s
  `xhci98.sys` had not been made (section 7; made by 2026-11-19, section 8).
- **It is not one `usbstor.sys` build.** 2000 SP4's file and Windows ME's both give it, each at one address.
- **It depends on the port and on the order of plugging, on the Intel EHCI controller.** A flash disk at port 1 gave
  it where at ports 2 and 6 it did not. At port 1 it worked when the card reader was absent at the start, and the
  card reader plugged after it then gave the fault. With the two present from the start, neither did.

## 7. Open

The questions of 2026-11-15, and what became of them:

- a cold start with the VL805 card disabled and the USB Tree's tray exited, then a flash disk on an Intel port:
  answered, as two tests, the controller disabled and the tray closed; each gave the blue screen (section 6);
- the same with the card reader unplugged before the start: answered by the table of ports and plugging order;
- Setup's log (`XHCIINST.LOG`) and the options chosen: not answered;
- the memory settings (a memory patch, `SYSTEM.INI`): answered;
- the power scheme and its times, whether Stand by is offered, and whether Windows runs on ACPI or APM: ACPI
  answered, the rest not;
- the versions and sizes of the storage package's files and of the USB drivers beside them: the `USBSTOR.SYS`
  versions answered; where `usbd.sys` and the installation came from is not known;
- one start with `3.4.3.0`'s `xhci98.sys` in place of `3.5.0.0`'s, everything else left at `3.5.0.0`: not made by
  2026-11-17; made by 2026-11-19, with no fault (section 8).

What stays unknown:

- What sends the system power request around the plug (section 3), and why its device is gone by then.
- Why the earlier release was reported as working. On 2026-11-17 the one start with `3.4.3.0`'s `xhci98.sys` had
  not been made; it was made by 2026-11-19 (section 8), and why that version hides the fault is still not known.
- The emulator never ran the card reader plugged after a flash disk, and never emulated a composite reader: section
  4's second device was a two-LUN mass-storage device alone. (From 2026-11-19 the two-LUN stand-in was plugged after
  the stick and before it, section 8; a composite reader is still not emulated.)

Nothing more is planned (owner, 2026-11-17): no further emulator leg is run, and the issue is closed on the
reporter's own test. A Czech 98 SE guest was not built (its installation media did not boot in the emulator).
On 2026-11-19, after the reporter's answer of that day, the owner had emulator legs run again, without asking the
reporter for anything more (section 8).

**The shutdown hang is a separate matter.** "Windows is shutting down" never ending, first seen in these legs, is
task 50.19: it comes only with the storage package's USB 2.0 stack and a stick unplugged in the session, about one
shutdown in seven, 5 of 30 with `3.5.0.0`, 5 of 30 with `3.4.0.0` and 3 of 30 with xHCI98 uninstalled and no xHCI
controller, against 0 of 52 with no stick pulled and 0 of 10 without the storage package. It is not this project's
(owner, 2026-11-15), and it is linked to this issue only through the shutdown path, where the faulting routine runs:
in the hung shutdowns no USBSTOR device was present.

## 8. The reporter's answer of 2026-11-19

Relayed on 2026-11-19, after the closing. It is the reporter's account of tests on the reporter's machine (the
system of the scope paragraph); nothing in it was read on this project's side. The rows are in the order the
reporter gave them. Which port, and so which controller, the flash disk was in for each test, and which
`usbstor.sys` file was installed in each test, were not stated.

| What was done | Result at a flash disk's plug |
|---|---|
| `XhciInterruptMode` 2 (polling) set for the VL805 | The fault; Device Manager still lists IRQ 3 among the controller's resources |
| `3.5.0.0`'s `xhci98.sys` replaced by hand with `3.4.3.0`'s | No fault; the flash disk works |
| `3.5.0.0`'s installed `XHCI98.SYS`, `XHCIUAS.SYS` and `XHCITR.EXE` deleted | The fault, at 0028:FF0FD32B in `usbstor.sys`, an address not reported before; disabling the VL805 in Device Manager as well changed nothing |
| Then every registry key and value containing "xhci" deleted by hand | The fault, at 0028:FF0FD32B |
| From that state, `3.4.3.0` installed | No fault; the flash disk works |
| From that state, `3.5.0.0` installed | The fault, at 0028:FF0FDA0B (section 1's address), with the USB Tree running or not |
| `HKLM\System\CurrentControlSet\Services\Class\USB` emptied of its subkeys, which held entries left from an Intel ICH7 board used before | Windows no longer detected any USB device; restored from a backup |
| From the backup with `3.4.3.0` installed: `XHCI98.SYS` and `XHCIUAS.SYS` deleted | The fault, at 0028:FF0FD32B |
| The same two files put back | No fault |
| The old entries under `HKLM\Enum\USB` and `HKLM\Enum\USBSTOR` removed, with `3.5.0.0` | No fault |

The reporter adds that before xHCI98 was first installed there was no fault, and does not know why `3.4.3.0`
behaved differently. The reporter has a backup image from before the entries were removed.

What it shows:

- **The fault came with the driver's files deleted.** It came with `XHCI98.SYS` deleted, and with every registry
  entry containing "xhci" deleted as well. Section 6's finding, that it comes with xhci98 serving no controller,
  agrees; whether a restart came between each deletion and the plug was not stated.
- **On that machine, with the old entries present, `3.4.3.0`'s `xhci98.sys` is the one state reported without the
  fault.** Seen three ways: the file swapped by hand into `3.5.0.0`; `3.4.3.0` installed from the state with no
  xhci98 entries; and its two files deleted (the fault) and put back (none).
- **Removing the old entries cleared it.** With the old entries under `HKLM\Enum\USB` and `HKLM\Enum\USBSTOR`
  removed, `3.5.0.0` gave no fault.
- **Polling did not prevent it.** With `XhciInterruptMode` 2 set for the VL805, the fault came.

What it does not show:

- Which entries were old: for which devices, under which instance ids, made under which controller and driver, and
  whether the ones that matter are under `Enum\USB`, `Enum\USBSTOR` or both. Section 2's report shows the flash
  disk's instance id in its serial form, the same id on the Intel EHCI controller and on the VL805.
- On which controller the flash disk was in these tests. Section 2's report put it on the Intel EHCI controller,
  which xhci98 does not serve; if it was there in these tests too, how the version of `xhci98.sys` changes the
  outcome is not known.
- Whether `3.4.3.0`, or the state with the driver's files deleted, also runs without the fault once the old
  entries are removed (not reported).
- Which file gave 0028:FF0FD32B. If it is 2000 SP4's, it is that file's offset 0xB (section 3) with its first
  object loaded at FF0FD320, where at section 1's fault it was loaded at FF0FDA00: one place in the file at two
  load bases. Why the base differs is not known. That is an inference from the addresses alone, as in section 6.

What it changes in the closing of 2026-11-17:

- **Stays true:** the fault comes with xhci98 serving no controller. With `3.5.0.0` it came at section 1's address,
  0028:FF0FDA0B, again; whether that test ran 2000 SP4's file, and so the routine of section 3, was not stated.
- **No longer holds:** that the one start with `3.4.3.0`'s `xhci98.sys` was not made (section 7). It was made: with
  the old entries present, `3.4.3.0`'s `xhci98.sys` installed gave no fault where `3.5.0.0`'s, or the files deleted, gave it. The
  outcome on that machine depends on the version of `xhci98.sys`; whether the update brought the fault in, or
  `3.4.3.0` hides one that is there without it, these tests do not settle: the fault also came with the
  driver's files deleted.
- **Narrowed:** "cause unknown". On that machine the fault went away under `3.5.0.0` when old entries under
  `HKLM\Enum\USB` and `HKLM\Enum\USBSTOR` were removed. Which entries, and why one `xhci98.sys` version hides the fault
  while they are there, are not known.

**What was tried on it (2026-11-19).** The code: between `3.4.3.0` and `3.5.0.0` nothing changed in what decides
how 98 SE's Configuration Manager identifies a USB device on xhci98's bus (the hardware, compatible and instance
ids, UniqueID, the composite parent); task 49.34's rule that a serial counts only with no twin of it present
decides only whether a device the bus keeps across a resume or a link recovery reopens; and neither version's
`xhci98.sys` touches a devnode, a driver key or an Enum entry of a device on another controller. The emulator: 98 SE
guests whose `Enum\USB` and `Enum\USBSTOR` held old entries for the same stick and for a two-LUN reader, made under
98 SE's own UHCI stack, NUSB 3.3's Czech EHCI stack and xhci98 `3.4.0.0`, under Windows ME's and 2000 SP4's
`usbstor.sys` in both orders of installation, with the stick's entry by serial and by port, and carried through the
updates to `3.4.3.0` and `3.5.0.0`; then plugged at the EHCI and xHCI ports in several orders. On such a guest the
reporter's own steps were also run:
`3.4.3.0`'s `xhci98.sys` copied over `3.5.0.0`'s and the reverse, the driver's files deleted, and every key and value
containing "xhci" deleted with the old entries kept, followed by nothing, `3.4.3.0` or `3.5.0.0`. No blue screen in
any. Two effects of old entries were seen, neither a fault. An entry whose driver key names `USBSTOR.SYS` alone (as
the download's storage set writes it) stayed at Problem 2 once NUSB 3.3's 2000 SP4 file was installed, unless a device
keyed `wdmstub.sys,USBSTOR.SYS` had loaded earlier in the session; then it started. And a stick revived its old
`USBSTOR` child entry, whichever storage stack had made it. In the two cells traced for it, no plug made USBSTOR ask
for power. A board change (the earlier controllers left behind) could not be built: the
guest did not detect the EHCI at its new address. `runs/run-50.md`, "50.17 (old device entries)".

The issue's status is the owner's; it is unchanged here.

## 9. The reporter's answer of 2026-11-20

Readings as of 2026-11-20 12:35, when every cell of the first table had been run; the root-cause readings that
follow it are as of 15:30.

Relayed on 2026-11-20, in parts: three exports of `HKLM\Enum\USB` from the reporter's machine, with notes; an export
of the whole `HKLM\System\CurrentControlSet\Services\Class\USB` branch; and where that export was taken. All four are
`REGEDIT4` text files. What they say of the machine's history is the reporter's account. What this section says they
show is read from their text alone; the exports are not tracked.

- **The faulting set:** `HKLM\Enum\USB` before the clean-up of section 8, in the state that faults, except that the
  reporter had taken out of it the entries of xHCI98's older version.
- **The current set:** `HKLM\Enum\USB` after the clean-up. It is the reporter's current set, and it does not fault.
- **One entry** which, imported over the current set, brings the fault back: the card reader's mass-storage function.
- **The driver keys:** `Class\USB`, exported on the current system, which does not fault. The reporter removed no
  entry from that key in the clean-up.
- The reporter's notes: other entries also have `Capabilities` all zero, and the differences in the strings and in
  `Serial` should not matter, the reporter thinks. And a correction: an earlier remark, that this entry is in both
  `Enum\USB` sets and differs a little, was a mistake; going by its driver key, `USB\0043`, it is not in the current
  set.

What the exports show:

- **The entry.** The one-entry file holds `HKLM\Enum\USB`, the key `VID_0BDA&PID_0161&MI_01` and one instance under
  it, with these values, the faulting set's for that instance byte for byte:

  ```
  [HKEY_LOCAL_MACHINE\Enum\USB\VID_0BDA&PID_0161&MI_01\1USB&VID_0BDA&PID_0161&20070818000000000]
  "Capabilities"=hex:00,00,00,00
  "HardwareID"="USB\\VID_0BDA&PID_0161&REV_6110&MI_01,USB\\VID_0BDA&PID_0161&MI_01"
  "CompatibleIDs"="USB\\CLASS_08&SUBCLASS_06&PROT_50,USB\\CLASS_08&SUBCLASS_06,USB\\CLASS_08"
  "DeviceDesc"="USB Mass Storage Device"
  "ClassGUID"="{36FC9E60-C465-11CF-8056-444553540000}"
  "Class"="USB"
  "Driver"="USB\\0043"
  "upperfilters"="USBNTMAP.SYS"
  "Mfg"="Compatible USB storage device"
  "ConfigFlags"=hex:00,00,00,00
  "Serial"=hex:38,00,00,00,00,00,00,00
  ```

- **The same key in the current set.** The current set has the same instance, with the same `HardwareID`,
  `CompatibleIDs`, `ClassGUID`, `Class`, `upperfilters`, `Capabilities` and `ConfigFlags`. Four values differ:
  `Driver` is `USB\0062`, `DeviceDesc` and `Mfg` are the Czech package's strings for a mass-storage device and its
  maker, and `Serial` is `hex:4f,00,00,00,00,00,00,00`. `USB\0043` occurs once in the faulting set and once in the
  one-entry file, and never in the current set; `USB\0062` occurs in the current set alone. So the correction and
  the exports agree: the key's path is in all three, and the entry the faulting set has there, with its driver key,
  is not in the current set. Importing the one-entry file over the current set replaces those four values.
- **The two driver keys name the same files.** Both are in the `Class\USB` export:

  | Key | `NTMPDriver` | `DriverDesc` | `DriverDate` |
  |---|---|---|---|
  | `0043` (the faulting entry's) | `"WDMSTUB.SYS,USBSTOR.SYS"` | "USB Mass Storage Device" | `12-19-2009` |
  | `0062` (the working entry's) | `"wdmstub.sys,USBSTOR.SYS"` | the Czech package's string | `12-23-2009` |

  Their other values are the same: `DevLoader` `*NTKERN`, `DriverFlags` 1, `InfPath` `USBSTOR.INF`, `InfSection`
  `USBSTOR_BULK`, `ProviderName` "Microsoft", `MatchingDeviceId` `USB\Class_08&SubClass_06&Prot_50`. What differs is
  the letter case of the stub's name, the description and the date. As the export was taken on the current system,
  with nothing removed from that key, `0043` is there in the working state too, unchanged; what the working state
  lacks is an `Enum` entry that names it.
- **A filter on the whole class.** The class key itself carries `UpperFilters` = `libusb0.sys`: an upper filter for
  every device of the `USB` class. `libusb0.sys` is the filter driver of libusb-win32, an open-source USB library.
- **Where the faulting set's storage entries point.** Its 14 mass-storage entries, by driver key, with what each key
  names in the `Class\USB` export:

  | Driver key | Device | Strings | `NTMPDriver` (date) |
  |---|---|---|---|
  | `0010` | `VID_0000&PID_0000\08824CD55101` | an unexpanded string name, "Microsoft" | `USBSTOR.SYS` (12-2-2005) |
  | `0013`, `0015` | `0EA0:2126`; `VID_0000&PID_0000\00A21DF90000` | Czech | `wdmstub.sys,USBSTOR.SYS` (2-21-2007) |
  | `0038`, `0039`, `0041` | `8086:F1A5`, `1307:0165`, `0421:006A` | English | `USBSTOR.SYS` (11-24-2007) |
  | `0040` | `1A4A:1670` | English | `WDMSTUB.SYS,USBSTOR.SYS` (12-15-2009) |
  | `0043` | the card reader's storage function | English | `WDMSTUB.SYS,USBSTOR.SYS` (12-19-2009) |
  | `0045` | `04DA:2372` | the maker's own | `PUMSS1.SYS` (1-9-2002) |
  | `0046` to `0048`, `0056`, `0057` | `04B4:6830`, `058F:6387`, `090C:1000` (two), `125F:0001` | Czech | `wdmstub.sys,USBSTOR.SYS` (12-23-2009) |

  The English strings, "USB Mass Storage Device" and "Compatible USB storage device", are those the download's NUSB
  3.3 storage set (`USBDRV\NUSB33\W98SE\USBSTOR.INF`) gives a Bulk-Only device and its maker; that INF writes
  `NTMPDriver = USBSTOR.SYS` alone, the form of `0038`, `0039` and `0041`. Which INF wrote the capitalised pair of
  `0040` and `0043` is not in the exports. Of the 14, only the card reader's storage function and the stick are in
  the current set, both on newer keys; the current set's nine mass-storage entries all carry the Czech strings, on
  keys from `0062` up, each naming `wdmstub.sys,USBSTOR.SYS` (12-23-2009).
- **Only the reader's storage function changed.** The reader's composite parent
  (`VID_0BDA&PID_0161\20070818000000000`,
  "USB Composite Device", key `0042`, which names `usbhub.sys`) and its smart-card function (`MI_00`, key `0044`,
  "Realtek USB 2.0 SmartCard Reader", section `USBSTOR_BULKNO` of a `USBSTOR.INF`, no `NTMPDriver`) are the same in
  both `Enum\USB` sets. In the current set the storage function's `USB\0062` and `Serial` 0x4F come after those of
  xHCI98's root hub and hub (`USB\0059` and `USB\0060`, 0x4D and 0x4E); in the faulting set its `USB\0043` and 0x38
  come next to its parent's `USB\0042` and 0x37.
- **`Capabilities` zero is not what differs.** It is zero for this instance in both sets, for the smart-card
  function in both, and for xHCI98's root hub in the current set.
- **The flash disk.** Section 2's stick (`VID_125F&PID_0001\AA00000000015037`) is in both sets with the Czech
  strings; its `Driver` is `USB\0057` in the faulting set and `USB\0063` in the current one, both keys naming
  `wdmstub.sys,USBSTOR.SYS`.
- **The rest.** The faulting set has 76 keys and the current set 56. Thirty-six are in the faulting set alone, 16 in
  the current set alone, and of the 40 in both, two differ in their values (the reader's storage function and the
  stick). The 36 are 22 device entries with their parent keys: 12 mass-storage entries, four unknown devices, two
  HID entries, a hub, and a composite device with its two functions. The 16 are xHCI98's root hub and hub and seven
  mass-storage entries (`USB\0064` to `USB\0070`), with their parent keys. Both sets keep the root hubs of earlier
  boards' Intel controllers (`8086:27C8` to `27CC`, under three subsystem ids) beside those of the current board's
  two EHCI controllers, and `Class\USB` keeps their driver keys. No key or value of the faulting set contains
  "xhci". The `Class\USB` export has 72 keys: the class key and the driver keys `0000` to `0070`.

What they do not show:

- `HKLM\Enum\USBSTOR`, the other half of section 8's clean-up, including the disk entries under the reader's storage
  function.
- Which INF wrote `0043` and `0040`, the keys that spell the stub's name in capitals.
- Which version of `libusb0.sys` is installed, whether the file is present, and since when the class carries the
  filter.
- For the test with the one entry: which ports the reader and the flash disk were in, whether a restart came between
  the import and the plug, which `xhci98.sys` was installed, and what Device Manager showed for the reader.
- The faulting set exactly as it faulted: xHCI98's older entries had been taken out of it.

**The emulator leg (owner, 2026-11-20).** A 98 SE guest built as in section 8 (QEMU, ACPI install,
`ich9-usb-ehci1` and `qemu-xhci`): the reporter's Czech storage package, which runs 2000 SP4's `USBSTOR.SYS` through
`WDMSTUB.SYS`, then `3.5.0.0`. The reporter's reader and stick are stood in for by QEMU's devices on the EHCI: the
reader by a two-LUN `usb-bot` at port 6, present from power-on, the stick by a `usb-storage` plugged at port 1 after
the desktop. Their own `Enum\USB` entries were given the reporter's entries' values (the reader's the storage
function `MI_01`'s, the stick's the `125F:0001` entry's on a key as `0063`) with QEMU's hardware id, and driver keys
`0042` to `0044`, `0062` and `0063` were written as exported, by the rig's own scripts from the exports' text.
Each run is a cold start, the plug, and 120 s of watching for a blue screen. The loaded-image list was read as data
in every run: VXDLDR's list of loaded devices, as the Windows 98 DDK's `vxdldr.h` documents it.

The class filter is libusb-win32 0.1.12.2's filter installer, `libusb-win32-filter-bin-0.1.12.2.exe` (435,406
bytes, SHA-256 `c519b60080d7e114734c8f703ba744787df10153551fc486e6c5c8dd9896df6d`), run inside the guest. It placed
`libusb0.sys` in `SYSTEM32\DRIVERS` and set `UpperFilters` = `libusb0.sys` on the `USB` class key, as the reporter's
export has it, and on six more (`MEDIA`, `HID`, `Net`, `Image`, `Modem`, `Storage`).

| Cell | The reader's entry, and what else | Runs | Blue screen |
|---|---|---|---|
| A | The faulting entry, on `0043` | 3 | none |
| B | The working entry, on `0062` | 3 | none |
| C | The working entry with only `Driver` changed to `USB\0043` | 3 | none |
| D | As A, with `0043`'s `NTMPDriver` in lower case | 3 | none |
| G | As A, with the class filter | 3 | 3 of 3: "A fatal exception 0E has occurred at 0028:FF0E094B in VXD USBSTOR(01) + 0000000B", at the first look, 12 or 13 s after the plug |
| G2 | As B, with the class filter | 3 | none |
| CG | As C, with the class filter | 3 | 3 of 3, the same text and address, at the first look, 12 s after the plug |
| DG | As D, with the class filter | 3 | none |
| E | As A, the reader stand-in not attached at the start | 3 | none |
| F | As A: one run with `3.4.3.0`'s `xhci98.sys` copied over `3.5.0.0`'s, one with `3.5.0.0`'s | 2 (one each) | none |
| G, `3.4.3.0` | As G, with `3.4.3.0`'s `xhci98.sys` copied over `3.5.0.0`'s | 3 | 3 of 3: "A fatal exception 0E has occurred at 0028:FF0CF3AB in VXD USBSTOR(01) + 0000000B", at the first look, 11 to 13 s after the plug |
| EG | As G, the reader stand-in not attached at the start: the stick plugged, then the reader at port 6 | 3 | none at the stick's plug; 3 of 3 at the reader's: "... at 0028:FF0DF5EB in VXD USBSTOR(01) + 0000000B", at the first look |

- **The fault is reproduced.** In cell G the blue screen is section 1's: offset 0xB into the first object of
  `USBSTOR.SYS`, loaded at `FF0E0940` in that guest; the file is 2000 SP4's, section 2's (21,552 bytes). After the key
  Windows carried on, and the stick's devnode was not started (its status word without the started bit). It needs,
  in these runs, both the class filter and the faulting entry: neither alone gave it (A, G2).
- **In the guest, the capitals decide it.** With the filter, the working entry with only its `Driver` value changed
  to `USB\0043` faulted every time (CG), and the faulting entry whose key `0043` names the stub in lower case never
  did (DG). Of all the differences between the faulting and the working entry, the one that brings the fault in
  these runs is the spelling of the stub's name in the reader's driver key, `WDMSTUB.SYS` against `wdmstub.sys`; the
  entry's strings and `Serial` (CG) and the key's description and date (DG) do not. How the spelling leads to the
  fault was not read.
- **98 SE loads the filter.** With the filter installed, `libusb0` is in the loaded-image list before the plug, and
  without it, not. Which devices it was loaded for was not read; of the seven classes it was set on, `USB` holds in
  this guest the two USB controllers, their root hubs and the two stand-ins. That it sits above the stand-ins is an
  inference from the class.
- **No second image.** In every image list taken, at most one `USBSTOR` image and one stub image were loaded, at one
  base each: with the reader stand-in present from the start, one of each before and after the plug; in E and EG,
  none before the plug and one of each after the stick's (the traced runs took the list before the plug only).
  The stub's module name is spelled as the key that loaded it first named it: `WDMSTUB` where the reader's key
  spells it in capitals (A, C, G, CG), `wdmstub` where it spells it in lower case (B, D, G2, DG). Where the
  stick's key spelled it otherwise than the reader's (A, C, G, CG), it loaded no second stub.

- **Section 6's row is reproduced.** With the reader absent at the start, the stick worked, and the reader's plug
  after it gave the blue screen (EG), as in section 6's table. There the stick's lower-case key loaded the stub
  first (`wdmstub`), so which key loads the stub first does not decide it. With the filter and the reader's key in
  capitals, the fault came at the plug of the second storage device, whichever of the two it was.
- **What enters the faulting routine.** Read through QEMU's debugging stub, as registers, stack words and memory
  values only, with breakpoints on NTKERN's `PoRequestPowerIrp` and on the first byte of `USBSTOR.SYS`'s first
  object, where the faulting routine begins (section 3). In a working run with the filter, at shutdown, USBSTOR
  asked for D3 for each of its devices with that routine as completion and the device's own object as context,
  and the routine was entered from NTKERN with that context; `libusb0` asked for D3 for the same devices too. At
  the plug in the working state the routine was not entered (two runs). In the faulting state no `PoRequestPowerIrp`
  came between the plug and the fault (three runs), and the routine was entered about 1 s after the plug from a
  return address, `C0293BDD`, in the system area where NTKERN's `PoRequestPowerIrp` also lies (which component it
  belongs to was not read), with a stack that is not a power request's completion: its second and third
  arguments are addresses on the stack, and its fourth, the one the routine takes as its context, was 0 (two
  runs). Read at that moment, the device-extension field of a device object at address 0 (offset 0x28) holds
  `0588003A`, and `0588003A` + 0x6C, the field section 3 has the routine read at 0xB, is not mapped. That fits
  section 3's account of the offset: a context that can be read but holds no device extension.

**The root cause, as far as the readings carry it (2026-11-20, from 12:45).** Read with the guest held at the
breakpoint and before and after the plug, as data: driver and device objects by the Windows 2000 DDK's
`DRIVER_OBJECT` and `DEVICE_OBJECT` layouts, the routine's arguments, and the loader's object table as before.

| Cell | The reader's entry, and what else | Runs | Blue screen |
|---|---|---|---|
| EP | As A, with no filter; the reader stand-in not attached at the start: the stick plugged, then the reader | 3 | none at the stick's plug; 3 of 3 at the reader's: "... at 0028:FF0D8FCB in VXD USBSTOR(01) + 0000000B" |
| TA | As A, with a pass-through filter of this project's own, built for this test only, as the class filter in `libusb0.sys`'s place | 3 | none |
| TB | As B, with the same filter | 3 | none |
| TL; TLE | As TA with the filter's build that logs every request it passes; TLE as EP | 1; 1 | TL none; TLE at the reader's plug: "... at 0028:FF0D9C0B in VXD USBSTOR(01) + 0000000B" |
| NXa | As EP on a machine with no xHCI controller: xHCI98's files are on the disk, and nothing loads them | 3 | none at the stick's plug; 3 of 3 at the reader's: "... at 0028:FF07926B in VXD USBSTOR(01) + 0000000B" |
| NX | As EP on a guest built with the Czech package alone, xHCI98 never installed, no xHCI controller | 3 | none at the stick's plug; 3 of 3 at the reader's: "... at 0028:FF087F6B in VXD USBSTOR(01) + 0000000B" |

- **The call is a second entry into the stub.** At the breakpoint the routine's first argument is a driver object
  named `\Driver\wdmstub.sys`, made at the stick's plug, and its second a counted Unicode string on the stack:
  the two arguments of a driver's entry point, its `DriverEntry`. The routine takes its fourth stack word, 0, as
  its context (above).
- **Two spellings, two driver objects, one image.** With the reader's key naming `WDMSTUB.SYS` and the stick's
  `wdmstub.sys`, once both devices have loaded there are two driver objects for the stub, `\Driver\WDMSTUB.SYS`
  and `\Driver\wdmstub.sys`, while the image list holds one stub image. Both driver objects' `DriverInit` is the
  start of the stub's seventh object, of type 17 in the loader's table. With one spelling there is one driver
  object (DG, G2).
- **What lies at that address decides.** Where `USBSTOR.SYS`'s first object was loaded at that same address (every
  run with `libusb0`, and every run in which the reader was absent at the start: E, EP, TLE, NXa, NX), a
  second entry lands in USBSTOR's first routine and faults; G, CG, EG, EP, TLE, NXa and NX had both spellings and
  faulted, E had the stick's alone and did not. Where it
  was not (A, C, F: the reader present at the start, no filter), the second entry runs at the stub's own address,
  and in A the two driver objects were there after the plug with no fault. That the seventh object is the stub's
  start-up code, freed after its first call and so free for the next image, is an inference from its type and
  from `USBSTOR.SYS` being found loaded over it.
- **No filter is needed.** EP faulted without any class filter. The filter's part in G is where the images land:
  with `libusb0` loaded first, USBSTOR's first object lands on the stub's seventh object also when the reader is
  present at the start. In libusb-win32 0.1.12.2's source (the project's public repository, commit `00eb396` of 2009-07-07, "version 0.1.12.2", its
  `libusb/Makefile` giving version 0.1.12.2; read as text, nothing of it tracked), the filter passes power requests
  down, asks for a device power change of the device below when a system power change completes
  (`libusb/src/driver/power.c` lines 132 to 135 and 202) and reports D0 at a start (`pnp.c` lines 97 to 101);
  nothing in it loads a driver or calls an entry point. Its licence files there are `COPYING_GPL.txt`,
  `COPYING_LGPL.txt` and `installer_license.txt`.
- **Not this filter, nor any filter as such.** The pass-through filter of this project's own, which attaches and
  passes every request down unchanged, gave no fault with the reader present at the start (TA, TB): its image is
  smaller than `libusb0`'s, and USBSTOR's first object was loaded clear of the stub's seventh object. With the
  reader absent at the start it faulted at the reader's plug (TLE), USBSTOR's first object again on the stub's
  seventh. Its logging build added no device to the reader's stack before that fault, so the reader had no request
  through it then: the fault comes while the reader's drivers are being loaded. The stick's requests went through
  it and were completed.
- **Without xHCI98 loaded, and without it installed.** With no xHCI controller in the machine, no image list of NXa
  holds `xhci98`, and the fault came as in EP, at other addresses: USBSTOR's first object at `FF079260`, again
  the stub's seventh. The NX guest was built from the base with the Czech package alone and never had xHCI98 (its
  `Class\USB` holds no xHCI98 key); its EHCI has three UHCI companions, so that 98 SE's own USB stack is installed
  beside the package's, as the reporter's `Class\USB` shows it from earlier boards (without them the package's EHCI
  root hub stayed at Problem 2). It faulted as EP, USBSTOR's first object at `FF087F60`, on the stub's seventh.
- **Not read.** Which component makes the second driver object and calls the entry point: the return address is
  in the system area. That driver-object names are told apart by their case while the image is matched without
  it is what the two lists show; the rule itself was not read.

What the guest lacks against the reporter's machine:

- The real card reader. The stand-in is a single-function device carrying the storage function's values; the reader
  is composite, with a smart-card function on a key with no driver. This emulator offers no composite device with a
  mass-storage function, so the composite form was not built.
- The board: two Intel EHCI controllers behind rate-matching hubs (one disabled by the reporter), and the VL805 card,
  where the guest has one EHCI controller with no hub and QEMU's xHCI controller.
- The other old entries: the earlier boards' controllers and root hubs under `Enum\USB` and `Class\USB`, the other
  devices' entries, and `HKLM\Enum\USBSTOR` as the reporter has it.
- The Czech 98 SE itself, the memory size and patch, the `usbd.sys` of section 2, and whatever else is installed.

What it changes in sections 7 and 8:

- **Narrowed further:** section 8's "which entries were old". On that machine one entry is enough, over the current
  set: the card reader's mass-storage function on driver key `0043`, where the working entry is on `0062`. The flash
  disk's own entry did not have to be old: in the current set it is on its newer key, `0063`. In the guest, the part
  of it that matters is the key it names, and in that key the capitals of the stub's name (CG, DG).
- **The two keys load the same two files.** Both name the stub and 2000 SP4's `USBSTOR.SYS` (section 2), the
  faulting one in capitals, the working one in lower case; the rest of the difference is a description, a date and
  the entry's own strings and `Serial`.
- **No longer holds:** that the emulator never showed the fault (sections 4 and 8). It came in a guest with the
  reporter's class filter and the faulting entry, with `3.5.0.0` installed.
- **Changed, in the guest:** section 3's reading that a system power request reached a USBSTOR device around the
  plug and that the context was most likely a deleted device object. In the guest no `PoRequestPowerIrp` call came
  between the plug and the fault, and the routine was entered with a context of 0 (above). The later readings of this
  section show the call to be a second call of a driver's entry point; which component makes it is not read.
- **Not reproduced: the version's part.** In the guest `3.4.3.0`'s `xhci98.sys` did not hide the fault: with it in
  place of `3.5.0.0`'s, cell G faulted in 3 of 3 runs, at the same offset in `USBSTOR.SYS`, whose first object was
  then loaded at `FF0CF3A0`; that the smaller `xhci98.sys`, loaded before it, moved it is an inference from the
  image lists.
- **The root cause, in the guest:** a storage device whose driver key spells the stub `WDMSTUB.SYS` and another
  whose key spells it `wdmstub.sys` make two driver objects for one stub image, and the stub's entry point is
  entered a second time after `USBSTOR.SYS` may have been loaded over it. Nothing of xHCI98 is in that chain,
  and the fault came on a guest that never had xHCI98 (NX).
- **On the reporter's machine, as an inference:** the faulting entry's key `0043` spells the stub in capitals and
  the flash disk's keys in lower case, so the same two driver objects would be made. Whether the second entry
  faults depends on what was loaded at the stub's start-up code by then, and so on what loaded before: the order
  of plugging (section 6), the filter, and the size of `xhci98.sys` (section 8). That would account for those
  dependences and for the clean-up clearing the fault; none of it was read on that machine.

The issue's status is the owner's; it is unchanged here.

## Sources

- The reporter's report sets and storage packages, attached to GitHub issue 35 (2026-11-15).
- The reporter's answers to the questions of 2026-11-15, relayed by the owner on 2026-11-17.
- The reporter's answer of 2026-11-19, relayed that day.
- The reporter's answer of 2026-11-20 (three exports of `HKLM\Enum\USB`, an export of `Class\USB`, notes and a
  correction), relayed that day; the exports are not tracked.
- `docs/contributing/runs/run-50.md`, "50.17" (with "The reporter's answers (2026-11-17)"), "50.17 (continued)",
  "50.17 (old device entries)" and "50.19";
  `docs/contributing/roadmap-usb4.md`, tasks 50.17 and 50.19.
