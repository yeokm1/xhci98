# xhci98

This project xHCI98 is a USB host controller driver (HCD) for xHCI host controllers. It supports Windows 98 SE, ME, 2000, XP (x86/x64), Vista (x86/x64) and 7 (x86/x64).

Since `2.0.0.0` the driver replaces Windows' own USB port and hub drivers with its own, so it is no longer limited to USB 2.0. It drives:

- SuperSpeed (USB 3.x) devices on the root ports
- SuperSpeed hubs
- USB 2.0 and 1.1 (High, Full and Low Speed) devices and hubs
- UAS (USB Attached SCSI) storage, through a second driver, `xhciuas.sys`

`1.2.0.0`, the USB 2.0-only `usbport.sys` miniport, is frozen and stays available under [releases/1.2.0.0](releases/1.2.0.0/readme.txt) for anyone who needs it.

This driver is developed based on Intel's xHCI specification and tested mainly on Intel machines so far. No guarantees have been made on xHCI implementations from other vendors. [Omores](https://www.youtube.com/@O_mores) has also [tested 1.x](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) on some AMD AM4 and AM5 platforms.

This project is from a solo human with AI-assistance only so bugs are not unexpected. Feel free to report them if you encounter any issues.

<img src="images/xhci98-usb-devices.jpg" width="800">

This is my 2020 ThinkPad P14s Gen 1 (Comet Lake xHCI, no EHCI) on Windows 98 SE, taken with the 1.x driver. Connected devices are 7-port hub, USB Ethernet, USB Audio, a USB-to-SATA bridge, two flash drives and a mouse all using the xHCI controller.

