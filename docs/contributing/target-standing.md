# Target standing and the readings behind the user documents

What each Windows target of xHCI98 has been observed doing, and on what: the
virtual machines, the emulator and the real machines each reading was taken
on, and what no vehicle has read. The user documents (`README.md`,
`docs/using/`, the release notes, the generated `readme.txt`, the installer
and tool texts) say what the package does and carry no testing status
(owner: no target labelled virtual-machine only, 2026-10-29; no
testing-status wording in user-facing text, 2026-10-31; no untested items
named in the user release notes, 2026-11-01). This file is where that
evidence lives instead.

It was assembled on 2026-11-06 from the texts it replaced, word for word:
the release notes' "Targets and their standing" and "Untested ground"
sections and the testing passages of their current sections, `README.md`'s
"Tested on" table, and the acceptance test's observation notes, all as
published for `3.4.1.0`. A section name in quotes is the release notes'
unless it says otherwise. The release notes' sections for past releases
("What `3.4.1.0` changes" and older) stay as they were published and still
carry their own readings. The run records under `runs/` and the roadmap
tasks they cite are the primary evidence; this file is the summary a
maintainer reads first, and a new reading is added here, never to a user
document.

## The machines

The real machines the project's readings were taken on, and the ones third
parties reported, as `README.md` listed them for `3.4.1.0`:

