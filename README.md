# xhci98

This project xHCI98 is a WDM generic USB host controller driver for xHCI host controllers. It supports Windows 98 SE, ME, 2000, XP (x86/x64), Vista (x86/x64) and 7 (x86/x64).

Although xHCI Controllers offer USB 3.0, this driver runs USB 2.0 on the controller only.

This driver is developed based on Intel's xHCI specification and tested mainly on Intel machines so far. No guarantees have been made on xHCI implementations from other vendors. [Omores](https://www.youtube.com/@O_mores) has also [tested it](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) on some AMD AM4 and AM5 platforms.

This project is from a solo human with AI-assistance only so bugs are not unexpected. Feel free to report them if you encounter any issues.

<img src="images/xhci98-usb-devices.jpg" width="800">

This is my 2020 ThinkPad P14s Gen 1 (Comet Lake xHCI, no EHCI) on Windows 98 SE. Connected devices are 7-port hub, USB Ethernet, USB Audio, a USB-to-SATA bridge, two flash drives and a mouse all using the xHCI controller.

Video by [Omores](https://www.youtube.com/@O_mores) featuring this driver:

[![omores video](https://img.youtube.com/vi/7SOyvvC7P4E/hqdefault.jpg)](https://www.youtube.com/watch?v=7SOyvvC7P4E)

My personal demo video: https://www.youtube.com/watch?v=AU77f9CSbYc

Blog post of this project: https://yeokhengmeng.com/2026/08/xhci98-usb-host-driver/

## Motivation

Modern PCs from the mid-2010s onwards use xHCI-only USB chipsets. Windows 98 shipped with UHCI/OHCI drivers (USB 1.1) and gained EHCI (USB 2.0) through the back-ported stack in NUSB. Windows 2000 gained EHCI natively in SP4.

Neither has native xHCI support at all so even basic devices like keyboards, mice and flash drives do not work out of the box without BIOS support. Even with BIOS support, those devices cannot be hotplugged and other devices like Ethernet and Audio are not supported. 

This project attempts to fill that gap.

## Why USB 2.0 only, on a USB 3.0 controller

The existing `usbport.sys` this driver depends on does not support USB 3.0 and neither does anything above it. This project driver `xhci98.sys` is only the miniport underneath that stack.

SuperSpeed would mean rewriting the entire USB HCD (Host Controller Driver) for both operating systems. This is significantly more work than this driver for a speed that most machines running Windows 98 or Windows 2000 is unlikely to effectively use. The [xHCI programming guide](docs/usb-xhci-info/xhci-programming.md#what-superspeed-support-would-require) summarises what it would take.

Every USB 3.x connector (USB4/Thunderbolt included) also carries the USB 2.0 wires and xHCI exposes them as a separate logical port per connector. This driver manages those USB 2.0 ports and leaves the USB 3.x ones unpowered so a SuperSpeed-capable device falls back on the USB 2.0 port and runs at High-Speed.

## Installation Steps

The driver needs a USB 2.0 stack on the machine first:

- **Windows 98 SE**: Either install [NUSB 3.3 or 3.6](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html) or the [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip). For SweetLow's stack, unzip, right-click the `USB2.INF` at its root then install. Reboot if requested after installing the USB 2.0 stack.
- **Windows ME**: Use [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip) only.
- **Windows 2000 SP4, XP SP3 x86**: Nothing to install, both OSes at their service pack level already have the stack.
- **Windows XP SP2 x64, Vista SP2 and 7 SP1 (x86/x64)**: Nothing to install either. On **Vista x64 and 7 x64**, driver signature enforcement has to be disabled as this driver is not signed.

On an xHCI-only Windows 98 SE or ME machine, **have the Windows installation CD at hand** or its contents on disk. Windows may ask for files from it while installing the driver or the USB devices plugged in afterwards.

Optional but recommended: boot real DOS (not a DOS box inside Windows) and run `XHCIQUAL` from the `XHCIQUAL\` folder. A controller reporting no legacy interrupt pin cannot be driven on either system and there is no software workaround, so find out before you install anything.

<img src="images/xhci98-xhciqual-basic.jpg" width="800">

Basic read-only quick scan answers in one line. The `xhci` run below it exercises the controller (handoff, reset, DMA, interrupt, port resets) and ends with a verdict:

<img src="images/xhci98-xhciqual-full-start.jpg" width="800">

Running the full check.

<img src="images/xhci98-xhciqual-full-end.jpg" width="800">

Controller qualified verdict.

XHCIQUAL demo video: https://www.youtube.com/watch?v=Tv6blmBS6Do

To submit logs with a [bug or hardware report](https://github.com/yeokm1/xhci98/issues/new/choose), run these from real DOS and attach `FULL.LOG` if the second run finished, or `PROBE.LOG` only if it did not:

1. `XHCIQUAL --probe-only --no-page --log PROBE.LOG` is read-only. It takes ownership of nothing and writes no PCI configuration register.
2. If that does not crash the machine, continue with `XHCIQUAL --no-page --log FULL.LOG`. This one **takes over the controller**, resets it and resets its ports. Use a PS/2 keyboard and do not write the log to a drive on the controller being tested.

### Install

It has four driver directories: `release-x86\` and `debug-x86\` for 32-bit Windows (98 SE, ME, 2000, XP, Vista, 7), and `release-x64\` and `debug-x64\` for 64-bit Windows (XP x64, Vista x64, 7 x64).

1. Put the unzipped package somewhere the machine can read: a floppy, a CD, a shared folder. `release-x86\` or `release-x64\` is the one to install. The `debug-` directories hold the same driver built for troubleshooting, only install if asked.
2. On Windows 98 SE, install a USB 2.0 stack first (NUSB 3.3, or SweetLow's). On Vista x64 and 7 x64, driver signature enforcement must be disabled, as the driver is unsigned.
3. In Device Manager, find the unrecognised xHCI controller. It sits unclaimed with a yellow mark, usually under "Other devices" such as "Universal Serial Bus Controller".
4. Properties -> Driver -> Update Driver -> Specify a location/Have Disk -> the `release-x86\` or `release-x64\` directory.
5. It installs as "USB 2.0 eXtensible Host Controller (xhci98)" with a "USB Root Hub" underneath it, and neither should carry a warning mark.
6. Reboot if requested.

<img src="images/xhci98-driver-info.jpg" width="800">

**On Windows 98 with NUSB, do not disable, remove or upgrade this driver in Device Manager**. Each of those blue-screens that system. The fault is in NUSB's `usbport.sys`, the Windows 2000 build of the USB 2.0 stack, not this driver. Microsoft's own USB drivers do the same thing on the same machine. The readme has the way round it for NUSB systems.

The same driver on the same machine survives all three under SweetLow's build of that stack.

### Tuning: the interrupt moderation interval (from 1.1.1.0)

`XhciImodInterval250ns` is a `DWORD` in the controller's driver (software) key, in **units of 250 ns**. It sets how long the controller waits after one interrupt before raising the next. A shorter interval makes USB mass storage faster, at the cost of more interrupts.

| | Value | Interval | Interrupts per second, at most |
|---|---|---|---|
| Written by the install | `500` | 0.125 ms | 8,000 |
| Used when the value is missing, unreadable, or outside `10`-`4000` | `4000` | 1 ms | 1,000 |
| Lowest accepted | `10` | 2.5 us | 400,000 |

A value outside `10`-`4000` is replaced by `4000`, not rounded to the nearest limit, so a mistyped `0` cannot turn moderation off. `4000` is the controller's own power-on value and what every earlier release ran at.

On the P14s under Windows 98 SE, large reads from a USB 3 stick went from 17.6 MB/s at `4000` to 32.5 MB/s at `500`, and USB audio played through the same test with no error counted by the driver and one ring underrun, against one and three on two passes at `4000`. Linux's xHCI driver defaults to `160` (40 us), which read 33.1 MB/s here. This package ships `500` to be more conservative, because interrupt load at real rates is what has crashed Windows 98 on real hardware before.

Feel free to tune it: lower towards `160` for the last few percent of storage speed, or raise it towards `4000` (or delete it) if you get audio stutter or instability under load. The driver reads it at start, so restart after a change. `XHCISNAP` shows the value read, the interval in force and what the controller took, under "registry values". The readme's "Registry settings" section says how to find the right key.

On Windows 98 with NUSB, an upgrade over an existing install crashes before the value is written, so the driver runs at `4000` until you set it by hand.

## What is tested, and what is not

Windows 98 SE is validated on real hardware. 32-bit Windows 7 has run on real hardware once (a ThinkPad E460, 2026-09-19). Windows 2000 SP4, Windows ME, Windows XP (x86/x64), Windows Vista (x86/x64) and 64-bit Windows 7 have only ever run in QEMU virtual machines.

| Machine | Controller | Result | Tested by |
|---|---|---|---|
| 2016 ThinkPad E460 | Intel Skylake, Sunrise Point-LP (100-series) PCH. xHCI 1.0. | OK | Me |
| 2020 ThinkPad P14s Gen 1 | Intel Comet Lake PCH-LP (400-series). xHCI 1.1. | OK | Me |
| Socket 1151 desktop (H110) | Intel 100-series PCH xHCI. | OK | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) |
| Socket 1151 v2 desktop (B360) | Intel Cannon Lake PCH (300-series) xHCI. | OK | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) |
| AM4 desktop (B550) | AMD 500-series chipset xHCI, plus the Ryzen CPU's own xHCI. | OK | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) |
| AM4 desktop (X570) | AMD-designed X570 chipset xHCI, plus the Ryzen CPU's own xHCI. | Not OK | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) |
| AM5 desktop (X670) | AMD 600-series chipset xHCI, plus the Ryzen CPU's own xHCIs. | OK | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) |

| | State |
|---|---|
| Windows 98 SE | Validated on real hardware and in VMs. HID, mass storage, USB Ethernet and USB Audio have all run on real xHCI silicon, at a root port and behind hubs. |
| Windows 2000 SP4 | Virtual machines only, including an SMP guest and Driver Verifier. It has never run on real hardware. |
| Windows ME | One virtual machine only, under SweetLow's USB 2.0 stack (the only stack it is supported with): the driver loads and starts, and a HID mouse, a USB mass-storage device and a composite audio device bind (2026-09-02). Never run on real hardware. |
| 32-bit Windows XP | One virtual machine only (XP Professional SP3): the package installs on an xHCI-only machine with no prompt, the driver loads and starts under XP's own USB stack, and a HID mouse, a USB mass-storage device and a composite audio device bind; disable, enable, remove and rescan in Device Manager all survive. Never run on real hardware. |
| Windows XP x64 | One virtual machine only (XP Professional x64 SP2), and **a separate 64-bit driver**, not the one above: the same clauses all pass, taken on the `qemu` build and then read again on the `release` flavour, from a package with its own INF. The `debug` build of it has never been run. Never run on real hardware. |
| Windows Vista SP2 and Windows 7 SP1, 32-bit and x64 | One virtual machine each (four in all). The 32-bit ones run the same driver as 98 to XP, the x64 ones the 64-bit driver. The package installs, the driver loads and starts, a HID mouse, a USB mass-storage device and a composite audio device bind, and five disable/enable cycles, remove and rescan in Device Manager all survive. Taken on the `qemu` build; the published `release` package was then installed on all four (2026-09-18). On Vista x64 and 7 x64, driver signature enforcement has to be disabled as this driver is not signed. 32-bit Windows 7 has run on real hardware once (a ThinkPad E460, 2026-09-19): the install, a mouse, a flash drive and USB audio at a root port and behind USB 2.0 hubs, reboot and shutdown all passed, but **the first disable of the controller hung** (Known limitations/issues). Vista and 64-bit Windows 7 have never run on real hardware. |
| Intel 7/8-series (`XUSB2PR` mux) | Never run. Everything said about the `XUSB2PR` port mux comes from Intel's datasheet and Linux, not silicon. The driver does not touch it. |
| Resume from standby (Windows 2000) | Never executed anywhere. No available VM offers a resumable power transition, and there is no Windows 2000 machine. |
| Low Speed, USB Audio, hub topologies | Work on Windows 98 hardware in the configurations tried. Not covered: an audio device with `bInterval > 1`, a USB 1.1 hub under a multi-TT hub, and the Windows 2000 side on silicon. |

The devices checked so far, all on the E460 under Windows 98 SE. Each is characterised in [test-equipment.md](docs/contributing/test-equipment.md).

| Device | VID:PID | Speed | Result |
|---|---|---|---|
| Terminus 7-port hub, multi-TT | `1A40:0201` | High | The hub every "behind a hub" result below was taken on. |
| Terminus 4-port hub, single-TT | `1A40:0101` | High | Characterised. The swap partner for the hub above. |
| Genesys 7-port hub (two cascaded chips), single-TT | `05E3:0608` | High | Characterised. A second-tier hub position. |
| Logitech USB Optical Mouse | `046D:C077` | Low | Works at a root port and behind the multi-TT hub. |
| Microsoft Wired Keyboard 600 (composite, two HID interfaces) | `045E:0750` | Low | Types, once Windows 98's own `usbhub.sys` is present; since 1.0.0.1 the install has Windows copy it from the Windows 98 CD or CABs. |
| SanDisk U3 Titanium flash drive | `0781:5408` | High | Works at a root port and behind the hub. |
| MSSU10-128GSR and SanDisk 3.2Gen1 USB 3.0 flash drives | `090C:2320`, `0781:55AB` | High (SuperSpeed falls back) | Enumerate at High-Speed on the USB 2.0 port. A file round trip passed. |
| StoreJet Transcend USB-to-SATA bridge (ASMedia) | `174C:5106` | High (SuperSpeed falls back) | A drive letter, and a file written and read back with matching contents. The first real bridge chip this driver has done verified I/O through. |
| ASIX AX88772A USB Ethernet | `0B95:7720` | High | DHCP lease and traffic on Windows 98 hardware. Also validated on Windows 2000 in a VM. Wedges the machine in three fast replug cycles (the defect below). |
| Sound Blaster Play! 2 (UAC 1.0 composite) | `041E:323D` | Full | Plays clean at a root port and behind the multi-TT hub. |
| Sound Blaster Play! 3, C-Media USB Audio Device (UAC 1.0) | `041E:324D`, `0D8C:0014` | Full | Enumerate and are named by the wizard. Found the Full-Speed `bMaxPacketSize0` bug. |
| Sound Blaster X4 (UAC 2.0, `bInterval` 3 and 4) | `041E:3278` | High | Enumerates but does not bind on Windows 98 (one HID devnode at Code 10, no composite parent), so its `bInterval > 1` endpoints were never exercised. |

<img src="images/xhci98-flash-speed-test.jpg" width="800">

ATTO Disk Benchmark on the P14s under Windows 98 SE against the MSSU10-128GSR flash drive, on the 1.1.1.0 build at new default interruption moderation interval of `500` (0.125 ms).

About 33 to 34.6 MB/s read and write from 64 KB transfers upward. The same drive gave about 18 MB/s at the 1 ms default of every release before 1.1.1.0. 

## Known limitations/issues

| Limitation | Detail |
|---|---|
| Disabling, uninstalling or upgrading an NUSB driver crashes the machine | A defect in NUSB's `usbport.sys` which cannot stop a running controller. Rename the existing `XHCI98.SYS`, reboot, then remove it. |
| Every device on a root port is reported as High Speed | Reporting the true speed of a slower device crashes usbport as there is no companion controller. A mouse or keyboard on a root port therefore polls at 1, 2 or 4 ms only. If this is an issue for you, put your lower-speed device behind a hub to allow the true speed to be reported - on Vista and 7, a USB 2.0 hub only (next row). |
| A USB 1.1 hub on a root port crashes Windows Vista and 7 | 32-bit and x64 alike: `STOP 0x7E` in `USBPORT.SYS` as soon as a mouse, keyboard or other slower device behind the hub is used. Plug such devices into a root port directly, or behind a USB 2.0 hub. Windows 98, 2000, XP and XP x64 are unaffected. See [issue 6](docs/issues/06-full-speed-root-port-bugcheck.md), section 6.2. |
| A Full-Speed USB audio device on a root port plays nothing on Windows XP and later | It installs and Windows shows it playing, but no sound reaches it. Behind a hub it plays (on Vista and 7 use a USB 2.0 hub). Windows 2000 plays on a root port. See [issue 6](docs/issues/06-full-speed-root-port-bugcheck.md), section 7. |
| Disabling the USB controller can hang Windows 7 | On the one real Windows 7 machine tried, the first Disable in Device Manager never finished and the next restart hung until powered off; enabling it again afterwards worked. Uninstalling or upgrading stops the controller too. Cause not known yet. Do it with no unsaved work open, and expect to power off if the restart hangs. |
| Fast, repeated plug and unplug can freeze Windows 98 | About twice a second sustained. Ordinary use is fine. |
| Mass-storage throughput seems slow before 1.1.1.0 | About 18 MB/s read and write on the P14s, below what USB 2.0 High Speed usually reaches. The cause was measured (roadmap task 23.3): the controller's interrupt moderation, left at its 1 ms reset default. From 1.1.1.0 the install sets it to `500` (0.125 ms), at which large reads from a USB 3 stick on the P14s went from 17.6 to 32.5 MB/s, and the ATTO run above read and wrote about 34 MB/s; see Tuning above. An upgrade over a running driver on Windows 98 with NUSB keeps the old 1 ms until the value is set by hand. |

## Toolchain and building

The driver is C (C89/C90, no C++ or CRT), built and verified on Windows 11 x64. The toolchain unpacks inside the repository (`tools/`, git-ignored) and installs nothing to `C:\`. Every script finds it relative to its own location, so a clone builds wherever it is unpacked.

1. Download the two toolchain archives into `tools\` under these exact names:

   | File | Source |
   |---|---|
   | `tools\MSVC600.zip` | MSVC 6.0 - [itsmattkc/MSVC600](https://github.com/itsmattkc/MSVC600) |
   | `tools\WIN2KDDK.EXE` | Windows 2000 DDK - [KunYi/WDK_DDKArchive](https://github.com/KunYi/WDK_DDKArchive/releases/tag/Win2K_DDK) |

   The 64-bit build needs a third, unpacked to `tools\WinDDK71`: WDK 7.1 (7600.16385.1), because no compiler older than its `cl` 15.00 can target x64. It is needed only for the amd64 package; a clone without it builds every 32-bit flavour.

2. Unpack them in place:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\setup-msvc6.ps1        # -> tools\MSVC600
   powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1 # -> tools\ntddk
   ```

3. Build the driver. `both` (the default) builds release and debug. `all` adds the emulator-only flavour.

   ```
   scripts\build-driver.cmd both
   ```

   | Flavour | Output |
   |---|---|
   | `release` | `src\objfre\i386\xhci98.sys` - the shipping driver |
   | `debug` | `src\objchk\i386\xhci98.sys` - the diagnostic build, also shipped |
   | `qemu` | `src\objchk_qemu\i386\xhci98.sys` - emulator-only, never published |

   Add `-amd64` for the 64-bit build, which is a **separate binary from a separate toolchain in a separate package** - no statement about "one binary" reaches it:

   ```
   scripts\build-driver.cmd both -amd64
   ```

   | Flavour | Output |
   |---|---|
   | `release` | `src\objfre\amd64\xhci98.sys` |
   | `debug` | `src\objchk\amd64\xhci98.sys` |
   | `qemu` | `src\objchk_qemu\amd64\xhci98.sys` - emulator-only, never published |

4. Build the two tools that ship beside the driver. `xhciqual\build.cmd` produces `XHCIQUAL.EXE`, the DOS qualifier, and needs [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2/releases) at `C:\WATCOM` (or wherever `WATCOM` points). That is the only tool installed normally on the host, and the driver never uses it. `xhcisnap\build.cmd` produces `XHCISNAP.EXE`, the snapshot reader, with the in-repo MSVC 6.0.

5. Make install media. A `.sys` on its own is not install media; the INF travels with it, and since 1.0.0.1 nothing else does, because the INF has Windows supply its own `usbd.sys` and `usbhub.sys` (since 1.0.1.0, `usbport.sys` on Windows 2000 and XP; since 1.0.2.0, `usbui.dll` on all four). For a Windows 98 SE target, first download NUSB 3.3 (`nusb33e.exe`) from [philscomputerlab.com](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html) to `tools\nusb33e.exe`.

   | Script | Output |
   |---|---|
   | `scripts\package\make-package.ps1` | `out\pkg-<flavour>-<arch>\` - media a VM or a machine can be pointed at |
   | `scripts\package\make-release.ps1` | `releases\<version>\` and `out\xhci98-<version>.zip` - the published cut, four directories since the `-Arch` default became both architectures |

6. Test in QEMU with the `qemu-xhci` device: a Win98 SE guest, a Win2000 SP4 guest, and an SMP Win2000 guest for race detection.

The version lives in [src/xhci_version.h](src/xhci_version.h). The INF's `DriverVer` is a literal that the build's INF gate checks against it. See [docs/contributing/build-and-test.md](docs/contributing/build-and-test.md) for the VM setup, versioning, packaging and the rest of the procedure.

## Repository Layout

```
src/            Driver source (C), the INF, and the DDK build files
test/           Host-side unit tests for the DDK-free core (test\run-host-tests.cmd)
scripts/        Setup helpers, build wrapper, the import/INF/packaging gates,
                and vm-matrix/ - the automated device matrix
xhciqual/       The DOS hardware-qualification tool (Open Watcom) and its
                bare-metal run logs under results/
xhcisnap/       The snapshot reader for the driver's log channel (both targets)
docs/           Documentation, indexed by docs/README.md
  using/        The user-facing release notes
  contributing/ Roadmap, architecture, build/test/runbooks, diagnostics,
                 invariants, provenance, and numbered design records
  usb-xhci-info/ xHCI programming/data structures, USBPORT ABI, WDM constraints,
                 and controller quirks
  references/   Fetch metadata for external specifications (downloads git-ignored)
releases/       The releases that have been cut, one directory per version
images/         Photographs of the driver on real hardware, used by this README
tools/          The build toolchain, unpacked in place (git-ignored)
external/       Local mirrors of the reference sources (git-ignored)
vm/             Guest images and per-run evidence (git-ignored)
out/            Staged install media from the packager (git-ignored)
.github/        Issue forms - the bug report and the hardware report
AGENTS.md       Guide for AI agents working on this project
```

## Documentation

Everything is under [docs/](docs/), indexed by [docs/README.md](docs/README.md).

Using the driver:

- [Release notes](docs/using/release-notes.md) - what the driver does, does not, and does not yet claim, per target
- [Release acceptance test](docs/using/release-acceptance-test.md) - the fixed procedure for checking a release on a new machine

Working on the driver:

- [Build and test](docs/contributing/build-and-test.md) - toolchain setup, builds, VMs, install, debugging, packaging, recovery
- [Roadmap](docs/contributing/roadmap.md) - project status: what each phase was for, its status, and the two acts left to the owner (the upload, the hand-run acceptance); Phases 0-16 are the initial release, and [Phase 17 onward](docs/contributing/roadmap-phases-17-on.md) is its second half
- [Architecture](docs/contributing/architecture.md) and [implementation invariants](docs/contributing/implementation-invariants.md)
- [Source files](docs/contributing/source-files.md) - what every file in `src/` is for
- [Failure diagnosis](docs/contributing/failure-diagnosis.md) and [measured lessons](docs/contributing/lessons.md) - read these before theorising about a failure
- [Design records](docs/contributing/design/README.md) - the numbered design decisions
- [Test equipment, as measured](docs/contributing/test-equipment.md) - every hub and device used for hardware validation
- [Legal and provenance record](docs/contributing/legal-provenance.md)

The hardware and the Windows USB stack:

- [xHCI programming](docs/usb-xhci-info/xhci-programming.md) and [xHCI data structures](docs/usb-xhci-info/xhci-data-structures.md)
- [USBPORT miniport interface](docs/usb-xhci-info/usbport-miniport-interface.md) and [ABI](docs/usb-xhci-info/usbport-miniport-abi.md) - the undocumented contract this driver plugs into
- [Windows 98/2000 WDM constraints](docs/usb-xhci-info/win98-wdm.md)

[AGENTS.md](AGENTS.md) is the guide for AI agents, and doubles as the short orientation for a human.

## AI usage

This project was developed with substantial help from AI coding agents, Claude Code (Claude Fable 5 and Claude Opus 5) and OpenAI Codex (GPT 5.6 Sol). The agents wrote and revised code, tests and documentation under the guidance in [AGENTS.md](AGENTS.md).

Kheng Meng directed the work, ran the hardware validation on real machines and reviewed what went in.

## References

- [xHCI specification](https://www.intel.com/content/www/us/en/content-details/868295/extensible-host-controller-interface-for-universal-serial-bus-xhci-requirements-specification-r1-2c.html) (Intel), citations verified against revision 1.2c
- [USB-IF xHCI backwards compatibility testing](https://www.usb.org/sites/default/files/xHCI_Backwards_Compatibility_Testing_v1_7.pdf) (v1.7)
- [ReactOS usbport](https://github.com/reactos/reactos/tree/master/drivers/usb/usbport) - primary reference for the `usbport.sys` miniport interface (undocumented by Microsoft)
- [Linux xhci-pci.c](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-pci.c) - controller quirk table
- [Haiku XHCI driver](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/busses/usb/xhci.cpp) and [FreeBSD xhci.c](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/xhci.c) - second opinions on hardware details

Neither PDF is tracked here. Fetch your own copies into the git-ignored `docs/references/`. [docs/references/README.md](docs/references/README.md) records the versions and SHA-256 sums. Mirrors of the source references can be fetched into `external/`. See [external/README.md](external/README.md).

## Licensing and provenance

This project's own source is licensed under the GNU General Public License, version 2 ([LICENSE](LICENSE)), `GPL-2.0-only`. 

The repository tracks no third-party binary on its own, although the two tool executables it tracks under `releases/` carry statically linked third-party runtimes.

* `xhci98.sys` links no runtime or extender. The 32-bit build links no third-party object either; the 64-bit build, linked by WDK 7.1, carries that kit's `/GS` stack-cookie handler (`__security_check_cookie` / `__report_gsfailure`), which nothing in this project's source calls.

* `XHCIQUAL.EXE` embeds the Open Watcom runtime and the DOS/32A extender. 

* `XHCISNAP.EXE` embeds the MSVC 6.0 runtime.

Each ships with a `NOTICE.TXT` recording it, and the `LICENSE` scope note says that those runtimes carry their own terms and are outside the grant. Neither quotes the terms themselves.

The full inventory, provenance methods and redistribution boundaries are in [docs/contributing/legal-provenance.md](docs/contributing/legal-provenance.md), which states facts, not legal conclusions.