Video by [Omores](https://www.youtube.com/@O_mores) featuring this driver:

[![omores video](https://img.youtube.com/vi/7SOyvvC7P4E/hqdefault.jpg)](https://www.youtube.com/watch?v=7SOyvvC7P4E)

My personal demo video: https://www.youtube.com/watch?v=AU77f9CSbYc

Blog post of this project: https://yeokhengmeng.com/2026/08/xhci98-usb-host-driver/

## Motivation

Modern PCs from the mid-2010s onwards use xHCI-only USB chipsets. Windows 98 shipped with UHCI/OHCI drivers (USB 1.1) and gained EHCI (USB 2.0) through the back-ported stack in NUSB. Windows 2000 gained EHCI natively in SP4.

Neither has native xHCI support at all so even basic devices like keyboards, mice and flash drives do not work out of the box without BIOS support. Even with BIOS support, those devices cannot be hotplugged and other devices like Ethernet and Audio are not supported. Windows 7 is no better, having no USB 3 stack of its own.

This project attempts to fill that gap.

## SuperSpeed and UAS

<img src="images/xhci98-flash-speed-test.jpg" width="800">

My ThinkPad P14s Gen 1 under Windows 98 SE with NUSB, on `2.0.0.0`: the MSSU10-128GSR flash drive at SuperSpeed through `xhciuas.sys`. Device Manager shows the controller, its root hub and the UAS storage device, and the drive is listed under Unplug or Eject Hardware.

ATTO Disk Benchmark (Direct I/O) reads about 201 to 215 MB/s write and 196 to 221 MB/s read from 512 KB transfers upward. `1.2.0.0` managed about 34 MB/s at High Speed on the same drive.

Some things to know:

- A UAS-capable drive gets UAS, and anything else gets Windows' own `usbstor.sys` (Bulk-Only), at whatever speed it connects. A drive that offers both can be forced to Bulk-Only (see "Tuning" below).
- Device Manager and the other Windows tools on these systems show a SuperSpeed device as High Speed at most. The interface they read predates SuperSpeed, so that display says nothing about the real link speed.
- On Windows 98 SE storage of any kind, UAS included, needs NUSB's mass-storage component (see "Installation Steps").
- Before `2.2.0.0`, SuperSpeed devices did not work on Intel Sunrise Point-LP (`8086:9D2F`: ThinkPad E460, HP EliteBook 850 G5) - a USB 3 stick was not seen and a USB 3 hub showed only its USB 2.0 half. Fixed in `2.2.0.0`; on my E460 a UAS stick now runs at about 242 MB/s write and 245 MB/s read. See [issue 11](docs/issues/11-sunrise-point-ssic-psi-table.md).

## Installation Steps

The prerequisites per OS:

- **Windows 98 SE**:
  - Install [NUSB 3.3 or 3.6](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html) if you want USB storage, UAS included.
  - [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip) also works but has no storage half: unzip, right-click the `USB2.INF` at its root then install. With it alone, a USB stick shows Code 28 and a UAS drive Code 2, so storage still needs NUSB's storage files (below).
  - With neither, HID and audio devices work but storage has no driver.
  - Reboot if requested after installing either.
- **Windows ME**: Use [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip). ME uses its own storage files, not NUSB's. It copies them from its own installation files, with no prompt, the first time an ordinary USB stick is installed. Until then a UAS drive shows Code 2 (see "Known limitations/issues").
- **Windows 2000 SP4, XP SP3 x86**: Nothing to install.
- **Windows XP SP2 x64, Vista SP2 and 7 SP1 (x86/x64)**: Nothing to install either. On **Vista x64 and 7 x64**, driver signature enforcement has to be disabled as this driver is not signed.

On Windows 98 SE, storage of any kind, UAS included, needs NUSB's mass-storage component: `USBSTOR.INF`/`USBSTOR.SYS`, `USBNTMAP.INF`/`USBNTMAP.SYS` and `USBMPHLP.PDR`, which NUSB 3.3 and 3.6 install. SweetLow's stack alone has no storage half. On Windows ME those files are ME's own.

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

### The package

```
release-x86\   xhci98.sys, xhci98.inf, xhciuas.sys, xhciuas.inf - 32-bit Windows
               (98 SE, ME, 2000, XP, Vista, 7) - and txtsetup.oem, for F6
debug-x86\     the same five files, built for troubleshooting
release-x64\   the same five files for 64-bit Windows (XP x64, Vista x64, 7 x64)
debug-x64\     the same, built for troubleshooting
xhciqual\      XHCIQUAL.EXE, the DOS qualifier
xhcisnap\      XHCISNAP.EXE, the log snapshot reader
readme.txt     the release notes in plain text
```

`release-x86\` or `release-x64\` is the one to install. The `debug-` directories hold the same drivers built for troubleshooting, only install if asked. Each directory carries both drivers, each with its own INF.

### Install

1. Put the unzipped package somewhere the machine can read: a floppy, a CD, a shared folder.
2. Install the prerequisites above. On Windows 98 SE the order for a fresh install is NUSB, then xhci98, then the UAS disk.
3. In Device Manager, find the unrecognised xHCI controller. It sits unclaimed with a yellow mark, usually under "Other devices" such as "Universal Serial Bus Controller".
4. Properties -> Driver -> Update Driver -> Specify a location/Have Disk -> the `release-x86\` or `release-x64\` directory.
5. It installs as "xHCI98 USB 3.x eXtensible Host Controller" with "xHCI98 USB 3.x Root Hub" underneath it, and neither should carry a warning mark. Windows 98 SE may ask for its CD for `usbd.sys`.
6. Reboot if requested.
7. Plug in a UAS disk, and when the wizard asks for "xHCI98 USB Attached SCSI Storage", point it at the same directory. No restart is needed for `xhciuas.sys`.
8. An external hub installs as "xHCI98 USB Hub" from the driver already installed. On Windows 98 SE and 2000 it installs with nothing to answer. On Windows XP every newly plugged hub brings the Found New Hardware wizard and the unsigned-driver warning (Continue Anyway).

### Installing Windows 2000 or XP, or using the Recovery Console

This is for a machine whose keyboard or install medium is on the xHCI controller.

1. Copy the files of `release-x86\` (`release-x64\` for XP x64) to the root of a floppy.
2. Press F6 when text-mode Setup offers it.
3. Press S and pick "xHCI98 USB 3.x Host Controller".

The same floppy serves the Recovery Console of Windows 2000 and XP. Press F6 as for an install, then R at Setup's Welcome screen (on Windows 2000, then C for the console). The USB keyboard logs in and types commands.

Things to know:

- Pressing F6 needs the BIOS's own USB keyboard support.
- A UAS disk is not usable until GUI-mode Setup; a plain USB flash stick is.
- A USB drive present at the partition screen takes `C:`. Unplug the USB drives you do not need, or Windows installs to the next letter.
- On Windows 2000, plug the USB keyboard and stick in before Setup starts.
- To install Windows XP and XP x64, have a PS/2 keyboard or a laptop's built-in keyboard at hand. Later in Setup the USB keyboard does not work until a step that first asks about this unsigned driver (see "Known limitations/issues"). The Recovery Console runs in text mode throughout and is not affected.

The [release notes](docs/using/release-notes.md) have the limits.

<img src="images/xhci98-driver-info.jpg" width="800">

The `2.0.0.0` controller in Device Manager on the P14s Gen 1 under Windows 98 SE.

### Upgrading from 1.x.x.x to 2.y.y.y

A `2.y.y.y` release replaces the `1.x.x.x` driver file, which has the same name; `1.2.0.0` was the last `1.x.x.x` release.

On every system, update the "USB 2.0 eXtensible Host Controller (xhci98)" entry in Device Manager, and always pick the driver from a list rather than let Windows search: searching can reinstall the old driver from Windows' own copy instead.

- **Windows 98 SE with NUSB**: do not use Update Driver while `1.x.x.x` is running: NUSB's `usbport.sys` crashes the machine with a blue screen as it stops the old driver. Instead:
  1. Open an MS-DOS Prompt and type `ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.SAV`.
  2. Shut the machine down and switch it on again. The controller now shows a yellow mark.
  3. In Device Manager open the controller, Update Driver, "Display a list of all the drivers in a specific location", Have Disk -> the `release-x86\` directory.
  4. Pick "xHCI98 USB 3.x eXtensible Host Controller", give it the Windows 98 SE CD when it asks for `usbd.sys`, and restart when asked.
  5. Each USB device is found once more as new hardware; let Windows install it. It may ask for the CD again.

  If you already updated in place and got the blue screen, restart: the `2.y.y.y` driver comes up on its own.
- **Windows 98 SE with SweetLow's stack**: Update Driver, "Display a list of all the drivers in a specific location", Have Disk -> the `release-x86\` directory, pick "xHCI98 USB 3.x eXtensible Host Controller".

  Windows does not ask you to restart, but you must: shut down and switch on again straight away. Until you do, USB devices stop working and Windows may respond slowly. After the restart each USB device is found once more as new hardware.
- **Windows ME**: ME runs the same USB 2.0 stack as Windows 98 SE with SweetLow's, so follow that route above, restart included.
- **Windows 2000**: Driver -> Update Driver -> "Display a list of the known drivers" -> Have Disk -> the `release-x86\` directory.

  Windows lists three models: pick "xHCI98 USB 3.x eXtensible Host Controller", the first, not the Root Hub or the storage entry. It starts at once with no restart. At your next restart Windows may ask for one more; say Yes.
- **Windows XP**: Driver -> Update Driver: "No, not this time", "Install from a list or specific location", "Don't search. I will choose the driver to install", Have Disk -> the package directory, "xHCI98 USB 3.x eXtensible Host Controller", Continue Anyway.

  A second wizard follows for "xHCI98 USB 3.x Root Hub": "No, not this time", "Install the software automatically", Continue Anyway, Finish. No restart.
- **Windows Vista and 7**: Update Driver Software -> "Browse my computer for driver software" -> "Let me pick from a list of device drivers on my computer" -> Have Disk -> the package directory -> "xHCI98 USB 3.x eXtensible Host Controller" -> "Install this driver software anyway". No restart.

  Do not just type the folder into the search box: Windows answers that the best driver is already installed and keeps the old one.

On XP x64, Vista x64 and 7 x64 the same steps apply, pointed at `release-x64\`.

After upgrading you can delete the `1.2.0.0` virtual-hub values `XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` from the controller's driver key (see "Tuning" below for where it is). They have no effect under `2.y.y.y`, so leaving them is harmless too.

After the upgrade every device is a new Device Manager entry, so a setting kept on a device's own entry, such as SweetLow's hidusbf polling rate, has to be applied again.

### Updating from 2.y.y.y
Install the new version over the old one with Update Driver on "xHCI98 USB 3.x eXtensible Host Controller", then on "xHCI98 USB 3.x Root Hub", both pointed at the `release-x86\` or `release-x64\` directory. No file needs renaming, since neither `2.x` release uses NUSB's `usbport.sys`.

- **Windows 98 SE and ME: restart afterwards, although Windows does not ask.** The new file waits to replace the old one at the next start, and until then the old driver keeps running. Under SweetLow's stack the controller may show a problem for a minute or two after Finish. On ME it shows one until the restart, while the devices keep working.
- **Windows 2000: use Have Disk.** Letting Windows search answers that a suitable driver is already installed and keeps the old one. Use "Display a list of the known drivers" -> Have Disk, as in the steps above.
- **XP, XP x64, Vista and 7**: the update takes effect at once with no restart, after one unsigned-driver warning.

Afterwards the root hub's Driver tab still shows the old version (on 98 SE and ME, its date), although it runs the new file. To change it, run Update Driver on "xHCI98 USB 3.x Root Hub" too:

- On Vista and 7, "Let me pick from a list of device drivers on my computer" and the new version's entry. Searching does not always find it.
- Elsewhere, Have Disk.

From `2.0.0.0` only, each device is found once more as new hardware, exactly once: a stick under its serial number, the other devices under a new id. Let Windows install them.

- On the NT systems this needs no answer.
- On Windows 98 SE the wizard runs for each and may ask for the CD for `hidclass.sys`.
- A hidusbf setting on such a device has to be applied again.

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one installed, "Search for a better driver" keeps the cached INF. Instead, Update Driver on "xHCI98 USB Attached SCSI Storage", choose "Display a list of all the drivers in a specific location", then Have Disk -> the package directory.

## Tuning

Each value below is a `DWORD` in the controller's driver (software) key. Here is where to find the key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host Controller". The number varies from machine to machine.

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciImodInterval250ns` | The interrupt moderation interval | `10` | `4000` | `4000` | `160` |
| `XhciForceBulkOnly` | Bulk-Only instead of UAS | `0` | `1` | `0` | `0` |
| `XhciFastPollFsLs` | A root-port Low- or Full-Speed mouse polled above 1000 Hz | `0` | `3` | `0` | `0` |
| `XhciFirstEnumWaitMs` | The longest wait for a hub's first report | `0` | `30000` | `5000` | `5000` |
| `XhciFirstEnumPortMs` | The longest one port may hold that wait | `0` | `XhciFirstEnumWaitMs` | `2000` | `2000` |
| `XhciIntelPortSwitch` | The Intel 7/8/9-series port switchover; `0` turns it off, `2` applies it to any Intel controller **at your own risk** | `0` | `2` | `1` | `1` |
| `XhciTolerance` | The handling of controller faults, all together; `0` turns most of it off (see "Controller faults") | `0` | `1` | `1` | `1` |
| `XhciIntervalCap` | Interrupt endpoints polled at least every 32 ms: `1` on AMD controllers, `2` on every controller, `0` off | `0` | `2` | `1` | `1` |
| `XhciAvgTrbEsit` | A comparison switch for interrupt endpoints; leave at `0` unless comparing | `0` | `1` | `0` | `0` |
| `XhciLogVerbosity` | The driver's log, read by `XHCISNAP` | `0` | `4` | `0` | `0` |
| `XhciLogDebugView` | The log sent to DebugView as well | `0` | `1` | `0` | `0` |

An install or update writes a value only where it is missing, so a value you changed stays as you set it. To go back to the table's value, set it by hand, or delete the value and update the driver. A machine updated straight from `1.2.0.0` keeps that release's `XhciImodInterval250ns` of `500`; set it to `160` by hand.

The two log values are described in the [release notes](docs/using/release-notes.md), "The log, and how to send one". The others are described below.

### The interrupt moderation interval

`XhciImodInterval250ns` is in **units of 250 ns**. It sets how long the controller waits after one interrupt before raising the next. A shorter interval makes USB storage faster at the cost of more interrupts.

| | Value | Interval | Interrupts per second, at most |
|---|---|---|---|
| Written by the install | `160` | 40 us | 25,000 |
| Used when the value is missing, unreadable, or outside `10`-`4000` | `4000` | 1 ms | 1,000 |
| Lowest accepted | `10` | 2.5 us | 400,000 |

A value outside `10`-`4000` is replaced by `4000`, not rounded to the nearest limit, so a mistyped `0` cannot turn moderation off. `4000` is the controller's own power-on value.

`160` is the value Linux uses, and every install path writes it. On my P14s with the MSSU10 over UAS (Windows 98 SE, one command at a time), at 8 MB transfers:

- `500`, the value `1.2.0.0` shipped, gave about 181 MB/s.
- `160` gave 211 MB/s write and 221 MB/s read.
- `40` added only 1 to 3% more.

Feel free to tune it. Raise it towards `4000` (or delete it) if you get audio stutter or instability under load.

### Forcing Bulk-Only instead of UAS

`XhciForceBulkOnly` set to `1` makes every storage device on that controller that offers both transports use Bulk-Only (Windows' own `usbstor.sys`) instead of UAS. `0` or absent, the default, means UAS wherever the device offers it. The install writes `0`.

It is read each time a device enumerates, so unplug and replug the drive after changing it. On Windows 2000 and later, a drive already installed keeps its driver until you uninstall it in Device Manager and replug it. A UAS-only device stays on UAS whatever the value says.

### Polling a Low- or Full-Speed mouse above 1000 Hz

`XhciFastPollFsLs`, new in `2.1.0.0`, is off by default; the install writes `0`. It works with SweetLow's hidusbf setting a mouse on a **root port** to its "31 Hz" or "62 Hz" rate:

| Value | "31 Hz" becomes | "62 Hz" becomes |
|---|---|---|
| `2` | 2000 Hz | 4000 Hz |
| `3` | 4000 Hz | 8000 Hz |

- A device behind a hub keeps its normal rate.
- While it is set, any root-port Low- or Full-Speed device that asks for 16 to 63 ms is polled faster too.
- It is read when the controller starts, so restart after changing it.

This is outside the xHCI specification. A controller that refuses it is caught: the device runs at its normal rate and `XHCISNAP` counts it as `fastpoll.fallbacks`. A controller that accepts it and then misbehaves cannot be caught. It is untested ground.

### The first report's wait

When the root hub or an external hub first reports its devices after it starts, the driver waits for the devices already plugged in to be ready, so Windows 2000's text-mode Setup sees them. The install writes both at their defaults.

| Value | Meaning | Default |
|---|---|---|
| `XhciFirstEnumWaitMs` | The longest wait in milliseconds. `0` turns it off; at most `30000` | `5000` |
| `XhciFirstEnumPortMs` | The longest one port may hold it, held to the total | `2000` |

With the defaults, the root hub's first report typically comes 20 to 30 ms after its start with nothing plugged in, and 0.3 to 0.9 s after it with a mouse and a stick plugged in.

### The Intel 7/8/9-series port switchover

On Intel 7-, 8- and 9-series chipsets (Ivy Bridge to Broadwell) and C610/X99, each switchable connector, usually a blue one, is wired to both the USB 2.0 (EHCI) and the xHCI controller. With the firmware's USB 3.0 setting on Auto, or no setting at all, those connectors typically start on the USB 2.0 controller, so up to `2.1.0.0` this driver saw nothing on them.

Since `2.1.1.0` the driver moves them to the xHCI controller at each start and resume, and hands them back to the USB 2.0 controller when its controller is disabled, removed or the machine shuts down. It does this only on Intel xHCI device ids `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` and `8D31`; every other controller is left alone unless `XhciIntelPortSwitch` is `2` (below). Since `2.2.0.0` the hand-back writes only the registers the driver itself wrote.

- A device on a switchable connector under a running USB 2.0 driver (NUSB's, for example) is disconnected there when this driver starts and comes back under it. Do not have a drive busy on a blue connector at that moment.
- `XhciIntelPortSwitch` set to `0` turns it off. Only `0` does; absent or any other number is on. It is read when the controller starts, so restart after changing it.
- Since `2.2.0.0`, `XhciIntelPortSwitch` set to `2` does the switchover on any Intel xHCI controller, listed or not, for a chipset with the same switchable connectors that the list misses. On a listed controller `2` is the same as `1`.

> Setting `XhciIntelPortSwitch` to 2 writes Intel chipset registers on any Intel USB 3 controller. On one without them (every Intel chipset from the 100-series on, and any not yet read) it writes registers of unknown meaning, with unknown results. Use 2 only, at your own risk, for an unlisted Intel chipset with both EHCI and xHCI; everyone else should leave it at 1.

### Controller faults

Since `2.2.0.0` the driver no longer leaves a device dead until it is replugged after a transfer, port or controller fault. This follows a tester's report from an AMD AM5 board of a mouse that stops at random. In short:

- A missed interrupt is picked up by the driver itself.
- A USB transaction error is retried up to three times before the transfer fails.
- A fault the driver cannot tie to a transfer re-enumerates the device, as if it had been replugged.
- A USB 2.0 port the controller disabled, or one that reported an over-current, is brought back.
- A controller that halts is recovered; one that stops answering is closed off safely.

Each is tried a few times at most, then the driver stops and holds the port or the controller until the device is unplugged or the controller is restarted. None of it acts unless a fault is reported. `XhciTolerance` set to `0` turns it off. `XhciIntervalCap` and `XhciAvgTrbEsit` are two related settings for AMD controllers.

If a USB device stops working on your machine, send two `XHCISNAP` captures (one while it works, one after it stops) and the controller's id from `XHCIQUAL`. The [release notes](docs/using/release-notes.md), "Controller faults: what the driver does about them", and the package's `readme.txt` have the details, including the exceptions.

### The 1.2.0.0 virtual-hub values

`XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` have no effect under `2.y.y.y`. The driver reports every device at its true speed with no virtual hub in the way, so there is nothing for them to switch.

## What is tested, and what is not

Windows 98 SE and Windows 2000 SP4 are the primary targets. Windows 98 SE has also run on real hardware, the ThinkPad E460 and the ThinkPad P14s Gen 1; every other target, Windows 2000 included, has run in QEMU virtual machines only. The [release notes](docs/using/release-notes.md)' "Targets and their standing" has the full detail per system.

| Target | State |
|---|---|
| Windows 98 SE | VMs under NUSB and SweetLow's stack: HID, storage, composite audio, the ASIX Ethernet adapter, hubs to five tiers, plug soaks, SuperSpeed storage and UAS. A stock install runs HID and audio, with no storage driver. Real hardware: the E460 and the P14s Gen 1. |
| Windows 2000 SP4 | VMs only, under Driver Verifier and on a multiprocessor guest: the same as Windows 98 SE, including UAS and forced Bulk-Only. SuperSpeed hubs untested. |
| Windows ME | VMs only, under SweetLow's stack: HID, storage, hubs, composite audio, soaks, SuperSpeed storage and UAS. The `2.0.0.0` hang on re-enabling the controller with a mouse attached is fixed in `2.1.0.0`. |
| Windows XP, 32-bit and x64 | VMs only: HID, storage, hubs, composite audio, disable and enable, soaks, SuperSpeed storage and UAS. |
| Windows Vista SP2 and 7 SP1, 32-bit and x64 | VMs only: the same, on four virtual processors. On the x64 systems, driver signature enforcement must be disabled. |
| The release package | `2.0.0.0` and `2.1.0.0` each installed from the release download on all ten systems and passed. `2.1.0.0` also installed over `2.0.0.0` on each. |

| Machine | Controller | 2.0.0.0 result | Tested by |
|---|---|---|---|
| 2016 ThinkPad E460 | Intel Skylake, Sunrise Point-LP (100-series) PCH. xHCI 1.0. | Yes, but SuperSpeed devices only since `2.2.0.0` ([issue 11](docs/issues/11-sunrise-point-ssic-psi-table.md)) | Me |
| 2020 ThinkPad P14s Gen 1 | Intel Comet Lake PCH-LP (400-series). xHCI 1.1. | Yes | Me |
| Omores' Intel and AMD desktops | H110, B360, B550, X570, X670 | No `2.0.0.0` report yet. | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) (1.x) |

The devices, each characterised in [test-equipment.md](docs/contributing/test-equipment.md):

| Device | VID:PID | Speed | 2.0.0.0 result |
|---|---|---|---|
| Terminus 7-port hub, multi-TT | `1A40:0201` | High | Works, with Low- and Full-Speed devices behind it. |
| Terminus 4-port hub, single-TT | `1A40:0101` | High | Works, with Low- and Full-Speed devices behind it. |
| Genesys 7-port hub (two cascaded chips), single-TT | `05E3:0608` | High | Works, with Low- and Full-Speed devices behind it. |
| Genesys USB 3.0 hub | `05E3:0610`, `05E3:0612` | SuperSpeed and High | Works, with devices behind both halves. |
| A Full-Speed hub behind a High-Speed hub | | Full | Works (a USB 2.0 hub held at Full Speed by an isolator). |
| Logitech USB Optical Mouse | `046D:C077` | Low | Works at a root port and behind a hub, polled every 8 ms. |
| Microsoft Wired Keyboard 600 (composite) | `045E:0750` | Low | Works. |
| SanDisk U3 Titanium flash drive | `0781:5408` | High | Works, with a verified round trip. |
| MSSU10-128GSR flash drive | `090C:2320` | SuperSpeed, UAS | Works: UAS at SuperSpeed, forced Bulk-Only, and Bulk-Only behind a USB 2.0 hub. |
| SanDisk 3.2Gen1 flash drive | `0781:55AB` | SuperSpeed, Bulk-Only | Works at SuperSpeed and behind a USB 2.0 hub. |
| StoreJet Transcend USB-to-SATA bridge (ASMedia) | `174C:5106` | SuperSpeed, UAS and Bulk-Only | Works: UAS at SuperSpeed and High Speed, and forced Bulk-Only. |
| ASIX AX88772A USB Ethernet | `0B95:7720` | High | Works with ASIX's own drivers, and on 98 SE and 2000 in VMs. |
| Sound Blaster Play! 2 (UAC 1.0 composite) | `041E:323D` | Full | Works: played and heard at a root port and behind a hub. |
| C-Media USB Audio Device (UAC 1.0 composite) | `0D8C:0014` | Full | Works, and on 98 SE and 2000 in VMs. |

The `1.2.0.0` results are in its [README](https://github.com/yeokm1/xhci98/blob/1.2.0.0/README.md) and [release notes](https://github.com/yeokm1/xhci98/blob/1.2.0.0/docs/using/release-notes.md).

## Known limitations/issues

### Outside this driver's control

These come from Windows, NUSB or the driver being unsigned, and no change to this driver can remove them.

| Limitation | Detail |
|---|---|
| Upgrading in place over a running `1.x.x.x` under NUSB blue-screens | NUSB's `usbport.sys` crashes the machine as it stops `1.x.x.x`, before `2.y.y.y` runs. Follow "Upgrading from 1.x.x.x to 2.y.y.y" above, which avoids it. |
| Vista x64 and 7 x64 need driver signature enforcement disabled | The driver is not signed. Driver signature enforcement must be disabled at every start, or the controller sits at Code 39. |
| No USB storage on a stock Windows 98 SE | With no NUSB installed there is no mass-storage driver at all. HID and audio still work. |
| Windows XP installed with the F6 floppy needs a PS/2 or built-in laptop keyboard | Setup copies Windows' own HID and USB helper files only with Microsoft's own USB controller drivers, so in GUI-mode Setup the USB keyboard and mouse wait for its device install, which first asks about the unsigned driver (default No). A USB-only keyboard cannot answer it; a PS/2 or built-in laptop keyboard can. Windows 2000 does not ask. |
| Windows ME: unplugging a device while Windows installs it | ME's own device manager stops responding if a device is unplugged while Windows is still installing its driver. ME does the same on Microsoft's own USB stack. Wait for the install to finish before unplugging. |

### Not planned, though a later release might address them

| Limitation | Detail |
|---|---|
| The driver never starts selective suspend | Idle devices and hub ports are never suspended to save power. A suspend or resume a hub reports is handled. |
| USB storage on Windows 98 is slower than the drive | An observation, not a defect found: Windows 98 sends one command at a time. On the P14s with the MSSU10 at 64 KB, about 208 MB/s on Windows 98 against 277 MB/s on Windows 11 at the same queue depth of one. This may be looked into in a later release. |
| Windows 2000: a mounted USB drive may come back at Code 31 after a controller recovery | After the driver recovers the controller in place (a halted controller, or a failed endpoint reset), a USB drive whose volume was mounted may stay at Code 31 until it is unplugged and plugged in again, or the controller is disabled and enabled. Other devices come back. Seen in a virtual machine; not on Windows 98 SE. The path predates `2.2.0.0`, which recovers on more faults. |
| A UAS drive as the first USB storage device on Windows ME | On a fresh Windows ME installation whose first USB storage device is a UAS drive, the drive shows Code 2 (NTKERN.VXD device loader(s) could not load). ME has not yet copied its own `USBNTMAP.SYS` and `USBMPHLP.PDR`, which it installs only when its first ordinary USB stick is plugged in. To recover, plug in any ordinary USB stick once, then unplug the UAS drive and plug it back in. No Remove and no restart are needed. |

### Untested ground

| Area | State |
|---|---|
| SuperSpeed isochronous transfers | Built from the specification. No SuperSpeed isochronous device has been held and QEMU models none. |
| SuperSpeedPlus (USB 3.1 Gen 2, USB 3.2 Gen 1x2 and Gen 2x2) | Accepted at its trained rate, built from the specification. Not read on any hardware: no Gen 2 device has been tested, so every mode is untested. |
| A UAS-only drive at SuperSpeed on a controller without streams | It is sent back to its USB 2.0 port and runs UAS at High Speed, or is refused if it has no USB 2.0 port. Built from the specification; no such controller has been held. |
| Polling above 1000 Hz (`XhciFastPollFsLs`) | Outside the xHCI specification. Not read on any real controller or in any virtual machine. |
| The Intel port switchover beyond the B490 | Device ids `8C31`, `9C31`, `8CB1`, `9CB1` and `8D31`, systems other than Windows 98 SE, and standby. Built after Linux's handling of the same chipsets. |

## Toolchain and building

The drivers are C (C89/C90, no C++ or CRT), built and verified on Windows 11 x64. The toolchain unpacks inside the repository (`tools/`, git-ignored) and installs nothing to `C:\`. Every script finds it relative to its own location, so a clone builds wherever it is unpacked.

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

3. Build the drivers. `both` (the default) builds release and debug. `all` adds the emulator-only flavour, and is what a release cut uses. Each flavour builds `xhci98.sys` and then `xhciuas.sys`.

   ```
   scripts\build-driver.cmd both
   ```

   | Flavour | Output |
   |---|---|
   | `release` | `src\objfre\i386\xhci98.sys` and `src\uas\objfre\i386\xhciuas.sys` - the shipping drivers |
   | `debug` | `src\objchk\i386\xhci98.sys` and `src\uas\objchk\i386\xhciuas.sys` - the diagnostic builds, also shipped |
   | `qemu` | `src\objchk_qemu\i386\xhci98.sys` and `src\uas\objchk_qemu\i386\xhciuas.sys` - emulator-only, never published |

   Add `-amd64` for the 64-bit build, which is a **separate pair of binaries from a separate toolchain in a separate package** - no statement about "one binary" reaches it. The outputs are the same paths with `amd64` in place of `i386`.

   ```
   scripts\build-driver.cmd both -amd64
   ```

   Every build runs the gates, and any failure stops it:

   - the import, INF, packager, QEMU launcher and device-matrix self-tests;
   - the INF gates: `scripts\inf-gate\check-inf.ps1` on `src\xhci98.inf` and `src\xhci98-amd64.inf`, and `scripts\inf-gate\check-uas-inf.ps1` on `src\uas\xhciuas.inf` and `src\uas\xhciuas-amd64.inf`;
   - the batch-file line endings and the source charset gate (ASCII only, CRLF);
   - the host unit tests (`test\run-host-tests.cmd`), before any DDK build;
   - after each link, the import gate against each driver's own allowlist (`scripts\import-gate\xhci98-imports*.allow`, `xhciuas-imports*.allow`), the flavour-marker check and the source stamp.

   `-NoTargetEvidence` skips the import gate's target-file evidence steps on a clone that has no target files staged.

4. Build the two tools that ship beside the drivers. `xhciqual\build.cmd` produces `XHCIQUAL.EXE`, the DOS qualifier, and needs [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2/releases) at `C:\WATCOM` (or wherever `WATCOM` points). That is the only tool installed normally on the host, and the drivers never use it. `xhcisnap\build.cmd` produces `XHCISNAP.EXE`, the snapshot reader, with the in-repo MSVC 6.0.

5. Make install media. A `.sys` on its own is not install media; each INF travels with its driver, and nothing else does, because the controller's INF has Windows supply its own `usbd.sys` and `usbui.dll`.

   | Script | Output |
   |---|---|
   | `scripts\package\make-package.ps1` | `out\pkg-<flavour>-<arch>\` - both drivers and their INFs, media a VM or a machine can be pointed at |
   | `scripts\package\make-release.ps1` | `releases\<version>\` and `out\xhci98-<version>.zip` - the published cut, four directories, each with both drivers |

6. Test in QEMU with the `qemu-xhci` device: a Win98 SE guest, a Win2000 SP4 guest, and an SMP Win2000 guest for race detection, with `usb-storage` and `usb-uas` for SuperSpeed and UAS.

The version lives in [src/xhci_version.h](src/xhci_version.h). The `DriverVer` of all four INFs is a literal that the build's INF gates check against it. See [docs/contributing/build-and-test.md](docs/contributing/build-and-test.md) for the VM setup, versioning, packaging and the rest of the procedure.

## Repository Layout

```
src/            The host controller driver xhci98.sys (C), its two INFs and
                the DDK build files
  uas/          The UAS class driver xhciuas.sys, its two INFs and build files
test/           Host-side unit tests for the DDK-free core (test\run-host-tests.cmd)
scripts/        Setup helpers, build wrapper, the import/INF/packaging gates,
                and vm-matrix/ - the automated device matrix
xhciqual/       The DOS hardware-qualification tool (Open Watcom) and its
                bare-metal run logs under results/
xhcisnap/       The snapshot reader for the driver's log channel
docs/           Documentation, indexed by docs/README.md
  using/        The user-facing release notes and acceptance test
  contributing/ Roadmaps, architecture, build/test/runbooks, diagnostics,
                 invariants, provenance, and numbered design records
  usb-xhci-info/ xHCI programming/data structures, WDM constraints, and the
                 USBPORT ABI the 1.x miniport used
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

- [Release notes](docs/using/release-notes.md) - what the drivers do, do not, and do not yet claim, per target
- [Release acceptance test](docs/using/release-acceptance-test.md) - the fixed procedure for checking a release on a new machine
- [Release history](releases/history.md) - what each version changed

Working on the driver:

- [Build and test](docs/contributing/build-and-test.md) - toolchain setup, builds, VMs, install, debugging, packaging, recovery
- [Roadmap for 2.0.0.0 onward](docs/contributing/roadmap-hcd.md) - the host controller driver, Phases 25 onward, UAS in Phase 31; the 1.x miniport's [Phases 0-16](docs/contributing/roadmap.md) and [17-24](docs/contributing/roadmap-miniport-updates.md) are closed records
- [Design record 13](docs/contributing/design/13-superspeed-hcd.md) - the host controller driver: the bus, hubs and composite devices, the UAS id policy, the INFs and the package
- [Design records](docs/contributing/design/README.md) - the numbered design decisions
- [Architecture](docs/contributing/architecture.md) and [implementation invariants](docs/contributing/implementation-invariants.md)
- [Source files](docs/contributing/source-files.md) - what every file in `src/` is for
- [Failure diagnosis](docs/contributing/failure-diagnosis.md) and [measured lessons](docs/contributing/lessons.md) - read these before theorising about a failure
- [Test equipment, as measured](docs/contributing/test-equipment.md) - every hub and device used for hardware validation
- [Legal and provenance record](docs/contributing/legal-provenance.md)

The hardware and the Windows USB stack:

- [xHCI programming](docs/usb-xhci-info/xhci-programming.md) and [xHCI data structures](docs/usb-xhci-info/xhci-data-structures.md)
- [Windows 98/2000 WDM constraints](docs/usb-xhci-info/win98-wdm.md)
- [USBPORT miniport interface](docs/usb-xhci-info/usbport-miniport-interface.md) and [ABI](docs/usb-xhci-info/usbport-miniport-abi.md) - the undocumented contract the 1.x miniport plugged into

[AGENTS.md](AGENTS.md) is the guide for AI agents, and doubles as the short orientation for a human.

## AI usage

This project was developed with substantial help from AI coding agents, Claude Code (Claude Fable 5, Claude Opus 5 and Claude Opus 5.5) and OpenAI Codex (GPT 5.6 Sol). The agents wrote and revised code, tests and documentation under the guidance in [AGENTS.md](AGENTS.md).

Kheng Meng directed the work, ran the hardware validation on real machines and reviewed what went in.

## References

- [xHCI specification](https://www.intel.com/content/www/us/en/content-details/868295/extensible-host-controller-interface-for-universal-serial-bus-xhci-requirements-specification-r1-2c.html) (Intel), citations verified against revision 1.2c
- [USB 3.2 specification](https://www.usb.org/document-library/usb-32-revision-11-june-2022) (USB-IF), revision 1.1 (June 2022) with its ECNs - SuperSpeed, SuperSpeed hubs and the BOS descriptors, cited as `USB 3.2 p.N`
- [USB-IF xHCI backwards compatibility testing](https://www.usb.org/sites/default/files/xHCI_Backwards_Compatibility_Testing_v1_7.pdf) (v1.7)
- [How does USB stack enumerate a device?](https://techcommunity.microsoft.com/blog/microsoftusbblog/how-does-usb-stack-enumerate-a-device/270685) (Microsoft USB blog) - the Windows hub driver's enumeration steps, timeouts and retries, which the first-enumeration wait's defaults follow
- [ReactOS usbport](https://github.com/reactos/reactos/tree/master/drivers/usb/usbport) - primary reference for the `usbport.sys` miniport interface of the 1.x driver (undocumented by Microsoft)
- [Linux xhci-pci.c](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-pci.c) - controller quirk table
- [Haiku XHCI driver](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/busses/usb/xhci.cpp) and [FreeBSD xhci.c](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/xhci.c) - second opinions on hardware details

None of these PDFs is tracked here. Fetch your own copies into the git-ignored `docs/references/`. [docs/references/README.md](docs/references/README.md) records the versions and SHA-256 sums. Mirrors of the source references can be fetched into `external/`. See [external/README.md](external/README.md).

## Licensing and provenance

This project's own source is licensed under the GNU General Public License, version 2 ([LICENSE](LICENSE)), `GPL-2.0-only`.

The repository tracks no third-party binary on its own, although the two tool executables it tracks under `releases/` carry statically linked third-party runtimes.

* `xhci98.sys` and `xhciuas.sys` link no runtime or extender. The 32-bit builds link no third-party object either; the 64-bit builds, linked by WDK 7.1, carry that kit's `/GS` stack-cookie handler (`__security_check_cookie` / `__report_gsfailure`), which nothing in this project's source calls.

* `XHCIQUAL.EXE` embeds the Open Watcom runtime and the DOS/32A extender.

* `XHCISNAP.EXE` embeds the MSVC 6.0 runtime.

Each tool ships with a `NOTICE.TXT` recording it, and the `LICENSE` scope note says that those runtimes carry their own terms and are outside the grant. Neither quotes the terms themselves.

The release download carries no Microsoft file. `usbd.sys` and `usbui.dll` are the OS's own, copied by Windows Setup from its own install source. On Windows 98, `xhciuas.inf` names NUSB's `USBNTMAP.SYS` as a filter on the UAS device; the file is named, never shipped, and NUSB's mass-storage install places it.

The full inventory, provenance methods and redistribution boundaries are in [docs/contributing/legal-provenance.md](docs/contributing/legal-provenance.md), which states facts, not legal conclusions.