| Machine | Controller | Read by |
|---|---|---|
| 2012 Lenovo B490 | Intel Ivy Bridge, Panther Point HM77 (7-series) PCH, with EHCI beside it. xHCI 1.0. | The owner |
| 2012 HP EliteBook 8470p | Intel Ivy Bridge, Panther Point QM77 (7-series) PCH (`8086:1E31`, LPC `1E55`), with EHCI beside it. xHCI 1.0. Added for `3.4.2.0` (roadmap-usb4 task 47.33): XP x64, the chipset-routed line and the polled start | The owner |
| 2015 Dell Latitude E7250 | Intel Broadwell-U, Wildcat Point-LP (9-series) PCH, with EHCI beside it. xHCI 1.0. | The owner |
| 2016 ThinkPad E460 | Intel Skylake, Sunrise Point-LP (100-series) PCH. xHCI 1.0. | The owner |
| 2018 HP EliteBook 850 G5 | Intel Sunrise Point-LP (`8086:9D2F`, the E460's controller), xHCI 1.0. | Community |
| 2020 ThinkPad P14s Gen 1 | Intel Comet Lake PCH-LP (400-series). xHCI 1.1. | The owner |
| Omores' Intel and AMD desktops | H110, B360, B550, X570, X670 | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) (1.x.x.x) |
| VMware Workstation 26 (virtual hardware 21), Windows XP SP3 guest | VMware's virtual xHCI (`15AD:077A`), a USB 3.0 stick at SuperSpeed | [Community](https://github.com/yeokm1/xhci98/issues/25) (3.2.0.0) |

## The virtual machines' controllers in the name table

The installer's name table (`instXhciNames` in `xhciinst/inst_core.c`; design
record 23 section 5.13) and the INFs' model lines also name the USB 3
controllers that two virtual machines emulate. The user documents' tables
(`docs/using/controller-names.md` and the release notes' "Controller names in
Device Manager") leave them out (owner, 2026-11-06); they are listed here
instead, and `test/test_inst.c` holds this table to the installer's as it
holds those two:

| Controller | PCI ids | In Device Manager |
|---|---|---|
| VMware USB3 xHCI | `15AD:0778`, `15AD:0779` | xHCI98 USB 3 Controller (VMware USB3 xHCI) |
| Red Hat QEMU xHCI | `1B36:000D` | xHCI98 USB 3 Controller (Red Hat QEMU xHCI) |

## The Windows 2000 qualification, and the real hardware

The release notes' "What this is not" opened with this bullet for `3.4.1.0`:

- It is not tested evenly. Every Windows 2000 result comes from virtual
  machines: Windows 2000 Setup bugchecked on both physical machines this
  project tried, so the driver has never run on Windows 2000 on real
  silicon. Windows ME, 32-bit Windows XP, Windows Vista, 32-bit Windows 7
  and Windows 8, 8.1 and 10 were read in QEMU virtual machines. Real
  hardware: Windows 98 SE, at a bench session before the `2.0.0.0` release,
  on a ThinkPad E460 and a ThinkPad P14s Gen 1, and later on the owner's
  Lenovo B490 for the Intel port switchover; and Windows XP x64 and 7 x64 on
  the owner's Dell Latitude E7250. `2.1.0.0`'s own changes were read in
  virtual machines; of `2.1.1.0`'s, the Power tab and the Intel port
  switchover were read on the owner's real hardware under Windows 98 SE, and
  the registry values in virtual machines; of `2.2.0.0`'s, the SuperSpeed
  fix for Intel Sunrise Point was read on the owner's ThinkPad E460 under
  Windows 98 SE, and the handling of transfer, port and controller faults by
  making each fault it answers on purpose in QEMU virtual machines under
  Windows 98 SE and Windows 2000; no real controller has raised one under
  this driver yet. Of `2.3.0.0`'s, the owner's bench of 2026-10-11 read
  polling, MSI, the interrupt routing repair, the port switchover, composite
  devices and the asynchronous-audio fix on real hardware ("What `2.3.0.0`
  changed"); of `2.3.1.0`'s, the Windows XP x64 Setup fix was read on the
  Dell Latitude E7250 ("What `2.3.1.0` changed"). Of the Thunderbolt 3 and
  USB4 support, from `3.0.0.0`, only the Thunderbolt 3 half has run on real
  hardware, on the ThinkPad P14s Gen 1 under Windows 98 SE, and `3.2.0.0`'s
  `S98VSD.VXD` with it at the bench of 2026-10-22; the rest was read on an
  emulator of those controllers. `3.3.0.0`'s changes were read in QEMU
  virtual machines ("What `3.3.0.0` changes"). "Targets and their standing"
  says what each one rests on.

## Each target's standing

Each target's row says what the `2.0.0.0` drivers were observed doing on that
system, and where; `2.1.0.0`, `2.1.1.0`, `2.2.0.0`, `2.3.0.0` and `2.3.1.0`
carry that code with the changes their history entries list, and the rows "What
`2.1.0.0` changed", "What `2.1.1.0` changed", "What `2.2.0.0` changed", "What
`2.3.0.0` changed" and "What `2.3.1.0` changed" say where each change was read.
"The integration build" is the merged build of a release's code before its cut,
in the never-published `qemu` flavour. The ten install legs are read on the
`release` flavour of that build, and again from the release asset itself after
the cut.

| Target | Standing | What it rests on |
|---|---|---|
| Windows 98 SE | Primary | Virtual machines, under NUSB 3.3 and under SweetLow's stack: the install, mouse, keyboard, storage with a verified file compare, a composite audio device split into its functions and played to its end (unheard: the guest has no audio out), the ASIX Ethernet adapter passed through, the controller's Advanced tab and the root hub's Power tab, and disable, enable, remove and rescan. Hubs behind QEMU's Full-Speed hub to five tiers, 25 plug cycles per device class, and a 120-hub churn with the guest responsive. A stock install with no USB 2.0 stack: the controller, hubs, a HID mouse and a composite audio device work (next section). SuperSpeed storage and UAS at SuperSpeed (16 streams) and at High Speed, each with a verified round trip, on the integration build. UAS and forced Bulk-Only on a passed-through dual-transport bridge, each with a verified round trip. Real hardware, on a ThinkPad E460 and a ThinkPad P14s Gen 1: the install, HID, storage, the ASIX Ethernet adapter, and a Full-Speed audio device played and heard at a root port and behind a hub; High-Speed hubs, single- and multi-TT, with Low- and Full-Speed devices behind them, and a Full-Speed hub behind a High-Speed hub (a USB 2.0 hub held at Full Speed by a full/low-speed isolator); a Low-Speed mouse polled every 8 ms at a root port and behind a hub; two SuperSpeed drives on a root port, the link read as SuperSpeed in `XHCISNAP`'s slot table, with round trips and throughput against the same drive behind a USB 2.0 hub; a SuperSpeed hub with a SuperSpeed drive behind its SuperSpeed half and a High-Speed device behind its USB 2.0 half, plugged, unplugged and plugged in again; and UAS on a dual-transport bridge at SuperSpeed and, behind a USB 2.0 hub, at High Speed, UAS on a UAS flash drive at SuperSpeed with Bulk-Only behind a USB 2.0 hub, and the forced-Bulk-Only value on both at SuperSpeed, each with a round trip and throughput against Bulk-Only. On the E460, whose Intel Sunrise Point-LP controller publishes a speed table every release before `2.2.0.0` read too strictly, no SuperSpeed device was ever enumerated (`docs/issues/11-sunrise-point-ssic-psi-table.md`), so up to `2.1.1.0` the SuperSpeed clauses above can have been read on the P14s Gen 1 alone; `2.2.0.0`'s SuperSpeed reading on the E460 is in the row "What `2.2.0.0` changed" |
| Windows 2000 SP4 | Primary; read in QEMU guests, Windows 2000 Setup having bugchecked on both real machines tried | The same device, hub and Device Manager rows as Windows 98 SE, under Driver Verifier, plus a multiprocessor guest. SuperSpeed storage, and UAS at SuperSpeed and at High Speed, on the integration build. UAS, forced Bulk-Only and the switch between them on a passed-through dual-transport bridge, each with a verified round trip. SuperSpeed hubs: untested, as no virtual machine models one; built from the specification. Never run on real hardware |
| Windows ME | Supported; read in QEMU guests | Under SweetLow's stack, on the integration build: the install, HID, storage with a verified file compare, unplug and replug, a hub with a mouse and a stick behind it, a composite audio device bound at a root port and behind a hub, the root hub's disable and enable, a 10-cycle soak per device class, SuperSpeed storage, and UAS at SuperSpeed (streams) and at High Speed. Bulk-Only storage works on Windows ME's own mass-storage files, and UAS once those are present (next section). Re-enabling the controller with a USB mouse attached hung Windows ME under `2.0.0.0` (a keyboard was never tried); `2.1.0.0` fixes it: the devices are kept across the controller's stop and revived on the re-enable, as Microsoft's hub driver does. Read on the `2.1.0.0` integration build: the re-enable with a mouse three times of three, with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; and on the `release` flavour once more after the update from `2.0.0.0`. On a stock Windows ME, with no USB 2.0 stack installed, the `2.3.0.0` release package (2026-10-12): the install, HID, a hub, a composite audio device behind the hub playing a tone whole, Bulk-Only storage on Windows ME's own files and UAS at SuperSpeed with verified file compares, the controller's disable, enable, remove and rescan, and shutdown |
| 32-bit Windows XP SP3 | Supported; read in QEMU guests | On the integration build: install, HID, storage with a verified file compare, unplug and replug, a hub with devices behind it, composite audio bound, the root hub's and the controller's disable and enable, shutdown; SuperSpeed storage, and UAS at SuperSpeed and at High Speed. A 10-cycle soak per device class (HID, a hub with a mouse, storage) |
| Windows XP x64 SP2 | Virtual machines, and real hardware on the Dell Latitude E7250; the 64-bit drivers | The same clauses as 32-bit XP, with the 64-bit `xhciuas.sys` at SuperSpeed and at High Speed, and the same soak. Real hardware: XP x64 on the owner's Dell Latitude E7250 |
| Windows Vista SP2, 32-bit and x64 | Supported; read in QEMU guests | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled, the drivers being unsigned then; the test-signed x64 drivers of `2.3.0.0` were read in test-signing mode, with no F8. The same soak |
| Windows 7 SP1, 32-bit and x64 | Virtual machines; x64 also on real hardware, the Dell Latitude E7250 | The same clauses, at four virtual processors; x64 on starts with driver signature enforcement disabled, the drivers being unsigned then, and the test-signed x64 drivers of `2.3.0.0` in test-signing mode, with no F8; five controller disable and enable cycles on each. The same soak. Windows 7 is not read on real hardware for `2.0.0.0`. Real hardware: 7 x64 on the owner's Dell Latitude E7250 |
| Windows 10, 32-bit and x64 (every build below 22000: the releases 1507 to 22H2, the LTSC releases, and Windows Server 2016 to 2022 as 10) | From `3.2.0.0`: the Thunderbolt 3 / USB4 connection manager, with the USB drivers an opt-in install, unticked, Windows' own USB 3 driver staying the default, and the bundled Stor98 likewise unticked, Windows' own `stornvme` staying the tunnelled drive's driver (roadmap-usb4 task 43.59); the drivers read in virtual machines, the installer's console install and uninstall read only by task 43.59's legs on 10 x86 and x64, its full matrix not yet run | Virtual machines of 22H2 (19045), the one release of 10 read, each driver installed by a script, not by the installer (roadmap-usb4 tasks 43.23, 43.33 and 43.36, 2026-10-22; QEMU, legacy BIOS, test-signing mode on): on 32-bit 10, `xhci98.sys` from the `3.1.0.0` package, installed over Windows' own driver after the "can't verify the publisher" prompt, ran the emulated USB 3 controller after the restart, with its root hub, a hub, the keyboard, the tablet, two USB sticks and a composite device started; Stor98's 32-bit driver, installed over Windows' own `stornvme`, ran an NVMe drive behind the emulator's Thunderbolt tunnel, and a surprise removal during writes lost the copy in progress with Windows' "Delayed Write Failed" message. On 64-bit 10 the `3.1.0.0` package was refused (error `0xE000022F`, below); with the catalogs, signed in the guest with a certificate of its own, the five x64 INFs installed after the same prompt and `xhci98.sys` and `xhcicm.sys` ran after the restart. Through the installer (roadmap-usb4 task 43.37's legs, 2026-10-23 and 24, `runs/run-43-24.md`; the build before task 43.59's Stor98 fix, the x64 half signed with the project test certificate), on 22H2 x86 and x64: the connection manager alone installed and, after the "can't verify the publisher" prompt, ran the emulated Thunderbolt 3 controller - approval, re-enumeration, the eject - and on x64 the emulated USB4 host too, with a hub and a stick behind the enclosure's USB3 tunnel on Windows' own USB driver; Stor98 x86 replaced `stornvme` on the tunnel drive and an unplug during writes failed the copy within seconds (Stor98 x64 was then at Code 10 on 10 x64, which task 43.59 fixed); under 10's own `stornvme` the same unplug failed the copy in under half a minute with "Delayed Write Failed"; the NUSB box copied the seven files to the install folder only and the uninstall removed them; the USB drivers, ticked, ran HID, a hub, sticks, composite audio and a hot-plug, and the uninstall gave the controllers back to Windows' own driver, though only after two or more runs with a restart between (each run said what was left and kept the Add/Remove Programs entry for the next; on 64-bit 10 one run also waited for a Thunderbolt port that was not connected). Not read: Sleep (QEMU offers these guests no standby), UAS (QEMU's device fails under Windows' own `uaspstor`), the "can perform faster" notice (QEMU's stick cannot raise it), Secure Boot on (no UEFI guest), and Intel's Thunderbolt driver. The installer admits it as it admits 8.1 (roadmap-usb4 tasks 43.34 to 43.36); only 22H2 is to be read (43.37), and every other release and Windows Server 2016 to 2022 are admitted untested. Windows 10 has no Thunderbolt or USB4 driver of its own; where Intel's Thunderbolt driver runs the controller, the installer leaves it unless the Thunderbolt support is ticked, which replaces it after a warning (from `3.3.0.0`, roadmap-usb4 task 44.28). Under `xhci98.sys` Windows' own USB notices and settings do not apply (task 44.24; design record 13 section 6.14.7): Explorer's "Device can perform faster when connected to USB 3.0" and Devices and Printers' status lines (the USB Tree's "can run faster" notice stands in for them), Windows' USB error messages and the "Notify me if there are issues connecting to USB devices" switch, and the USB power settings in Power Options; no Kernel DMA Protection opt-in is written for the controller, and USB debugging is not served. The x64 drivers load only in test-signing mode, which needs Secure Boot off, or with signature enforcement disabled; and the x64 package installs there only because it carries a catalog for each driver, signed with the same test certificate: a package without one, as `3.1.0.0`'s, is refused with error `0xE000022F` in test-signing mode (design record 23 section 5.12). With Secure Boot on, Microsoft's driver signing policy refuses unsigned drivers on 32-bit 10 too, so the 32-bit drivers load there only with Secure Boot off (unread in a guest). The installer gives 10 what it gives 8 and 8.1 (roadmap-usb4 task 43.22): on x64 the connection manager only in test-signing mode, Secure Boot's state read and shown; 10 too makes the connection manager a boot driver when it installs its filter (read on 10 x64 by roadmap-usb4 task 44.32), which from `3.3.0.0` the installer sets back to start on demand ("What this is"). From `3.3.0.0`'s legs (roadmap-usb4 tasks 44.24, 44.25 and 44.28, `runs/run-44.md`; QEMU, 22H2): Sleep under `xhci98.sys` on 10 x86, with a display driver that allows standby (Windows' Basic Display driver turns it off), the controller, its root hub and two sticks working after the wake and a file on each compared; UAS through `xhciuas.sys` on 10 x64 with the test certificate trusted (Windows then ranks the staged `xhciuas.inf` above its own `uaspstor`), a file written and compared; Explorer's "Device can perform faster when connected to USB 3.0" under Windows' own driver, and the USB Tree's notice in its place under `xhci98.sys`; the uninstall of the USB drivers in one run on 10 x86 and x64; on a UEFI guest with Secure Boot on (10 x64), the installer refusing before any change; and Intel's DCH Thunderbolt driver on the emulated controller, left as it is, or replaced after the warning and given back by the uninstall. Still not read: UAS under Windows' own `uaspstor` (QEMU's device fails it under both drivers), Sleep on 10 x64, and memory integrity turned on. Windows 11 (build 22000 and later) is refused |
| Windows 8 and 8.1, 32-bit and x64 | The Thunderbolt 3 / USB4 connection manager (from `3.1.0.0`, alone); from `3.2.0.0` the USB drivers an opt-in install, unticked, untested, and the bundled Stor98 ticked with it (on 8.1 in place of `stornvme` on a tunnelled drive, task 43.59) | Virtual machines: the install alone (roadmap-usb4 task 43.22, 2026-10-22, on the `3.1.0.0` package; the full install, update and uninstall legs and the emulator's tunnel legs are task 43.24's; on 8.1 x64 task 43.59 read the console's install, update and uninstall with a tunnelled drive on Stor98, `runs/run-43.md`): in QEMU guests of Windows 8 x86, 8 x64 and 8.1 x64 with the emulator's Thunderbolt 3 controller, `INSTALL /console /cmonly /yes` read the version, offered the connection manager alone, and installed it on the controller and the filter on its port. On 32-bit 8 it installed after Windows' "can't verify the publisher" prompt and loaded, the drivers being unsigned and the guest having no Secure Boot. On 64-bit 8 and 8.1, in test-signing mode, Windows refused the package outright ("The third-party INF does not contain digital signature information", error `0xE000022F`): the package has no signed catalog, and 64-bit Windows 8 and 8.1 install such a package only on a start with driver signature enforcement disabled (Advanced startup, Troubleshoot, Advanced options, Startup Settings, 7), where it installed and loaded. From `3.2.0.0` the x64 package carries a catalog for each driver, signed with the same test certificate, and 64-bit 8.1 installed it in test-signing mode after the same prompt (roadmap-usb4 task 43.36; 64-bit 8 with the catalogs was not run). There Windows makes `xhcicm.sys` a boot driver (it filters a PCI bridge), and a start without test-signing mode then stopped in Automatic Repair (read on 8 x64; 8.1 x64 expected to, not read), so from `3.2.0.0` the installer installs it on 64-bit 8 and 8.1 only in test-signing mode, with that warning. With Secure Boot on, Microsoft's driver signing policy refuses unsigned drivers on 32-bit 8 and 8.1 too, so the 32-bit drivers load there only with Secure Boot off; Secure Boot on was not read (every guest starts from a legacy BIOS). From `3.2.0.0` the installer reads whether Secure Boot is on and says which case applies, and with it on does not take test-signing mode as on; that reading was made on no guest. The installer's own legs (roadmap-usb4 tasks 43.24 and 43.31, 2026-10-22 to 24, `runs/run-43-24.md`; the `3.2.0.0` sources, still numbered `3.1.0.0`, the x64 half signed with the project test certificate) were read on Windows 8 x86, 8 x64, 8.1 x86 (from the owner's medium of 2026-10-23) and 8.1 x64, in QEMU with the emulator's Thunderbolt 3 controller and enclosure: the connection manager alone installed, updated and uninstalled, with one Add/Remove Programs entry; the enclosure approved, re-enumerated, ejected from `INSTALL /console /eject` and unplugged during writes under Stor98 (the write failed within seconds, no hang); Stor98 x64 loaded on 8 x64 and 8.1 x64, and on 8.1 replaced Windows' own `stornvme` on the tunnel drive; the USB drivers, ticked, ran HID, a hub, sticks, a hot-plug and composite audio and uninstalled back to Windows' own driver, on 8.1 only after a restart and a second run. Under `stornvme` without Stor98 an unplug during writes on 8.1 left the copy blocked, with no error, for over an hour, and plugging the drive in again did not release it. Before `3.2.0.0`, hibernation and Fast Startup stopped Windows with bug check 0x9F with the connection manager installed ("Thunderbolt and USB4: known limitations"). Not read there: a USB device behind a dock, UAS (QEMU's device fails under Windows' own `uaspstor` as under 7), Intel's Thunderbolt software, and Secure Boot on. Under `xhci98.sys` Windows' own USB notices and settings do not apply, as on 10 (design record 13 sections 6.12.11 and 6.14.7): Explorer's "can perform faster" line and Devices and Printers' status lines, Windows' USB error messages, and the USB power settings. From `3.3.0.0`'s legs (roadmap-usb4 task 44.22, `runs/run-44.md`; QEMU): Stand by under `xhci98.sys` on 8.1 x86, with a display driver that allows it, both sticks' files compared after the wake; the uninstall of the USB drivers in one run on 8 x86 and 8.1 x86; and on 8 x86 with a second USB 3 controller left to Windows' own driver, Explorer's "Device can perform faster when connected to USB 3.0" for a stick on the `xhci98.sys` controller, beside the USB Tree's notice for the same stick. Not read: UAS under `uaspstor` (as on 10) and Stand by on 8 x86 |
| Every target, from the release package | | For `2.0.0.0`: the `release` flavour of the integration build, on the ten install legs (Windows 98 SE under NUSB and under SweetLow's stack, ME, 2000, XP, XP x64, and Vista and 7 in both architectures): installed, controller and root hub started, HID, storage with a verified file compare, composite audio bound, the controller's disable and enable, and shutdown, with SuperSpeed storage and UAS at SuperSpeed and at High Speed on most legs. The one defect was Windows ME's controller re-enable, which `2.1.0.0` fixes (the Windows ME row). For `2.1.0.0`: the `release` flavour of its integration build, installed over `2.0.0.0` on eleven virtual machines (the ten legs' systems and a stock Windows 98 SE) and read on the same clauses less SuperSpeed storage and UAS, all passing, the file compare skipped on the stock Windows 98 SE, which has no storage driver (see "Updating from 2.y.y.y"); and the ten install legs from the `2.1.0.0` release package, read after the cut, all passed: the same clauses with SuperSpeed storage and UAS at SuperSpeed and at High Speed, Windows ME's controller re-enable with a mouse attached, a hub disabled and enabled with a mouse and a stick behind it on the seven NT systems, and hidusbf Setup reading the mouse behind a hub on 32-bit XP. For `2.1.1.0`: the ten install legs from the `2.1.1.0` release package, read after the cut, all passed, on the same clauses as for `2.1.0.0`. For `2.3.1.0`: the ten install legs from the `2.3.1.0` release package (2026-10-13), all passed: the install, controller and root hub, HID, a Bulk-Only stick and UAS at SuperSpeed with verified file compares, a hub with composite audio behind it, the controller's disable, enable, remove and rescan, and shutdown; the tone played whole in a WAV capture on ME and 2000, read by the driver's isochronous counters on XP, Vista and 7, and not played on Windows 98 SE, whose USB audio does not play in QEMU. For `2.4.0.0`, before the cut: the `release` flavour of the dry cuts (and the never-published `qemu` flavour where a reading needed it), in QEMU on development host A, on the ten install legs' systems. On each, a fresh install with the new device ids (one root hub, `USB\XHCI98_ROOT_HUB`, and each hub and composite device bound to this driver alone, the root hub's install copying no file) and `XHCICLN` finding nothing to do; a mouse, a Bulk-Only stick and UAS with verified file compares, composite audio, and the controller's disable, enable, remove and rescan; and the update over `2.3.1.0` as "From 2.3.x or earlier" describes it, `XHCICLN` listing the old entries and on 2000 to 7 removing them (`XHCICL64` on the 64-bit systems, where the 32-bit tool refused), and on every NT system read a disable of the controller refused while the tool held it. The helper installed on 2000 and XP and replaced `xhcimsi.sys` on 32-bit Vista and 7; the composite entry's configuration selection with two audio devices (a tone played whole from each on ME, 2000, XP, XP x64 and 7 x64); the quirks' code paths at value `2`, on QEMU's controller, which no list names; a 0.96 controller through the `qemu` flavour's version override, and its refusal at `XhciAccept096` `0`, on 98 SE and 2000; the F6 floppy to the desktop on 32-bit XP and, with the Windows 2000 entry, to the desktop and one restart on 2000. The ten install legs from the release package are read after the signed cut |
| What `2.1.0.0` changed | | All in QEMU on development host A, on the `2.1.0.0` integration build unless the `release` flavour is named. The controller re-enable with devices kept: Windows ME with a mouse (three of three), with a mouse and a stick, with a USB keyboard alone, and with a hub holding a mouse and a stick, every device back and working; Windows 98 SE and 2000 disable and enable with a stick behind a hub and a file compare after, and a 10-cycle soak on each; 32-bit XP disable and enable with no USB keyboard attached (XP refuses the disable while one is). Instance ids from the serial number: a stick moved between root ports kept its one entry and was not found again as new hardware on Windows 98 SE, ME, 2000 and 32-bit XP, and on Vista and 7 in both architectures; moved behind a hub as well on Windows 98 SE, 2000, 32-bit XP and 32-bit Vista, and to a SuperSpeed port on Windows 98 SE. Two sticks with different serial numbers had two entries, and of two with the same one the second was known by its port (Windows 98 SE, 2000 and XP). External hubs as entries of their own, on QEMU's hub, which is a USB 1.1 Full-Speed hub: a mouse and a stick behind it, the tree by connection, the hub unplugged and plugged in again, a device unplugged behind it, and a two-tier chain, on Windows 98 SE, 2000 and 32-bit XP; the tree and the hub unplugged and plugged in again on Vista and 7 in both architectures, the tree on ME and XP x64, and USBView walking into the hub on XP, XP x64, Vista and 7; the hub disabled and enabled in Device Manager with the devices behind it coming back, on Windows 98 SE, and on Windows 2000 and XP with the fix for the defect the first reading found (two disable and enable cycles each in QEMU, the same instance ids and the stick's files intact). A High-Speed hub, its transaction translators, and a USB 3 hub's SuperSpeed half were read in no virtual machine; the owner read a High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub" on real hardware after the cut, and both work. The Power tab: figures in mA on the root hub's and the hub's Power tabs on Windows 98 SE and 2000 (a mouse, a keyboard and an audio device 100 mA, a stick 0 mA), on 32-bit XP, and on XP x64 (a mouse and a keyboard 100 mA, a stick and the hub 0 mA; QEMU's stick and hub ask for 0 mA in their own descriptors), and a hub's Power tab on 32-bit 7. The Advanced tab's figure while audio plays was not read: Windows 98 SE's `USBAUDIO.VXD` fails on playing in QEMU ("Known limitations"). Device names: where a Windows class INF names the device, its name is shown, on every system read; the device's own product name was read in Windows 98 SE's "New Hardware Found" box, for devices no INF names on Windows 98 SE, 2000 and XP, and as Windows 7's "Bus reported device description" in both architectures. hidusbf: 1000, 500 and 250 Hz at a root port and behind a hub on Windows 98 SE under NUSB 3.6 and on ME, at a root port on 32-bit XP, the filter loaded and its setting reaching the controller on a stock Windows 98 SE (`release`), and behind a hub on 32-bit XP with the fix for the defect the first reading found (Setup read the mouse's speed, and 1000, 500 and 250 Hz programmed Interval 3, 4 and 5). Fast polling: not read. The F6 floppy, on the `release` flavour: Windows 2000 SP4 and 32-bit XP SP3 text-mode Setup with a USB keyboard and a USB stick, and the Recovery Console on both; see that section |
| What `2.1.1.0` changed | | The registry values: in QEMU on development host A, on the `release` flavour before `XhciIntelPortSwitch` was added, a fresh install on Windows 98 SE and 2000 writing the seven at their defaults, and an update over `2.1.0.0` on Windows 98 SE, 2000 and 32-bit Windows 7 keeping two values set by hand and adding the missing three ("Registry settings"). The Power tab: on the owner's ThinkPad P14s Gen 1, a SuperSpeed drive declaring 896 mA reading 510 mA and `XHCISNAP` giving its exact figure, a USB 2.0 device reading as before ("The controller's Advanced tab and the root hub's Power tab"). The Intel port switchover: on the owner's Lenovo B490 (`1E31`) under Windows 98 SE with NUSB, on Auto and Smart Auto, and its gate staying closed on the P14s Gen 1 and in QEMU ("Intel 7-, 8- and 9-series chipsets: the port switchover") |
| What `2.2.0.0` changed | | The SuperSpeed fix for Intel Sunrise Point-LP: on the owner's ThinkPad E460 (`8086:9D2F`) under Windows 98 SE, on the `release` flavour of a build taken before the cut with the fix and the enumeration notes in it, a UAS-capable USB 3 stick seen at SuperSpeed from power-on, after two unplug and replug cycles and behind a USB 3 hub, a large file copied there and back and compared clean, and ATTO Disk Benchmark at 242 MB/s writing and 245 MB/s reading at 8 MB transfers; the hub's SuperSpeed and USB 2.0 halves both present after a hot-plug, a replug and a cold boot; `XHCISNAP`'s report naming the port's speed ID 4 as SuperSpeed by its default meaning ("SuperSpeed and UAS"). No Bulk-Only SuperSpeed device was to hand. The speed tables of the ThinkPad P14s Gen 1, the Lenovo B490 and an HP EliteBook 850 G5 were read with `XHCIQUAL`: the first two list ID 4, which is why they never failed, and the HP's Sunrise Point controller publishes the E460's table word for word. In QEMU on development host A, which publishes no speed table, on Windows 98 SE and on Windows 2000 SP4 at two processors under Driver Verifier: a SuperSpeed stick at a root port, a stick behind QEMU's Full-Speed hub, and a SuperSpeed unplug and replug, each with a file compare, read as `2.1.1.0` read beside them, on the integration build and, on Windows 98 SE, the `release` flavour, and again on the final code; with the E460's speed table put in QEMU's place by a test aid, the stick read at SuperSpeed by the default meaning. A SuperSpeed device behind a SuperSpeed hub was not read in QEMU, which models no SuperSpeed hub; the E460 reading above has it. `XhciIntelPortSwitch` 2: on QEMU's controller, which is not Intel, on Windows 98 SE on the `release` flavour, no switchover write and the devices working. The controller tolerance, on the `qemu` flavour with each fault made on purpose, on the same two guests: every fault design record 17 section 5 lists - a lost interrupt, a root port disabled, an over-current, the controller halted, all ones with and without the proof the controller stopped, a transaction error, the retry's failed reset and its two races, the reports nothing owns, and the control endpoint's - each made once on both guests, against a USB mouse with a Bulk-Only copy running on a SuperSpeed stick, and each made to persist on Windows 98 SE and on Windows 2000 but for three not run there (the stale context's and the retry's two races persisting); each transient fault answered with no replug, each persistent one ending in its stated state, and the counters and terminal text read in `XHCISNAP`; at `XhciTolerance` `0`, on both guests, the root port disabled, the over-current, the controller halted, the transaction error and a report nothing owns, each handled as `2.1.1.0` handles it and still counted (the others were not read at `0`). A copy running across a controller recovery fails, since every device is re-enumerated, and on Windows 2000 a mounted drive can come back at Code 31 ("Known limitations"). The three new registry values: written at `1`, `1` and `0` by the install into a driver key that had never had them on Windows 2000 and by an update on Windows 98 SE (a fresh install on Windows 98 SE was not read), and a user's `0` kept by an update on Windows 2000; `XhciIntervalCap` `2` capped the 255 ms status endpoint of QEMU's hub and `XhciAvgTrbEsit` `1` left the devices working, on Windows 98 SE on the `release` flavour |
| What `2.3.0.0` changed | | In QEMU on development host A, on the integration builds before the cut; each item's section says what was read and on which systems. Then the owner's bench of 2026-10-11, on the signed release package. On a Dell Latitude E7250 (`8086:9CB1`) under Windows 7 x64, in test-signing mode: MSI with the routing repair writing nothing, and BackstopDrains 0; the line under `XhciInterruptMode` `1`, with the repair rewriting the route, also with the EHCI disabled; the switchover at `0` leaving a USB 3 stick at USB 2.0 speed and at `1` giving SuperSpeed; two USB audio devices, each under its composite entry, with USBView showing their strings, and the asynchronous one playing on the fix. Under XP x64: the repair rewriting the route at the default, and with it off, the dead line moved to polling and a SuperSpeed stick copying clean. On a ThinkPad E460 under Windows 98 SE: about 18 GB through a SuperSpeed stick on the line and about 4 GB under polling, every copy compared clean. On a ThinkPad P14s Gen 1 under Windows 98 SE, brief checks only. Not read: standby, remote wake, MSI-X, Windows 98 SE on the E7250, and the Low-Speed mouse of GitHub issue 17 ("Untested ground") |
| What `2.3.1.0` changed | | The Setup fix: on the owner's Dell Latitude E7250 (`8086:9CB1`) with the USB 3.0 controller on, Windows XP x64 Setup started from a WinSetupFromUSB stick carrying an nLite-integrated install from a reported machine, on a build with both fixes: text mode reached the partition screen, where `2.3.0.0` stopped with 0x7B; the stick offers both transports and ran as Bulk-Only there. Then, on the `release-x64` build of the fix (2026-10-12, before the version stamp), the install ran from the stick to the desktop, with no device in Device Manager marked with a problem. The stock Windows ME reading is the Windows ME row's, on the `2.3.0.0` package. The shutdown latch, in QEMU on development host A, from the release package: Windows 98 SE (with a selective-suspend client), ME, 2000, XP and 7 x64 each shut down twice with a mouse, a stick, a UAS disk and a hub with audio attached, and every device came back. The re-arm check: under QEMU's MSI-X on Vista x64 and 7 x64, BackstopDrains 0 and UAS at about 100 MB/s, and under MSI and the line, nothing queued by it. The `portsw\` layout: the release package's tree, and the scripts run from `portsw\` on Windows 98 SE, ME, 2000, XP, XP x64 and 7 x64. Not read: a real MSI-X controller, and the reported machine itself |

The virtual machines are QEMU's `qemu-xhci`, which models no SuperSpeed hub,
no link faster than 5 Gbit/s, no Low-Speed device and no High-Speed hub; what
those need is the bench's, or is untested ground.

## Untested ground

Built, and not read on any vehicle. A report from any of these is new
information.

| Area | State |
|---|---|
| Stor98 on 32-bit Windows 8.1, and Stor98 x64 on 8, 8.1 and 10 | 32-bit Windows 8.1 ran Stor98 through the installer from the owner's medium of 2026-10-23 (task 43.24, `runs/run-43-24.md`); 32-bit Windows 10 was run on 22H2 only. On 8 x64, 8.1 x64 and 10 x64 the `3.1.0.0` x64 package, which has no catalogs, was refused ("The third-party INF does not contain digital signature information"; read in test-signing mode; roadmap-usb4 task 43.23, `runs/run-43.md`). From `3.2.0.0` `stor98.inf` names `stor98.cat`, and on 8.1 x64 and 10 x64 in test-signing mode the package with it was taken into the driver store after the publisher prompt (task 43.36, the catalogs signed in the guest with a certificate of its own); no drive was bound to it there. In task 43.59 (`runs/run-43.md`), signed in the guest and trusted, Stor98 x64 ran tunnelled drives on 8.1 x64 and, without `DmaRemappingCompatible` (task 43.59s, as the INF ships), on 10 x64, a surprise unplug failing the transfer at once on both; the owner-signed package's leg is owed with the bench candidate, and 8 x64 with the catalogs ran it through the installer, with the owner-signed package (task 43.24) |
| SuperSpeed isochronous transfers | Built from the specification against host tests. No SuperSpeed isochronous device has been held and QEMU models none |
| High Speed isochronous transfers, and the isochronous ring page | No High Speed isochronous device (a camera, USB Audio 2.0) has run on a real computer. Since the planned isochronous ring work (design record 13 section 11.8, not yet in a release), an isochronous endpoint serviced more than once a frame gets a ring of its own when the event ring has room: since task 47.30 (section 11.10) the event ring has 1,024 entries where the start gets its memory, and then the first three such pipes get 254 TRBs and up to 128 packets a request (a fourth 91), so a 3 x 1024-byte camera endpoint holds about 15.9 to 31.75 ms in flight and a stream of small packets 31.75 ms; on the 256-entry ring (the fall-back, or `XhciEventRingTrbs` 256) it is 112 TRBs and packets (120 until task 47.5 gave an isochronous pipe eight transfer records, section 11.9), about 7.0 to 9.3 ms for the camera endpoint and 14 ms for small packets, against 62 on the shared pool ring. The 1,024-entry segment has run on no real controller. A client keeps a stream continuous with at least two requests in flight, each at most half of that. An isochronous stream also interrupts at most every eighth packet and at each request's end (BEI), from xHCI 1.0; whether that saving shows on real hardware is unread |
| SuperSpeedPlus links, by mode | Accepted at the trained rate, built from the specification against host tests. Each mode is untested: Gen 2x1 (10 Gbit/s on one lane), Gen 1x2 (10 Gbit/s on two lanes, the same rate as Gen 2x1 and a different mode), and Gen 2x2 (20 Gbit/s). No Gen 2 device was read on any hardware; no 20 Gbit/s port is held |
| SuperSpeedPlus isochronous transfers | Built from the specification against host tests; no vehicle |
| SuperSpeedPlus hubs | Built from the specification against host tests; no vehicle |
| SuperSpeed hubs, on every target but Windows 98 SE | Read on real hardware under Windows 98 SE only; no virtual machine models one. On Windows 2000, which has no bench, built from the specification and untested |
| High-Speed hubs, single- and multi-TT, and Full and Low Speed devices behind them, on every target but Windows 98 SE | Read on real hardware under Windows 98 SE only. Virtual machines model only a Full-Speed hub, so on every other target the High-Speed paths rest on host tests |
| Low- and Full-Speed devices behind a built-in hub whose USB 2.0 speed table lists only High Speed (`XhciUsb2HubSpeedFallback`; the VIA VL805, GitHub issue 18) | Built from the specification, the reporter's logs and other systems' practice against host tests; on a VL805 reported working on `2.3.0.0` by the owner, not yet read in detail, and not run on any other controller with such a table. QEMU's controller lists no table, so its legs read only that nothing else changed |
| Controllers below xHCI 1.0 (0.96: NEC/Renesas uPD720200 and uPD720200A, ASMedia ASM1042 and others) | Accepted by default (`XhciAccept096`), driven as xHCI 1.0 controllers but for the fields Linux writes differently on them (TD Size in 0.96's units, the control transfer's Transfer Type, the isochronous burst counts, and no isochronous Frame ID) and with the capability register at 1Ch ignored, so a resume from standby starts the controller afresh (its devices found again, but those `XhciResumeKeep` keeps from `3.4.0.0`); built against host tests and Linux's handling of the same parts; read on no 0.96 controller. Linux's chained isochronous Link TRBs for NEC and AMD 0.96 parts are not applied, so isochronous transfers (audio, video) on those parts are the least certain. Set `XhciAccept096` to `0` to refuse such a controller as `2.3.1.0` did |
| A UAS-only device at SuperSpeed on a controller that cannot stream | Built from the specification against host tests; no such controller held |
| Low- and Full-Speed polling above 1000 Hz (`XhciFastPollFsLs`) | Outside the xHCI specification, built against host tests; read on no real controller. Value `4`'s programming was read in QEMU on 32-bit Windows 7 and Windows 98 SE ("XhciFastPollFsLs"), and its `3.1.0.0` codes on Windows 98 SE alone, a Full-Speed device behind a hub included |
| The Intel port switchover beyond two machines | Read on one `1E31` machine under Windows 98 SE and on one `9CB1` machine, the Dell Latitude E7250, under Windows 7 x64 ("Intel 7-, 8- and 9-series chipsets: the port switchover"). Device ids `8C31`, `9C31`, `8CB1` and `8D31` and every other system rest on Linux's handling of the same chipsets and on host tests of the sequence |
| `XhciIntelPortSwitch` `2` on an unlisted Intel controller | Built and host-tested; read on no unlisted Intel controller. At your own risk: on one without the switch registers it writes registers of unknown meaning ("Value 2") |
| A finer system timer while polling (`XhciPollFineTimer`, off by default), and the helper `xhcihlpr.sys` | New in `2.4.0.0`, built against host tests and static checks: the helper's imports were read in the kernels and HALs of 32-bit Windows 2000 SP4, XP SP3, Vista SP2 and 7 SP1, statically. The finer timer was read only in QEMU virtual machines without hardware acceleration, polling, on 7 x64, 7 x86 and Vista x86, where while it was held it slowed the guest's clock to a tenth to seven tenths of real time and USB keyboard input was lost; so it ships off; on 32-bit XP and 2000 the guest's clock kept nine tenths of real time or more while it was held. Real hardware is unread. Read in QEMU before the release besides: the helper's install on 2000 and XP with no restart and its replacement of `xhcimsi.sys` on 32-bit Vista and 7; a held request given back at the controller's stop (7 x86) and at a hibernation (7 x86 and x64), and taken again at the next copy after the resume (Vista x86 and 7); a clean shutdown during a polled copy with the finer timer on, on 2000 and 7 x86; two controllers polled at once, holds equal to releases on each (2000, 7 x86). A recovery with the request held, and the finer timer's effect on a copy's rate or on audio, were not read ("XhciPollFineTimer: a finer system timer while polling") |
| A High Speed hub and a USB 3 hub under the `2.4.0.0` ids (`USB\XHCI98_HUB`, `USB\XHCI98_HUB30`) | Built against host tests and the INF gate. Virtual machines model only a Full Speed hub, which takes the same id and code path as a High Speed one (`USB\XHCI98_HUB`) and stands for it before the release; a High Speed hub and a USB 3 hub's two halves under the new ids are read on real hardware after the release |
| Linux's controller quirks (`XhciMsiAvoid` and the thirteen others, and `XhciIntervalCap` on `1002:7316`) | Built from Linux's handling and the xHCI specification against host tests. None has been read on a controller it lists, though the project's E460 (`9D2F`), B490 (`1E31`) and E7250 (`9CB1`) and a user's VL805 are listed for some ("Linux's controller quirks"). QEMU's controller is on no list. What `XhciIntelShutdownToEhci` and `XhciShutdownReset` do at shutdown is visible only in an external capture of that shutdown |
| hidusbf on a composite device's own entry (`USB\XHCI98_COMPOSITE`) | New in `2.4.0.0`, built against host tests. QEMU offers no keyboard-and-mouse composite device, so the rate set there reaching a function is owed to a real composite device, by passthrough or on a machine, after the release (the composite entry's own selection was read in QEMU with USB audio devices, "What `2.4.0.0` adds"); whether hidusbf's Setup offers the entry is unread ("SweetLow's hidusbf: a mouse's polling rate") |
| Installing Windows 2000 or XP, or the Recovery Console, from the F6 floppy on real hardware | Read in virtual machines: text mode on Windows 2000 and 32-bit XP, the Recovery Console on both, installs to the desktop on 32-bit XP and XP x64, and Windows 2000 with its own entry and the three files from its CD to the desktop and one restart. Never on real hardware; a repair install, Windows 2000's Emergency Repair Disk and Windows XP's Automated System Recovery on no vehicle |
| AMD USB4 hosts (`1022:1668`, `1022:1669`) under the software connection manager | Read on no AMD hardware, and the INF's match on these two ids has never run. Read in QEMU, on the emulator's `amd` host model: the emulator's host interface with AMD's router identity (vendor `0438`, device `0208` to `020B`), on Windows 7, 2000 and 98 SE with an ASMedia ASM2464 enclosure behind it (task 43.45, 2026-10-22). The driver read the AMD router's identity and applied its quirk-table row (CLx left off). It tunnelled PCIe to the NVMe disk under Stor98, ejected from XHCITR, and handled unplug, replug and surprise removal; the disk compared clean on every target. Resume read on Windows 7 (hibernate) only. Windows 2000 cannot sleep in that virtual machine. Stand by and resume with a USB4 or Thunderbolt disk on Windows 98 SE is untested on every host. In QEMU the connection manager resumed and set the tunnel up again, but Windows 98 SE's own post-standby fault stopped the disk from being read; the real machine at hand cannot put Windows 98 SE in Stand by |

## What the release notes' current sections rested on

Each passage below was taken out of a current section of the release notes
on 2026-11-06, as it stood for `3.4.1.0`; the paragraph it came from is
named by its first words, and the section text that remains says what the
driver does.

### What this is

- (In the paragraph beginning "A link that trains above 5 Gbit/s
  (SuperSpeedPlus: USB ...") That ground is untested; see "Untested ground".
- (In the paragraph beginning "Keep test-signing mode on while the
  Thunderbolt 3 ...") Read in virtual machines on 7 x64, 10 x64 and Vista
  x64.
- (In the paragraph beginning "Keep test-signing mode on while the
  Thunderbolt 3 ...") stopped 64-bit Windows 8 in Automatic Repair (8.1 and
  10 are expected to do the same; not read), and
- (In the paragraph beginning "F8 -> "Disable Driver Signature Enforcement"
  still works ...") (read on both systems for the `2.1.1.0` package).

### Controller names in Device Manager (new in `3.2.0.0`)

- (In the paragraph beginning "Every name is at most 46 characters: the
  longest ...") (the word "Properties" after a long name can still be cut in
  the title), read with test names.
- (In the paragraph beginning "Every name is at most 46 characters: the
  longest ...") "xHCI98 USB 3 Controller (Red Hat QEMU xHCI) Properties",
  read on 98 SE and ME. The emulated controllers' names are in "The virtual
  machines' controllers in the name table", above.
- (In the paragraph beginning "Every name is at most 46 characters: the
  longest ...") the installer still never installs over another maker's
  driver. Read in virtual machines.

### Requirements

- (In the paragraph beginning "Operating system | Windows 98 SE (4.10.2222)
  or ...") "Targets and their standing" says what each was read on; Windows
  XP x64 and Windows 7 x64 have also run on the owner's Dell Latitude E7250.
- (In the paragraph beginning "USB stack | None. The driver replaces the
  ...") Windows ME needs no USB 2.0 stack either: the `2.3.0.0` package was
  read on a stock Windows ME with none installed (the Windows ME row), and
  the earlier readings under SweetLow's USB 2.0 stack stand.
- (In the paragraph beginning "Controller | An xHCI controller presenting
  PCI class ...") A controller with no USB 2.0 protocol port at all is
  accepted, built from the specification and untested: no such controller
  has been held |

### Windows 98 SE and ME: the stock install, and storage

- (In the paragraph beginning "Windows ME needs no USB 2.0 stack either. On
  ...") through Windows ME's own class drivers (read on `2.3.0.0`,
  2026-10-12).
- (In the paragraph beginning "The seven files can also be installed on
  ...") and hangs - even with all seven in the folder (read in a virtual
  machine, roadmap-usb4 task 43.5). With `USBNTMAP.INF` in `C:\WINDOWS\INF`
  first, the disk installed as "USB Disk" with a drive letter and no
  restart.
- (In the paragraph beginning "The seven files can also be installed on
  ...") After that the storage devices read here, Bulk-Only and UAS, worked
  with no extra step, and a UAS device that sat at Code 2 started at the
  next boot.
- (In the paragraph beginning "The seven files can also be installed on
  ...") A stock Windows 98 SE needs none of NUSB's other files: on one with
  none of them, the seven alone carried USB sticks and a UAS disk, a 40 MiB
  file written and compared (read in a virtual machine, roadmap-usb4 task
  43.5).

### Installing

- (In the paragraph beginning "It installs as "xHCI98 USB 3 Controller"
  ("xHCI98 ...") until the restart (read with `2.3.x`'s `xhcimsi.sys`; with
  `xhcihlpr.sys` not yet read).
- (In the paragraph beginning "The first UAS device asks for the UAS ...")
  Vista's names the device by its own product string (QEMU's UAS disk read
  "USB Attached SCSI HBA"), and it becomes
- (In the paragraph beginning "The first UAS device asks for the UAS ...")
  Browse my computer for driver software, the same directory. Read in
  virtual machines.
- (In the paragraph beginning "An external hub installs as "xHCI98 USB Hub"
  ...") choose Continue Anyway. Read in virtual machines; on the other
  systems how a hub installs was not recorded.

### Installing / Installing, updating and uninstalling with INSTALL.BAT (new in `3.0.0.0`)

- (In the paragraph beginning "Windows 2000, XP, XP x64, Vista and 7: ...")
  keep that medium at hand (whether the tool's run moves that to the hard
  disk is read on 2000 and XP in virtual machines, not on XP x64).
- (In the paragraph beginning "Windows 8, 8.1 and 10 (10 from `3.2.0.0`,
  ...") then with a warning that they are untested there, and the bundled
  Stor98
- (In the paragraph beginning "Windows 8, 8.1 and 10 (10 from `3.2.0.0`,
  ...") report a Windows 10 version and are treated as 10, untested.
- (In the paragraph beginning "Windows 8, 8.1 and 10 (10 from `3.2.0.0`,
  ...") make it a boot driver when they install its filter (10 read by
  roadmap-usb4 task 44.32), and under
- (In the paragraph beginning "Windows 8, 8.1 and 10 (10 from `3.2.0.0`,
  ...") stopped 64-bit 8 in Automatic Repair (task 43.22; 8.1 and 10
  expected to, not read);
- (In the paragraph beginning "Windows 8, 8.1 and 10 (10 from `3.2.0.0`,
  ...") (Windows Update may also put Intel's back). On Windows 10 the
  drivers were read in virtual machines of 22H2, each installed by a script,
  and the tool's console install and uninstall only by task 43.59's legs on
  10 x86 and x64; its full matrix there is still to run (roadmap-usb4 tasks
  43.23, 43.33, 43.36 and 43.37).

### Installing / Installing Windows 2000 or XP, or using the Recovery Console: the F6 floppy

- (In the paragraph beginning "Text mode copies the three files to the new
  ...") Windows 2000 keeps three inert service entries named `usbd`,
  `hidclass` and `hidparse` (set to start on demand; nothing starts them)
  from this step. Read with Windows 2000 SP4 in QEMU virtual machines
  (below).
- (In the paragraph beginning "Installing Windows 2000 or XP. GUI-mode Setup
  then ...") and GUI-mode Setup then installed no driver on the controller
  (32-bit XP, read in a virtual machine); this release's
- Read on neither, and untested ground: a repair install, Windows 2000's
  Emergency Repair Disk and Windows XP's Automated System Recovery, which
  run the same text mode.
- (In the paragraph beginning "A USB drive present at the partition screen
  ...") Unplug the USB drives you do not need, or Windows installs to the
  next letter (Windows XP x64 went to E: in a test).
- (In the paragraph beginning "Installing Windows 2000 with the first entry,
  "(32-bit ...") the new system then cannot load that class driver at its
  first start. Read in QEMU virtual machines: with a keyboard, a mouse and
  an audio device behind a hub, and with a keyboard and a USB stick at root
  ports, on `2.3.1.0` and `2.4.0.0` alike (`hidusb.SYS`); and with a USB
  stick at a root port and an audio device behind a hub, no keyboard or
  mouse, on `2.3.1.0` and `2.4.0.0` alike (`usbstor.SYS`). What works on
  Windows 2000 from the floppy, as read: text mode itself,
- What has run, in QEMU virtual machines on development host A only; no
  install from the floppy and no Recovery Console has been run on real
  hardware:
- (In the paragraph beginning "Text mode, on the `2.1.0.0` integration
  build's `release` ...") - **Text mode, on the `2.1.0.0` integration
  build's `release` flavour**: on Windows 2000 SP4 and 32-bit XP SP3, with a
  USB keyboard and a USB stick at root ports, the keyboard answered in text
  mode and the stick was listed at the partition screen. On Windows 2000 a
  keyboard behind a hub answered too (on the `qemu` flavour). Before the
  first-report wait neither the keyboard nor the stick worked in Windows
  2000's text mode.
- (In the paragraph beginning "The Recovery Console, on the same build: on
  ...") - **The Recovery Console, on the same build**: on Windows 2000 SP4
  and 32-bit XP SP3, with only the USB keyboard, the console logged in to
  the installed Windows, ran `dir` and exited.
- (In the paragraph beginning "Installs to the desktop, on the build of
  ...") - **Installs to the desktop, on the build of 2026-10-04 before the
  first-report wait**: on 32-bit XP SP3 and XP x64 SP2, Setup completed with
  the controller, the root hub and USB storage working, once GUI-mode
  Setup's prompts were answered from a PS/2 keyboard. Windows 2000's
  GUI-mode Setup showed no prompt and completed on that build. XP x64's text
  mode was not read again on the `2.1.0.0` build, and its Recovery Console
  not at all.
- (In the paragraph beginning "Windows 2000 SP4 with the Windows 2000 entry,
  ...") - **Windows 2000 SP4 with the Windows 2000 entry, on the `2.4.0.0`
  branch build** (unsigned, the steps above followed as written, the three
  files from the SP4 CD): with a USB keyboard, mouse and audio device behind
  a hub, every key from F6 on typed on the USB keyboard, and the first start
  went on into GUI-mode Setup, where the USB keyboard and mouse worked,
  through every page to the desktop; with a USB stick at a root port and no
  USB keyboard or mouse, the stick was listed at the partition screen and
  the install went on to the desktop as well, the stick readable there. Both
  were read again after one ordinary restart: Device Manager showing no
  problem, no error in the event log, and the driver `2.4.0.0`. The
  stick-only install reset the virtual machine once by itself during
  GUI-mode Setup's "Installing Components", cause not found; it did not
  happen again in a repeat from the same point or at any later start. With
  the Windows 2000 entry and a floppy without the three files, Setup asked
  for them in drive A: as described above; with a floppy holding them put in
  its place and Enter pressed, the copy went on and text mode finished. On
  the same build, 32-bit XP SP3 with the first entry went on into GUI-mode
  Setup, which installed the controller from the floppy's copies with no
  error.
- (In the paragraph beginning "Slipstreaming into a Windows 2000 CD. If you
  add ...") What was read: those two sections in the SP4 CD's
  `TXTSETUP.SIF`, and the stop and its cure on the F6 route above. A
  slipstreamed CD itself was not carried through: one attempt, adding the
  driver as an `[InputDevicesSupport]` entry with its own file section, did
  not get the driver loaded in text mode, and that was not pursued.

### Installing / Updating from 2.y.y.y

- (In the paragraph beginning "Install a newer `2.y.y.y` over an older one
  with ...") ; that update over `2.1.0.0` was read on Windows 98 SE (Have
  Disk), 2000 and 32-bit Windows 7, and an update over `2.1.1.0` adds the
  three values new in `2.2.0.0` the same way. What follows was read for
  `2.1.0.0` over `2.0.0.0`.
- (In the paragraph beginning "Install a newer `2.y.y.y` over an older one
  with ...") Read in QEMU virtual machines on development host A, on the
  `release` flavour of the `2.1.0.0` integration build installed over the
  published `2.0.0.0`, on all ten install legs' systems and a stock Windows
  98 SE:
- (In the paragraph beginning "Windows XP, XP x64, Vista and 7: read ...") -
  **Windows XP, XP x64, Vista and 7**: read with a command-line driver
  update in place of Device Manager, so the dialogs on Device Manager's
  route (the steps above) were not read for this update. XP, XP x64 and
  32-bit Vista showed their unsigned-driver warning once; on every one the
  update took effect at once, and no restart was asked for.
- (In the paragraph beginning "Windows Vista and 7: use Have Disk. Update
  ...") Have Disk, and the `release-` directory instead. Read on 32-bit
  Vista and 7, from `2.3.0.0` to `2.3.1.0` (roadmap task 38.5).
- (In the paragraph beginning "The root hub's Driver tab keeps showing
  `2.0.0.0` (on ...") On the other systems use Have Disk; the root hub's
  update was not read there.
- (In the paragraph beginning "On Vista and 7, Have Disk does not take ...")
  or use a build with a newer `DriverVer` (read on the E7250 under Windows 7
  x64).
- (In the paragraph beginning "An update to `2.4.0.0` adds
  `XhciPollFineTimer` at `0` (off) ...") when set to `1`, starts then. Read
  in QEMU virtual machines on the `2.4.0.0` dry cuts: over `2.3.0.0` and
  `2.3.1.0` on 32-bit Vista and 7, and over `2.3.1.0` on 2000 and XP.
- (In the paragraph beginning "Deleting the driver keeps the older packages.
  On Vista ...") the next time Windows finds the controller it may install
  one of them (read on 32-bit Windows 7).

### Installing / From 2.3.x or earlier

- (In the paragraph beginning "From `2.4.0.0` the root hub, every hub and
  every ...") What follows was read in QEMU virtual machines on the
  `2.4.0.0` dry cuts, updating the published `2.3.1.0` on Windows 98 SE
  (under NUSB and under SweetLow's stack), ME, 2000, 32-bit XP, XP x64, and
  Vista and 7 in both architectures (design record 13 section 10.13 is the
  design):
- (In the paragraph beginning "The devices beneath follow. A device or a
  ...") and the new one take the next free one; not read).
- (In the paragraph beginning "Before updating on Windows XP, XP x64 or
  ...") so they cannot answer them (read on XP, where a PS/2 keyboard and
  mouse answered every one).
- (In the paragraph beginning "Windows XP shows "will not work until you
  ...") on XP `XHCICLN` already found every check passed before that restart
  (its removal was read after it).
- (In the paragraph beginning "Two controllers (read on 32-bit XP): one
  controller's ...") - **Two controllers** (read on 32-bit XP): one
  controller's update
- (In the paragraph beginning "Two controllers (read on 32-bit XP): one
  controller's ...") until it was uninstalled and found again (an update of
  the second controller itself was not read).

### Installing / NVMe drives behind Thunderbolt or USB4 (new in `3.0.0.0`)

- (In the paragraph beginning "On Windows 98 SE and ME, `98-me\stor98\` also
  carries ...") that a test build of this release placed (that name stopped
  Stor98 from starting). Read so far on the 98 SE emulator and on the
  owner's 98 SE machine, where 2 MiB unbuffered writes ran about four times
  and reads about five times as fast as without it, and on the ME emulator:
  the install, 1 and 2 MiB transfers verified with the drive plugged after
  the desktop, and two surprise removals mid-transfer that reported errors
  and hung nothing. Its remaining legs (roadmap-usb4 task 43.46) are still
  owed.

### Devices and hubs in Device Manager

- (In the paragraph beginning "New in `2.1.0.0`. Every reading below is a
  QEMU ...") Every reading below is a QEMU reading on development host A, on
  the `2.1.0.0` integration build; the row "What `2.1.0.0` changed" under
  "Targets and their standing" lists them per system.
- (In the paragraph beginning "A device keeps its entry when it moves. ...")
  is not found again as new hardware. Read with a stick moved between root
  ports on every system but XP x64, behind a hub on Windows 98 SE, 2000, XP
  and 32-bit Vista, and to a SuperSpeed port on Windows 98 SE.
- (In the paragraph beginning "A device keeps its entry when it moves. ...")
  while both are plugged in (read on Windows 98 SE, 2000 and XP). `XHCISNAP`
  counts that case
- (In the paragraph beginning "External hubs have entries of their own. A
  ...") brings back the devices behind it (read twice each on Windows 98 SE,
  2000 and XP in QEMU).
- (In the paragraph beginning "External hubs have entries of their own. A
  ...") leaves the hub running; close the window first (read on Vista in
  both architectures). QEMU's only hub is a USB 1.1 Full-Speed hub, so a
  High-Speed hub's entry and a USB 3 hub's "xHCI98 USB 3.x Hub" (since
  `3.3.0.0` "xHCI98 USB 3 Hub") were read in no virtual machine; the owner
  read both on real hardware after the cut, and they work.
- (In the paragraph beginning "Devices are named by their product name where
  ...") shows for a device no INF names, such as "QEMU USB SERIAL" on
  Windows 98 SE and "QEMU USB CCID" on Windows 2000 and XP, in Windows 98's
- (In the paragraph beginning "Devices are named by their product name where
  ...") every character outside plain ASCII is shown as `?`, since how those
  systems would convert it has not been read; the NT systems show the name
  as the device sends it. A name with characters outside plain ASCII was not
  read on any system.
- (In the paragraph beginning "A composite device has an entry of its ...")
  a mouse with a second interface, and QEMU's own audio device - appears
- (In the paragraph beginning "A composite device has an entry of its ...")
  the parts then installing silently (read in QEMU with its audio device;
  the other systems are being read).
- (In the paragraph beginning "A composite device has an entry of its ...")
  Windows installs each from the drivers it used before. The update from
  `2.2.0.0` has not been read on any system yet; a setting made on a part's
  own entry
- (In the paragraph beginning "USBView shows a device's strings (new in
  `2.3.0.0`; ...") so SweetLow's USBView shows each device's and hub's
  strings. Read in QEMU on Windows 2000, XP, and Vista and 7 in both
  architectures, with a stick, a mouse, a tablet and a hub, with stick
  copies compared clean and the mouse moving while USBView refreshed every
  two seconds, and devices unplugged in the middle; Windows 98 SE and ME are
  being read.

### SweetLow's hidusbf: a mouse's polling rate

- Read in QEMU virtual machines on development host A, with hidusbf's
  release of 2026-10-03 and the interval the driver programmed read back
  from the controller's own endpoint context: a mouse set to 1000, 500 and
  250 Hz was polled every 1, 2 and 4 ms, and at its own rate with the
  setting off, at a root port and behind a hub on Windows 98 SE under NUSB
  3.6 and on Windows ME, and at a root port on 32-bit XP (QEMU's mouse runs
  at High Speed on a root port and at Full Speed behind QEMU's hub). On a
  stock Windows 98 SE, on the `release` flavour, the filter loaded and its
  setting reached the controller. hidusbf works behind a hub on XP too:

### SuperSpeed and UAS

- (In the paragraph beginning "A UAS drive that runs Bulk-Only (from
  `3.3.0.0` ...") is expected to keep `usbstor.sys` after the bus offers it
  as UAS. That case itself has not been read on a real drive yet; what was
  read, on Windows 7 with `usbstor.sys` bound to a UAS drive's entry by a
  test INF, is the entry keeping `usbstor.sys` across an unplug and replug,
  the tree's note, and the UAS driver taking the drive once the entry was
  uninstalled (and the test INF removed) and the drive plugged in again.
- (In the paragraph beginning "SuperSpeed UAS needs streams. A UAS-only
  device at ...") behind a SuperSpeed hub it is refused in place. That
  branch is built from the specification and has run on no such controller.
- (In the paragraph beginning "Intel Sunrise Point-LP (`8086:9D2F`):
  SuperSpeed since `2.2.0.0`. Every ...") On a controller whose table lists
  ID 4, as every other one read so far does, nothing changes. Read on the
  E460 under Windows 98 SE: a UAS stick at SuperSpeed at power-on, after
  replugs and behind a USB 3 hub, a file compared clean, about 242 MB/s
  writing and 245 MB/s reading (ATTO, 8 MB transfers), and both halves of
  the hub present.

### Intel 7-, 8- and 9-series chipsets: the port switchover

- (In the paragraph beginning "Turning it on, or off again: `portsw.vbs`.
  The ...") The script never sets `2` (below); that is set by hand
  ("Registry settings"). Read in virtual machines on Windows 98 SE, 2000 and
  Windows 7 x64, where it showed, set and cleared the value; QEMU's
  controller is not an Intel one, so the switchover itself was not exercised
  there.
- (In the paragraph beginning "In one double-click: `portsw-on.bat` and
  `portsw-off.bat`. Beside `portsw.vbs`, ...") **Then restart Windows.**
  Neither sets `2`. Read in virtual machines on Windows 98 SE, ME, 2000, XP
  and Windows 7 x64 (run as administrator, and not, where the script asks
  for the rights itself), each setting and clearing the value.
- (In the paragraph beginning "A USB 3 stick at USB 2.0 speed. ...") (A USB
  2.0 hub or cable in between gives the same speed, and the switchover does
  not change that.) This is what the owner's Dell Latitude E7250 showed on
  2026-10-11 with the switchover at its default `0`; at `1`, after a
  restart, the stick linked at SuperSpeed.
- What it rests on. Read on 2026-10-05 by the owner on a Lenovo B490 (device
  id `1E31`) under Windows 98 SE with NUSB, with the firmware's USB 3.0
  setting on Auto: before Windows started, `XHCIQUAL` found the connectors
  on the USB 2.0 controller (both routing registers 0); under the driver
  every step was accepted and each register read back equal to firmware's
  mask; a SuperSpeed drive on a blue connector linked at 5 Gbit/s and a file
  made the round trip, and a USB 2.0 device worked under the driver; the one
  black connector stayed with NUSB's USB 2.0 controller; disabling the
  controller in Device Manager gave a device on a blue connector back to
  NUSB's driver, and enabling it took the device back; with
  `XhciIntelPortSwitch` at `0` a device on a blue connector stayed with the
  USB 2.0 controller; a mouse plugged into a blue connector at boot ended up
  under the driver; and after a shutdown the machine stayed off and the next
  boot found both registers at 0 again. On Smart Auto, the registers before
  Windows, the switchover, the SuperSpeed link and the shutdown read the
  same. On a ThinkPad P14s Gen 1, whose controller (`02ED`) has no USB 2.0
  controller beside it, the report showed `psw.gate` 0 and no other `psw.`
  record, and every external connector worked as before. These are the
  owner's readings, clause by clause; the B490's logs stayed on the B490.
- Not read: a standby and resume (Windows 98 SE offers none on the B490);
  any system but Windows 98 SE, until the E7250 (`9CB1`) under Windows 7 x64
  at the `2.3.0.0` bench; the other four device ids, which rest on Linux's
  handling of the same chipsets; and value `2` on any unlisted Intel
  controller (see "Untested ground"). The readings above were taken on
  `2.1.1.0`; `2.2.0.0`'s change to the hand-back and value `2` rest on host
  tests of the sequence and on the virtual-machine reading in "Targets and
  their standing".

### Controller faults: what the driver does about them

- (In the paragraph beginning "From `3.2.0.0` (roadmap-usb4 task 43.64), a
  cancelled transfer that ...") which the xHCI specification forbids. Read
  in the specification and the sources, with a host test; not run on a
  controller.
- (In the paragraph beginning "Also from `3.2.0.0` (roadmap-usb4 task
  43.67), a device whose ...") a device may now hold up to 16 of the
  controller's 32 transfer rings. Read in QEMU on 98 SE with an emulated pad
  (7 endpoints) and wireless receiver (16 endpoints), both configured whole.
- What it rests on. QEMU raises none of these faults, so each was made on
  purpose in the never-published `qemu` flavour and read in QEMU virtual
  machines on development host A, on Windows 98 SE and on Windows 2000 SP4
  at two processors under Driver Verifier. Every fault the project's test
  build can make (the lost interrupt, a root port disabled, an over-current,
  the controller halted, all ones with and without the proof the controller
  stopped, a transaction error, the retry's failed reset and its two races,
  the reports nothing owns, and the control endpoint's) was made once on
  both guests at a root port, against a USB mouse with a file copy running
  on a SuperSpeed stick, and made to persist on both but for three run on
  Windows 98 SE alone: a stale context and the retry's two races,
  persisting. Each transient fault was answered with no replug, each
  persistent one ended in its stated state, and the counters read in
  `XHCISNAP` as above. At `XhciTolerance` `0`, on both guests, a root port
  disabled, an over-current, the controller halted, a transaction error and
  a report nothing owns were each handled as `2.1.1.0` handles it and still
  counted; the other faults were not read at `0`. QEMU has no SuperSpeed
  hub, so nothing was read behind one. The readings found one defect, fixed
  before the release: a controller that had reached its limit of recoveries
  kept its devices' transfers waiting, so a read of a drive or a disable of
  the controller hung; it now removes the devices. Its review found that a
  suspend could save the controller's state with transfers still queued,
  also fixed. The behaviours have not been seen to act on real hardware: no
  real controller has raised one of these faults under this driver yet, and
  the reported machine behind this work has not run this release.

### Windows Vista and 7: message-signalled interrupts (new in `2.3.0.0`)

- (In the paragraph beginning "32-bit Vista and 7: the one 32-bit
  `xhci98.sys` ...") held the controller at code 1 until a restart (read
  with `xhcimsi.sys`, and again with `xhcihlpr.sys` on the `2.4.0.0` dry
  cut, after an update and after a fresh install).
- (In the paragraph beginning "Going back to an earlier version on Vista
  ...") ("Updating from 2.y.y.y", "Deleting the driver keeps the older
  packages"). The downgrade was read once, in a 32-bit Windows 7 virtual
  machine with an earlier test build: it ran on the line, and the `xhcimsi`
  service stayed.
- `XHCISNAP` shows which delivery the controller uses. These behaviours were
  read in QEMU guests (the readings are listed in the run record), and on
  real hardware on the owner's Dell Latitude E7250 under Windows 7 x64: MSI
  by default, with nothing for the backstop to drain, and the line under
  `XhciInterruptMode` `1`. MSI-X has not been read on real hardware.

### Registry settings

- Read in QEMU virtual machines on development host A, on the `release`
  flavour, before `XhciIntelPortSwitch` was added: a fresh install on
  Windows 98 SE and 2000 wrote the seven at their defaults, and an update
  over `2.1.0.0` on Windows 98 SE, 2000 and 32-bit Windows 7, with
  `XhciLogVerbosity` and `XhciFirstEnumWaitMs` set by hand first, kept both
  and added the three that were missing, `XhciForceBulkOnly`,
  `XhciFastPollFsLs` and `XhciFirstEnumPortMs`. The other systems install
  through INF lines with the same values and the same flag, and were not
  read for this. The three values new in `2.2.0.0` join the same INF lines
  with the same flag. Read in QEMU virtual machines on development host A,
  before the cut: on Windows 2000 SP4 the install wrote `XhciTolerance` `1`,
  `XhciIntervalCap` `1` and `XhciAvgTrbEsit` `0` into a new driver key, and
  an update with `XhciTolerance` set to `0` by hand kept the `0`; on Windows
  98 SE an update over a driver key that never had them wrote all three. A
  fresh install on Windows 98 SE, and a value kept by an update there, were
  not read.

### Registry settings / XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs: how the controller's events arrive

- (In the paragraph beginning "`XhciPollActiveMs` | `1` | While polling, how
  often ...") held between the active and the idle period: 8 ms with QEMU's
  mouse, on every system.
- (In the paragraph beginning "`XhciPollActiveMs` | `1` | While polling, how
  often ...") so `1` means "as often as Windows allows"; 1 ms was read with
  transfers outstanding on a busy XP, Vista and 7 in QEMU |
- (In the paragraph beginning "Neither period matters while the controller
  has a working ...") Polling adds up to one period of delay to each
  completed transfer. Read in QEMU on every system, modes `0`, `1` and `2`:
  a file copy from a stick and the guest's idle processor load showed no
  step change between the line and polling; audio played without a gap under
  polling on 2000, XP x64 and 7 in both architectures, and with two gaps of
  about 20 ms on Vista x64. The move to polling when a line never arrives
  was read in QEMU on every system, and on real hardware on the Dell
  Latitude E7250 under XP x64 with `XhciIntelIrqRoute` `0`, where the line
  is dead: the driver moved to polling and a SuperSpeed stick copied clean.
  There Windows ran the poll every 15 to 62 ms, its timer's granularity on
  that machine, not every 1 ms. Polling by choice (`2`) was read on a
  ThinkPad E460 under Windows 98 SE: about 4 GB through a SuperSpeed stick,
  compared clean. `XHCISNAP`'s report says which delivery the controller
  uses, and why.

### Registry settings / XhciPollFineTimer: a finer system timer while polling

- (In the paragraph beginning "New in `2.4.0.0`, off by default: the install
  writes ...") A polled controller is looked at no more often than Windows'
  clock ticks: at best every 10 to 15 ms with `XhciPollActiveMs` `1` in QEMU
  on Windows 2000, XP and 32-bit 7 (`2.3.x`), and every 15 to 62 ms on the
  Dell Latitude E7250 under XP x64 ("XhciInterruptMode, XhciPollIdleMs and
  XhciPollActiveMs").
- (In the paragraph beginning "Why it ships off. It has been read only ...")
  **Why it ships off.** It has been read only in QEMU virtual machines
  without hardware acceleration, under polling (`XhciInterruptMode` `2`), on
  Windows 7 x64, 7 x86 and Vista x86: while the finer clock was held the
  guest's clock ran at a tenth to seven tenths of real time, and keys typed
  on a USB keyboard were lost (on 32-bit XP and 2000 it kept nine tenths of
  real time or more); with the value `0`, or under an interrupt, the clock
  kept real time and no key was lost. Whether real hardware does the same is
  unread, so it is off until a machine has been read ("Untested ground").
  The `2.3.x` readings of polling above are of the clock tick alone.

### Registry settings / XhciImodInterval250ns: the interrupt moderation interval

- (In the paragraph beginning "`160` is the value Linux's xHCI driver uses.
  On ...") `40` added only 1 to 3% more for up to four times the interrupt
  rate. A Full-Speed audio stream ran at `160` in a Windows 2000 virtual
  machine with no missed-service or packet error, which shows the value
  reaches the controller and the stream runs, not what it costs audio on
  real hardware.

### Registry settings / XhciFastPollFsLs: Low- and Full-Speed polling above 1000 Hz

- (In the paragraph beginning "A controller that refuses a code is caught as
  ...") A Low- or Full-Speed device behind a hub at any code is **untested
  ground**: `2.3.0.0` to `3.0.0.0` left such a device alone, and below 1 ms
  or above 128 ms the specification does not describe it.
- (In the paragraph beginning "Untested ground on real controllers: it has
  been read ...") **Untested ground on real controllers**: it has been read
  on none. In QEMU (since `2.3.0.0`, on 32-bit Windows 7 and Windows 98 SE)
  value `4` programmed the intervals the codes ask for, `2` and `3` ignored
  the codes, a mouse behind a hub kept its rate and the mouse moved
  throughout; that shows what the driver asks of the controller, not how a
  real controller and device behave at those rates. The `3.1.0.0` codes were
  read the same way on Windows 98 SE: 128, 135 and 143 programmed polling
  intervals of 125 us, 16 ms and 4.096 s (a Full-Speed mouse behind QEMU's
  hub among them), 143 left a High-Speed device at its own 4.096 s, and 144
  changed nothing. The bench reading on the ThinkPad E460 and P14s Gen 1 was
  not taken for this release.

### Registry settings / XhciFirstEnumWaitMs and XhciFirstEnumPortMs: the first report's wait

- Read with the defaults in QEMU virtual machines on development host A, on
  Windows 98 SE and 2000: the root hub's first report went 20 to 30 ms after
  its start with nothing plugged in, and 0.3 to 0.9 s after it with a mouse
  and a stick plugged in at root ports or behind a hub; a hub's own first
  report waited 0 to 31 ms. Windows 98 SE's reading at boot with a USB mouse
  plugged in was taken with the virtual machine's SMM turned off (QEMU's
  `-machine pc,smm=off`), because the virtual machine's firmware stopped in
  SMM at boot with a USB mouse attached; that is the virtual machine's, not
  the driver's. Neither value was read set to anything but its default.

### Registry settings / XhciIntelPortSwitch: the Intel port switchover

- (In the paragraph beginning "Only a `DWORD` that is not `0` turns it ...")
  with the USB 2.0 controller. Read on a Lenovo B490 under Windows 98 SE: at
  `0` and after a restart, a device on a blue connector stayed with the USB
  2.0 controller.

### Registry settings / XhciIntelIrqRoute: the Intel interrupt routing repair

- What it rests on. On the owner's E7250 under Windows XP x64, the same
  register written by hand to the value this setting writes fixed the mouse
  at once and let a USB stick work. The driver's own write was then read
  there at the `2.3.0.0` bench: under XP x64 at the default, and under
  Windows 7 x64 with the line asked for (`XhciInterruptMode` `1`, also with
  the EHCI disabled), it moved INTA from PIRQA to PIRQF (`D20IR` `3210` to
  `3215`, IRQ 21) and read it back; under Windows 7's default MSI it wrote
  nothing, as designed. No other machine has been read.

### Registry settings / XhciMaxTransferKB: the UAS command size

- (In the paragraph beginning "Windows 98 SE and ME. Both values are read,
  ...") sends at most 64 KiB at a time whatever the UAS driver offers. Read
  in QEMU virtual machines on development host A, Windows 98 SE with NUSB
  and Windows ME, with `XhciMaxTransferKB` at `1024`: no command above 64
  KiB, one at a time, and file reads no faster than at `64`.

### Registry settings / XhciWideMap: two transfer pieces at once

- (In the paragraph beginning "The decision rests on a reading of every
  Windows ...") x86 and x64. Later HAL hotfixes were not read; the checks
  made at each start and each transfer stand for them.

### Registry settings / XhciSelectiveSuspend: putting idle devices to sleep

- Read in QEMU virtual machines with the project's own test driver bound to
  a device, on Windows 98 SE, ME, 2000, XP and 32-bit Windows 7 (there is no
  64-bit test driver; the run record has the legs), and with the value at
  `0` reading as the build before it on every system. Windows' own drivers'
  idle was not read: Windows' HID driver never idles QEMU's mouse. No real
  machine has been read with it on, and no real remote-wake device. So leave
  it off unless you are testing it.

### Registry settings / XhciUsb2HubSpeedFallback: Low- and Full-Speed IDs behind a hub

- (In the paragraph beginning "Read at each start of the controller: restart
  Windows ...") restart Windows after changing it. Built without the
  hardware; on a VL805 reported working on `2.3.0.0` by the owner, not yet
  read in detail; in QEMU virtual machines, whose controller lists no speed
  table, devices behind a hub read as before. `XHCISNAP`'s report says
  whether it is on,

### Registry settings / XhciResumeKeep: devices kept across such a resume (new in `3.4.0.0`)

- (In the paragraph beginning "Where a resume from standby or hibernation
  starts the ...") The install writes it as `1`. The record is
  `docs/contributing/runs/run-45.md`, "45.23".

### The controller's Advanced tab and the root hub's Power tab

- (In the paragraph beginning "The controller's properties carry an Advanced
  tab and the ...") since external hubs are the driver's own. Read on
  Windows 98 SE under both stacks and on Windows 2000, in virtual machines.
  Since `2.1.0.0` each external hub's entry has a Power tab of its own, for
  the devices behind it, read on Windows 98 SE, 2000 and 32-bit Windows 7 in
  virtual machines.
- (In the paragraph beginning "Up to `2.0.0.0` the Power tab showed every
  device's ...") `2.1.0.0` answers it. Read in QEMU virtual machines on
  development host A, on the `2.1.0.0` integration build: the root hub's and
  the hub's Power tabs list each device's power in mA on Windows 98 SE and
  2000 (a mouse, a keyboard and an audio device 100 mA, a stick 0 mA), on
  32-bit XP, and on XP x64 (a mouse and a keyboard 100 mA, a stick and the
  hub 0 mA). The 0 mA is what QEMU's stick and hub ask for in their own
  descriptors. Two things
- (In the paragraph beginning "Bandwidth counts isochronous pipes in use,
  and nothing ...") and an audio device adds its share only while it plays
  or records. That is read from Windows' own page, not on a running system:
  the figure while audio plays was not read, since Windows 98 SE's
  `USBAUDIO.VXD` fails on playing in a virtual machine ("Known
  limitations").
- (In the paragraph beginning "A SuperSpeed device's power: 510 mA means 510
  ...") after the device's `dev.ss.vidpid`. Read on a ThinkPad P14s Gen 1
  under Windows 98 SE: a SuperSpeed drive declaring 896 mA, which read 224
  mA under `2.1.0.0` on Windows 98 SE, reads 510 mA, and the report shows
  `dev.ss.maxpower.ma=00000380` (896 mA); a USB 2.0 device reads as it did.
  A SuperSpeed device declaring less than 510 mA has not been read on
  hardware.

### The log, and how to send one / Both logs in the xHCI98 USB Tree (from `3.0.0.0`)

- (In the paragraph beginning "Do not run DebugView on Windows 98 on real
  ...") which is what that crash was traced to, but no build of it has been
  run under DebugView on Windows 98 hardware.

### Known limitations

- Each was measured, in a virtual machine unless it names a physical
  machine, or says it was found by reading the code.
- (In the paragraph beginning "Windows 2000 updated from `3.2.0.0`: an
  internal NVMe ...") XP may do the same; it was not read there.
- (In the paragraph beginning "Windows ME: plugging and unplugging a USB
  mouse ...") it was seen in a virtual machine after 4 to 17 back-to-back
  plug and unplug cycles under this driver, and ME's own USB stack, on a
  UHCI controller in the same virtual machine, stopped the same way after 8
  and 58 cycles of a series that pulled each mouse one second after its
  plug. This driver starts a new device no later than ME's own stack does -
  sooner, in those readings.
- (In the paragraph beginning "Windows 98 SE: a USB audio device can ...")
  the rates behind "about the same rate", "often" and "often froze": with
  this driver not loaded, Windows 98 SE's own USB stack on a UHCI
  controller wedged in 14 of 30 tries, against 23 of 39 under this driver,
  the device attached 40, 120 and 300 seconds after the boot, with
  selective suspend on or off; USBView walked and closed with a USB audio
  device attached froze the machine, or stopped it in `USBAUDIO`, about
  half the time; and a device present when Windows started, unplugged and
  plugged back, froze it 5 times in 7.
- (In the paragraph beginning "Windows 98 SE: a USB audio device can ...")
  Waiting after the boot did not avoid it in these readings, though an
  earlier reading had suggested two minutes would.
- (In the paragraph beginning "Windows 98 SE: a USB audio device can ...")
  under Windows' own UHCI driver, NUSB and `xhci98.sys` alike (QEMU
  readings, 2026-10-23);
- (In the paragraph beginning "Windows 98 SE: a USB audio device can ...")
  leave it plugged in until shutdown** (a device present when Windows
  started came up with it in every boot read);
- (In the paragraph beginning "Windows 98 SE: a USB audio device can ...")
  "98 SE may freeze when the first USB audio device is plugged in after a
  boot"). Real-PC readings are owed.
- (In the paragraph beginning "Windows 98 SE and ME: a USB Audio ...") with
  a Sound Blaster X4 it stopped Windows with a fatal exception 0E in
  `USBAUDIO` (read on 98 SE).
- (In the paragraph beginning "Windows 98 SE and ME: a USB Audio ...") which
  ranks above Windows' `USB\CLASS_01` (read on 98 SE under xHCI98 with a
  simulated USB Audio Class 2.0 device at an id no INF lists, `uac2`'s INF
  in Windows' INF folder; usb-wdm-free's release 0.1.3 record).
- (In the paragraph beginning "Windows 98 SE in a virtual machine cannot
  ...") - **Windows 98 SE in a virtual machine cannot play USB audio**
  through Windows 98's own `USBAUDIO.VXD`; that is QEMU's emulated device
  and the VxD, through this driver and through a UHCI controller alike. On
  real hardware it plays: audio was played and heard at a root port and
  behind a hub.
- (In the paragraph beginning "Windows 98 SE: Stand by is untested on ...")
  - **Windows 98 SE: Stand by is untested on real hardware; remove USB disks
  before Stand by.** It has been read only in QEMU virtual machines, where
  Windows 98 SE itself does not resume reliably: after the wake, a program
  started, or Ctrl+Alt+Del, can stop with a fatal exception 0E in `KERNEL32`
  even with no USB controller in the machine. With a USB disk
- (In the paragraph beginning "Windows 98 SE: Stand by is untested on ...")
  A disk plugged in after Windows started did not stop the resume, and it
  was still attached afterwards. Windows 98 SE's own UHCI driver did not
  resume at all there. With only a USB mouse attached, at a root port or
  behind a hub, the resume completed and the mouse worked through this
  driver.
- (In the paragraph beginning "Windows ME: Shut Down can hang after the
  ...") the processor busy. Read in QEMU virtual machines with a mouse, a
  keyboard, two sticks (both read just before), a hub and two audio devices
  attached, on `2.4.0.0` and on the published `2.3.1.0` alike; without the
  disable and enable the same machine shut down in seconds.
- (In the paragraph beginning "Intel 7-, 8- and 9-series chipsets, with the
  ...") when the controller starts or stops**: it is disconnected on one
  side and found again on the other. Read on a Lenovo B490 under Windows 98
  SE with NUSB, with a mouse plugged in at boot, and with a device across a
  disable and enable of the controller.
- (In the paragraph beginning "After the driver closes off a controller it
  ...") and only restarting the machine ends it. Seen in virtual machines on
  Windows 98 SE and 2000 with the fault made on purpose.
- (In the paragraph beginning "Windows 2000: after the driver recovers the
  controller, ...") or the controller is disabled and enabled in Device
  Manager. Seen in a virtual machine (five times of five with the volume
  mounted); Windows 98 SE did not show it.
- (In the paragraph beginning "Windows 2000: after the driver recovers the
  controller, ...") recovers the controller on more faults. Seen again in
  `2.3.0.0`'s virtual-machine tests on Windows 2000: after a recovery with
  the controller polled (`XhciInterruptMode` `2`), and after a resume from
  hibernation. A controller that lost its state across the hibernation, as
  QEMU's does, is started afresh,
- (In the paragraph beginning "Windows 2000: after the driver recovers the
  controller, ...") Each time, a restart of the drive's own device cleared
  it (read with a command-line device restart, not in Device Manager).
- (In the paragraph beginning "Windows 2000 from the F6 floppy needs three
  ...") `usbd.sys` among them. Read with a keyboard and mouse on `2.3.1.0`
  and `2.4.0.0` alike, and with a USB stick and no keyboard or mouse on both
  as well.
- (In the paragraph beginning "After an update, the root hub's Driver tab
  ...") although it runs the new file (read again after an update from
  `2.2.0.0` on Windows 2000, XP and XP x64).
- (In the paragraph beginning "After an update, the root hub's Driver tab
  ...") which they do not ask for (read again after an update from
  `2.2.0.0`).
- (In the paragraph beginning "32-bit Windows Vista and 7: the install asks
  ...") the controller at Code 1 until a restart (read with `xhcimsi.sys`).
- (In the paragraph beginning "Intel 7-series (Panther Point, `8086:1E31`):
  a periodic schedule ...") refused.** Found by reading Linux's code, not
  measured. Linux keeps its

## The acceptance test's readings

`docs/using/release-acceptance-test.md` said, step by step, where each
expectation was observed. Those notes, as they stood for `3.4.1.0`:

### The steps / Step 1. Record the machine, its BIOS and the OS build

- Observed: the record format is `xhciqual/hardware-testing.md`'s, with
  `xhciqual/results/e460-2026-08-22/README.md` as a worked example.
  `XUSB2PR`'s behaviour is derived from the Intel datasheet and from Linux,
  and has been read on one machine: a Lenovo B490 (1E31) under Windows 98
  SE, whose firmware on Auto and on Smart Auto left the switchable ports on
  EHCI and whose ports the driver then routed (roadmap-hcd task 34.3,
  `docs/contributing/runs/run-34.md` 34.3-H2); see
  `docs/usb-xhci-info/xhci-programming.md`, "Firmware Handoff, and the
  Controller Deviations This Driver Acts On".

### The steps / Step 2. Check the download

- (In the row or paragraph beginning "Observed: the layout and the assertion
  that protects it ...") `docs/contributing/legal-provenance.md` section 5
  records why the asset carries no Microsoft file. `2.0.0.0`'s four-file
  directory (two drivers, two INFs) was staged for the `release` legs of its
  integration build and read again from its asset; each later release's
  directory list is its own `readme.txt` section 8 and
  `releases/<version>/`.

### The steps / Step 3. The DOS pass

- (In the row or paragraph beginning "`Interrupt Pin = 0` is no longer a
  stop: ...") record it and continue to step 4 (the `2.0.0.0` driver accepts
  such a controller, built from the specification and untested).

### The steps / Step 4. Install, from the release directory

- (In the row or paragraph beginning "4.2 | Windows 2000 SP4 | Device
  Manager, ...") since the INF names only the USB class (read on a clean
  2000 SP4 guest, 2026-10-13, `docs/contributing/runs/run-37.md`,
  "37.final") |
- (In the row or paragraph beginning "4.5 | Windows ME | No USB 2.0 ...") |
  No USB 2.0 stack is needed (a stock ME was read on `2.3.0.0`; SweetLow's
  stack, if present, also works); the Windows 98 SE route of 4.1 |
  Completes, with a request to restart. In the virtual machines no CD was
  asked for. This target is supported in virtual machines only |
- (In the row or paragraph beginning "4.6 | Windows XP | Device Manager, the
  ...") -> Have Disk -> `RELEASE-X86\`; Continue Anyway at the
  unsigned-driver warning | Completes with no file prompt and, from
  `2.4.0.0`, no request to restart, with `xhcihlpr.sys` and its service
  `xhcihlpr` installed as on 4.2. This target is supported in virtual
  machines only |
- (In the row or paragraph beginning "4.8 | Windows XP x64 | The Windows
  ...") | The Windows 2000 route of 4.2, pointed at `RELEASE-X64\`; Continue
  Anyway at the unsigned-driver warning | Completes with Windows' signature
  and Logo prompts only, no file prompt and no restart. `RELEASE-X86\`
  offers this machine no driver at all, which is what a wrong pick looks
  like. This target is supported in virtual machines; of real machines it
  has run on one, the owner's Dell Latitude E7250, at the `2.3.0.0` bench |
- (In the row or paragraph beginning "4.9 | Windows Vista, Windows 7
  (32-bit) | ...") with no `PendingFileRenameOperations` entry (read in the
  Phase 38 VM matrix); record both.
- (In the row or paragraph beginning "4.9 | Windows Vista, Windows 7
  (32-bit) | ...") so the driver starts on the ordinary boot. This target is
  supported in virtual machines only |
- (In the row or paragraph beginning "4.10 | Windows Vista x64, Windows 7
  x64 ...") Record which of the two modes the run used. This target is
  supported in virtual machines; Windows 7 x64 has also run on one real
  machine, the owner's Dell Latitude E7250, at the `2.3.0.0` bench |
- Observed: the device strings are `src/xhci98.inf`'s `XhciDesc` and
  `RootHubDesc`. 4.1's prompts, word for word, and the restart were read on
  Windows 98 SE in virtual machines under NUSB 3.3, under SweetLow's stack
  and on a stock install (roadmap-hcd 26-V.0 and 26-V.1, and the stock and
  SweetLow-only readings of Phase 28). 4.2 is 26-V.0's and 26-V.2's. 4.8 is
  28-A.2's, on Windows XP x64 in a virtual machine. 4.6, 4.9 and 4.10 are
  the Phase 28 guest legs on the integration build, read again on its
  `release` flavour, and read once more from the release asset after the
  cut. 4.7's two tabs were read on Windows 98 SE under both stacks and on
  Windows 2000 (26-A.8).

### The steps / Step 5. Devices, one at a time, then a hub

- Observed: under `2.0.0.0`, in virtual machines: HID, storage with a
  verified file compare, hubs with devices behind them and unplug and replug
  on both primary targets and the Phase 28 guests (roadmap-hcd 26-V.1,
  26-V.2, 27-V.1 and 28-V.1's legs on the integration build); UAS at
  SuperSpeed and at High Speed with a round trip and no restart (31-V.1, and
  the Windows 98 SE storage readings of Phase 31); SuperSpeed storage
  (29-V.1). The Windows 98 SE storage prerequisite and the "Disk drive" step
  are the SweetLow-only reading of Phase 28. On real hardware, every clause
  of this step was observed on Windows 98 SE on a ThinkPad E460 and a
  ThinkPad P14s Gen 1, at the bench session before the release (roadmap-hcd
  28-E.1, 29-E.1, 30-E.1 and 31-E.1): High-Speed hubs, single- and multi-TT,
  with Low- and Full-Speed devices behind them and a Full-Speed hub behind a
  High-Speed one, a Low-Speed mouse polled every 8 ms at a root port and
  behind a hub, audio played and heard at a root port and behind a hub,
  SuperSpeed storage, a SuperSpeed hub, and UAS at SuperSpeed and High Speed
  with forced Bulk-Only. The Low-Speed clause (5.1) has no virtual-machine
  vehicle and rests on that bench reading.

### The steps / Step 6. Reboot with devices attached

- Observed: record only for `2.0.0.0`; the guest legs restarted with devices
  attached but did not read this step as a clause. Under `1.x` this was
  batch 11-V's, on both primary targets.

### The steps / Step 7. The target-specific clauses

- 7.1 was read under `2.0.0.0` in Windows 98 SE virtual machines under NUSB
  3.3 and under SweetLow's stack: disable, enable, remove and rescan, with a
  mouse attached (roadmap-hcd Phase 26, "the Windows 98 door sequence"). It
  has not been read on real hardware.
- (In the row or paragraph beginning "7.3 is the driver's own composite
  split: Windows' own ...") (the release notes, "Devices and hubs in Device
  Manager"). Read in virtual machines on both primary targets with the
  C-Media `0D8C:0014`, which has no IAD (roadmap-hcd 26-A.7).
- 7.6 was read under `2.0.0.0` in the Windows 2000 virtual machine, under
  Driver Verifier, with the remove and rescan beside it (roadmap-hcd
  26-V.2). It is also the mechanism step 8 needs.
- (In the row or paragraph beginning "7.7 | Record the USB stack installed,
  if ...") | None (stock ME) or SweetLow's USB 2.0 stack; both have been
  read |
- (In the row or paragraph beginning "7.8 | Plug in one composite device, as
  ...") | As 7.3; the functions were read on the integration build with
  QEMU's `usb-audio` at a root port and behind a hub |
- 7.12 and 7.16 were read under `2.0.0.0` on the XP guests in virtual
  machines (roadmap-hcd 28-A.2 on XP x64, and the Phase 28 guest legs). 7.10
  and 7.14's Low-Speed device has no virtual-machine vehicle; a Full-Speed
  one is what the virtual machines read.
- 7.19 was read under `2.0.0.0` in virtual machines: five disable and enable
  cycles each on Windows 7 x86 and x64 with no hang (roadmap-hcd 28.3). The
  controller disable that hung 32-bit Windows 7 on a ThinkPad E460 under
  `1.x` belonged to the miniport under Microsoft's `usbport.sys` and did not
  occur under `2.0.0.0`. Windows 7 has not been run on real hardware with
  `2.0.0.0`.

### The steps / Step 8. Produce the log channel, and read the SuperSpeed witness

- (In the row or paragraph beginning "On Windows 98 real hardware, do not
  run DebugView ...") that route crashed the `1.x` driver's machines and has
  not been read on `2.0.0.0` there.
- Observed: under `2.0.0.0`, `XHCISNAP -probe` answered "the channel is
  live" with the channel on and a full `-o` report was read on Windows 98 SE
  under NUSB 3.3 and under SweetLow's stack and on Windows 2000 (roadmap-hcd
  26-A.8, in virtual machines). In 29-V.1 the decoded port speed and the
  slot speed were read from the driver's trace on both primary targets, in
  virtual machines; the report's slot table, added for this witness, read a
  SuperSpeed drive as SuperSpeed on real hardware under Windows 98 SE at the
  bench (29-E.1).
- (In the row or paragraph beginning "It is the same procedure on every
  target. From ...") the one 32-bit `XHCISNAP.EXE` under WOW64 on the 64-bit
  targets; `XHCISNAP -probe` was read that way on Windows XP x64
  (roadmap-hcd 28-A.2).

### The steps / Step 9. Shut down with devices attached

- Observed: a clean shutdown with devices attached closed every guest leg of
  roadmap-hcd Phases 26 to 28 in virtual machines; with traffic moving at
  the shutdown, record only.

### The definition of "record only"

- The opening: A clause marked "record only" is an expectation this project
  has not observed on the `2.0.0.0` driver: write down what is seen, and do
  not fail the step on it.
- "The steps": Each gives what to do, the expected reading, what to do when
  that reading does not appear, and where the expectation was observed. An
  expectation that has never been observed anywhere is marked "record only"
  and is not a pass criterion.
- Equipment, "Cannot start without", row 2: "for the VM-only rows", and the
  target blocks of step 7 headed "Windows ME (virtual machines only)",
  "Windows XP (virtual machines only)", "Windows XP x64 (virtual machines
  only)" and "Windows Vista and Windows 7, 32-bit and x64 (virtual machines
  only)".

## The other user documents

Passages taken out of `README.md` and `docs/using/tuning.md` on 2026-11-06,
as they stood for `3.4.1.0`:

- `README.md`, the opening: "It is built to Intel's xHCI specification and
  tested mainly on Intel machines."
- `README.md`, "NVMe drives", the row for 7: "its `stornvme` comes only with
  update KB2990941, untested behind a tunnel".
- `README.md`, "Known limitations/issues", Thunderbolt and USB4: "Drives
  attached at power-on are not affected; other non-ACPI computer types are
  untested".
- `README.md`, "Toolchain and building", step 6: "Test in QEMU with
  `qemu-xhci` (Win98 SE, Win2000 SP4 and an SMP Win2000 guest; `usb-storage`
  and `usb-uas` for SuperSpeed and UAS)."
- `docs/using/tuning.md`, `XhciFastPollFsLs`: "A Low- or Full-Speed device
  behind a hub at any code is untested ground, and so is every code below
  `131` or above `138` at Low or Full Speed."
- `docs/using/tuning.md`, `XhciInterruptMode` `2` and an IRQ held raised on
  Windows 98 SE: "(read in QEMU on 98 SE, roadmap-usb4 task 45.33; ME is
  expected to behave the same, not tested)".
