# xhci98

This project xHCI98 is a USB host controller driver (HCD) for xHCI host controllers. It supports Windows 98 SE, ME, 2000, XP (x86/x64), Vista (x86/x64) and 7 (x86/x64).

The driver replaces Windows' own USB port and hub drivers with its own, so it is not limited to USB 2.0. It drives:

- SuperSpeed (USB 3.x) devices on the root ports
- SuperSpeed hubs
- USB 2.0 and 1.1 (High, Full and Low Speed) devices and hubs
- UAS (USB Attached SCSI) storage, through a second driver, `xhciuas.sys`
- Thunderbolt 3 and USB4 docks and enclosures, through a third driver, `xhcicm.sys` (see "Thunderbolt 3 and USB4")

This driver is developed based on Intel's xHCI specification and tested mainly on Intel machines so far.

No guarantees have been made on xHCI implementations from other vendors.
Best effort has been made to be compatible with the quirks of controllers from Fresco Logic, ASMedia, AMD, TI, Etron, VIA and some Intel controllers, even ones built to the xHCI 0.96 draft standard.

This project is from a solo human with AI-assistance only so bugs are not unexpected. Feel free to report them if you encounter any issues.

<img src="images/xhci98-usb-devices.jpg" width="800">

This is my 2020 ThinkPad P14s Gen 1 (Comet Lake xHCI, no EHCI) on Windows 98 SE, taken with the 1.x driver. Connected devices are 7-port hub, USB Ethernet, USB Audio, a USB-to-SATA bridge, two flash drives and a mouse all using the xHCI controller.

Video by [Omores](https://www.youtube.com/@O_mores) featuring this driver:

[![omores video](https://img.youtube.com/vi/7SOyvvC7P4E/hqdefault.jpg)](https://www.youtube.com/watch?v=7SOyvvC7P4E)

My personal demo video: https://www.youtube.com/watch?v=AU77f9CSbYc

Blog post of this project: https://yeokhengmeng.com/2026/08/xhci98-usb-host-driver/

## Contents

- [Motivation](#motivation)
- [SuperSpeed and UAS](#superspeed-and-uas)
- [Thunderbolt 3 and USB4](#thunderbolt-3-and-usb4)
  - [Before you start: the BIOS](#before-you-start-the-bios)
  - [Installing for Thunderbolt and USB4](#installing-for-thunderbolt-and-usb4)
  - [Approving devices](#approving-devices)
  - [Removing a device safely](#removing-a-device-safely)
  - [NVMe drives](#nvme-drives)
  - [Things to know](#things-to-know)
- [Installation Steps](#installation-steps)
  - [The package](#the-package)
  - [Install with INSTALL.BAT](#install-with-installbat)
  - [Install](#install)
  - [Windows Vista x64 and 7 x64: test-signing mode](#windows-vista-x64-and-7-x64-test-signing-mode)
  - [Installing Windows 2000 or XP, or using the Recovery Console](#installing-windows-2000-or-xp-or-using-the-recovery-console)
  - [Upgrading the driver](#upgrading-the-driver)
  - [Updating xhciuas.sys over an older one](#updating-xhciuassys-over-an-older-one)
- [Tuning](#tuning)
- [What is tested, and what is not](#what-is-tested-and-what-is-not)
- [Known limitations/issues](#known-limitationsissues)
  - [Outside this driver's control](#outside-this-drivers-control)
  - [Not planned, though a later release might address them](#not-planned-though-a-later-release-might-address-them)
  - [Untested ground](#untested-ground)
- [Toolchain and building](#toolchain-and-building)
- [Repository Layout](#repository-layout)
- [Documentation](#documentation)
- [AI usage](#ai-usage)
- [References](#references)
- [Licensing and provenance](#licensing-and-provenance)

## Motivation

Modern PCs from the mid-2010s onwards use xHCI-only USB chipsets. Windows 98 shipped with UHCI/OHCI drivers (USB 1.1) and gained EHCI (USB 2.0) through the back-ported stack in NUSB. Windows 2000 gained EHCI natively in SP4.

Neither has native xHCI support at all so even basic devices like keyboards, mice and flash drives do not work out of the box without BIOS support. Even with BIOS support, those devices cannot be hotplugged and other devices like Ethernet and Audio are not supported. Windows 7 is no better, having no USB 3 stack of its own.

This project attempts to fill that gap.

## SuperSpeed and UAS

<img src="images/xhci98-flash-speed-test.jpg" width="800">

My ThinkPad P14s Gen 1 under Windows 98 SE with NUSB, the MSSU10-128GSR flash drive at SuperSpeed through `xhciuas.sys`. Device Manager shows the controller, its root hub and the UAS storage device, and the drive is listed under Unplug or Eject Hardware.

ATTO Disk Benchmark (Direct I/O) reads about 201 to 215 MB/s write and 196 to 221 MB/s read from 512 KB transfers upward.

Some things to know:

- A UAS-capable drive gets UAS, and anything else gets Windows' own `usbstor.sys` (Bulk-Only), at whatever speed it connects. A drive that offers both can be forced to Bulk-Only (see "Tuning" below).
- Device Manager and the other Windows tools on these systems may show a SuperSpeed device as High Speed at most. The interface they read predates SuperSpeed.

## Thunderbolt 3 and USB4

A third driver in the package, `xhcicm.sys`, brings Thunderbolt 3 and USB4 docks, enclosures and NVMe cases to these systems. Plug a device in while Windows is running and the devices inside it, such as an NVMe drive, a dock's own USB controller or its network adapter, appear in Device Manager within seconds with no Refresh, and leave when it is unplugged. A device the BIOS connected at power-on is kept as it is.

- On Intel Thunderbolt 2 and 3 controllers, and on the USB4-generation Intel ones while their firmware runs its own connection manager, the driver approves each device the firmware announces.
- On USB4 hosts whose firmware has no connection manager (AMD, ASMedia's ASM4242 on AM5 X870 boards, Intel Lunar Lake and later, Barlow Ridge), the driver is the connection manager and builds the tunnels itself.

| Controller | PCI ids (`8086:` unless named) | In Device Manager |
|---|---|---|
| Falcon Ridge (Thunderbolt 2) | `156A`, `156C` | xHCI98 Thunderbolt 2 Controller |
| Alpine Ridge and Titan Ridge (Thunderbolt 3) | `15BF`, `1575`, `1577`, `15D2`, `15D9`, `15E8`, `15EB` | xHCI98 Thunderbolt 3 Controller |
| Ice Lake | `8A0D`, `8A17` | xHCI98 Thunderbolt 3 Controller |
| Maple Ridge, Tiger Lake to Meteor Lake | `1134`, `1137`, `9A1B`, `9A1D`, `9A1F`, `9A21`, `463E`, `466D`, `A73E`, `A76D`, `7EB2`, `7EC2`, `7EC3` | xHCI98 USB4 Host Router |
| Lunar Lake, Panther Lake, Wildcat Lake, Barlow Ridge | `A833`, `A834`, `E333`, `E334`, `E433`, `E434`, `4D33`, `5781`, `5784` | xHCI98 USB4 Host Router |
| AMD, ASMedia and any other USB4 host | `1022:1668`, `1022:1669`, PCI class `0C0340` | xHCI98 USB4 Host Router |

**Untested hosts.** The Thunderbolt 3 support was developed from captures of the Alpine Ridge JHL6240 (`8086:15BF`) in my ThinkPad P14s Gen 1. Every other controller in the table is driven from the Linux driver's description of it and is untested, and so is every USB4 host; each one is enabled all the same. If the driver starts and does nothing on your machine, the debug build's log says why, and `XhciCmMode` `0` turns it off. Reports from any of these machines, working or not, are very welcome.

Not supported: daisy chains (a device behind another device), DisplayPort over the cable, Apple machines and their controllers.

### Before you start: the BIOS

- Boot in legacy (CSM) mode, as for every system here.
- Set **Thunderbolt BIOS Assist** to off. With it on, the BIOS switches the controller off whenever nothing is attached at power-on, and the driver cannot switch it back on. On the ThinkPad P14s Gen 1, Assist on with pre-boot support off hides the controller altogether, and a USB4 enclosure then cannot be used at all.
- Set the Thunderbolt security level to **User Authorization** or **No Security**. Secure Connect, DisplayPort-only and USB-only are not supported.
- Pre-boot support (boot from Thunderbolt) may be on or off. When it is on, a device attached at power-on is connected by the BIOS and keeps working under the driver.

### Installing for Thunderbolt and USB4

Run `INSTALL.BAT` (see "Install with INSTALL.BAT") and answer Y to "Install Thunderbolt 3 / USB4 support?" (Enter answers Y when the controller is present; `INSTALL /thunderbolt` skips the question). It installs `xhcicm.sys` with the other drivers. On Windows 2000 to 7 it also places the package's filter on the controller's PCI ports (`nt\xhcicmf.inf` in each driver directory), which is what lets Windows see a device plugged in later; Device Manager alone cannot place it on XP and later. Restart when it asks: on Windows 2000 the first install of the filter needs that restart before the controller is used.

On Windows 98 SE and ME, `INSTALL.BAT` prints the remaining steps: update the controller's entry (an unknown PCI device) from the driver directory, as for the xHCI controller. These systems need no filter.

The controller shows in Device Manager under the names in the table above. On Windows 2000 to 7 its PCI ports show as "xHCI98 Thunderbolt PCI Bridge" or "xHCI98 USB4 PCI Bridge": they are still Windows' own PCI bridges, with the filter on them.

### Approving devices

By default (`XhciCmApprove` `1`) every device plugged into the port is approved at once. A device with a PCI Express tunnel can read and write all of the machine's memory, and these systems cannot fence it off. Set `XhciCmApprove` to `0` on a machine where people you do not trust can reach the port: then only devices the BIOS connected at power-on work. Nothing is remembered: a re-plugged device is approved again by the same rule.

### Removing a device safely

Close the files open on the device first, as for any removable disk.

`INSTALL /devices` shows the connected Thunderbolt and USB4 devices, on every system:

- each one's name, generation, approval and connections
- the Windows devices behind it, with their drivers

`INSTALL /eject` removes one of them safely:

1. It lists the devices and asks which one.
2. It asks Windows to stop it. If a program has a file open on the device, Windows refuses and nothing changes: close the program and try again.
3. When it says "You can now unplug the device.", unplug it.

On Windows 2000 and XP run it as an administrator; on Vista and 7 it asks for elevation. `tools\xhciinst\XHCIINST` takes the same switches (`XHCIIN64` on 64-bit Windows).

- **Windows 98 SE and ME:** use `INSTALL /eject`; these systems have no other way. Until you unplug the device, Device Manager's Refresh can start it again.
- **Windows 2000:** stop the device in "Unplug or Eject Hardware", then unplug it. Without the stop, Windows 2000 shows its "Unsafe Removal of Device" warning.
- **Windows XP, Vista and 7:** stop the device in "Safely Remove Hardware", then unplug it.
- **A device missing from those lists** (one connected before the driver started, or disabled and enabled since, or whose driver was updated): on XP, Vista and 7 `INSTALL /eject` still asks Windows to stop it. Windows 2000 cannot stop such a device: the tool says so; restart with the device connected and it is listed for removal again, or shut the computer down and unplug it then. Unplugging and plugging it in again also puts it back in the list.
- **A dock** is listed under a generic PCI bridge name, the dock's own switch; stopping it stops everything in the dock.
- **For scripts:** `INSTALL /eject 2` ejects device 2 of the list after one question; `INSTALL /eject id=<instance id> /yes`, with the instance id `/devices` prints, does it with no question.

### NVMe drives

An NVMe drive in an enclosure needs an NVMe driver, which these systems do not have. `nvme98`, a separate project, is that driver for Windows 98 SE to Vista. `xhcicm.sys` brings the drive's tunnel up and down and tells `nvme98` when the drive arrives, and `nvme98` runs the drive.

The package carries an unchanged nvme98 release in `release-x86\` and `release-x64\`. Its readme, beside it as `nvme98-readme.txt`, is its install guide and lists its known limits. The package's `readme.txt` and the release notes name the version.

`INSTALL.BAT` does not install it. When the drive's NVMe controller shows in Device Manager without a driver, use Update Driver -> Have Disk, pointed at:

- `release-x86\` on Windows 98 SE, ME and 32-bit 2000, XP, Vista and 7
- `release-x64\` on Windows XP x64, Vista x64 and 7 x64

The 64-bit nvme98 is test-signed by its own project, as `xhci98.sys` is by this one, and loads on Vista x64 and 7 x64 in test-signing mode; its readme has the details.

### Things to know

- Before standby or hibernation, stop and unplug every Thunderbolt or USB4 device. Standby with one attached, or with an Ice Lake or later controller, is not supported.
- On Windows 98 SE and ME, a device plugged in after the start needs the BIOS to give it an interrupt. On the ThinkPad P14s Gen 1 it does. If a device inside the enclosure stays at Code 29 on another machine, attach it at power-on with pre-boot support on.
- On Windows 98 SE and ME the Thunderbolt driver stays loaded until Windows restarts, so a disable, update or removal of it takes full effect after a restart.
- If the platform switches a USB4 controller off behind Windows' back, the driver stops using it; disable and enable the controller in Device Manager, or restart, to use it again.
- The settings (`XhciCmMode`, `XhciCmApprove`, `XhciCmSafeEject` and the rest) are in the controller's driver key, under the System class; the [release notes](docs/using/release-notes.md)' "The connection manager's registry values" lists them all, and `INSTALL.BAT` asks about the first three. It never asks about `XhciCmReenum`, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding` and `XhciCmPreOsCleanup`: leave them at their defaults unless a problem report calls for a change, which `INSTALL /set Name=Value` makes.

## Installation Steps

The prerequisites per OS:

- **Windows 98 SE**:
  - Install [NUSB 3.3 or 3.6](https://www.philscomputerlab.com/windows-98-usb-storage-driver.html) if you want USB storage, UAS included.
  - [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip) also works but has no storage driver. Unzip, right-click the `USB2.INF` at its root then install. With it alone, a USB stick shows Code 28 and a UAS drive Code 2, so storage still needs NUSB's storage files (below).
  - With neither, HID and audio devices work but storage has no driver.
  - Reboot if requested after installing either.
- **Windows ME**: Nothing to install; a stock ME works. [SweetLow's stack](http://sweetlow.orgfree.com/download/usb20_win9x.zip) also works if it is already there. ME has its own storage files. An ordinary USB stick needs to be plugged for the first time to copy the drivers before a UAS drive will be recognised.
- **Windows 2000 SP4, XP SP3 x86**: Nothing to install.
- **Windows XP SP2 x64, Vista SP2 and 7 SP1 (x86/x64)**: Nothing to install. On **Vista x64 and 7 x64** the driver is test-signed by me, so Windows has to be in test-signing mode, or driver signature enforcement disabled at every boot; see "Windows Vista x64 and 7 x64: test-signing mode" below.

On Windows 98 SE, storage of any kind, UAS included, needs NUSB's mass-storage component: `USBSTOR.INF`/`USBSTOR.SYS`, `USBNTMAP.INF`/`USBNTMAP.SYS` and `USBMPHLP.PDR`, which NUSB 3.3 and 3.6 install. SweetLow's stack alone has no storage half. 

On an xHCI-only Windows 98 SE or ME machine, **have the Windows installation CD at hand** or its contents on disk. Windows may ask for files from it while installing the driver or the USB devices plugged in afterwards.

Optional but recommended: boot real DOS (not a DOS box inside Windows) and run `XHCIQUAL` from the `TOOLS\XHCIQUAL\` folder. It says whether the driver can work on the machine before you install anything. A controller reporting no legacy interrupt pin is not refused: the driver polls it, or takes a message-signalled interrupt on Vista and 7, and `XHCIQUAL` reports it as `QUALIFIED WITH CAVEATS`.

<img src="images/xhci98-xhciqual-basic.jpg" width="800">

Basic read-only quick scan answers in one line. The `xhci` run below it exercises the controller (handoff, reset, DMA, interrupt, port resets) and ends with a verdict:

<img src="images/xhci98-xhciqual-full-start.jpg" width="800">

Running the full check.

<img src="images/xhci98-xhciqual-full-end.jpg" width="800">

Controller qualified verdict.

XHCIQUAL demo video: https://www.youtube.com/watch?v=Tv6blmBS6Do

To submit logs with a [bug report](https://github.com/yeokm1/xhci98/issues/new/choose), run `QUALREP.BAT` from real DOS in the `TOOLS\XHCIQUAL\` folder and attach the `.LOG` file it names. It runs `XHCIQUAL --probe-only --no-page --log-stamp`, which is read-only: it takes ownership of nothing and writes no PCI configuration register. The log is written in the current directory under a name made from the DOS date and time (`mDDhhmmS.LOG`), so a second run never overwrites the first.

If a maintainer asks for the full run as well, continue with `XHCIQUAL --no-page --log FULL.LOG`. This one **takes over the controller**, resets it and resets its ports. Use a PS/2 keyboard and do not write the log to a drive on the controller being tested.

If the machine runs Windows with the driver, the bug report asks for the driver's own report too: in `TOOLS\XHCISNAP\`, run `SNAPON.BAT`, restart Windows, make the problem happen, run `SNAPGET.BAT`, and attach the `.DEV` and `.TXT` files it names; `SNAPOFF.BAT` turns the log channel off again afterwards. On Vista and 7, right-click each one and choose Run as administrator.

### The package

| Directory | For | Contents |
|---|---|---|
| `release-x86\` | 32-bit Windows: 98 SE, ME, 2000, XP, Vista, 7 | the drivers, the helper and the extras below |
| `release-x64\` | 64-bit Windows: XP x64, Vista x64, 7 x64 | the same, without `xhcihlpr.sys`, plus `xhci98-test.cer` |
| `debug-x86\`, `debug-x64\` | troubleshooting only | the same drivers built for troubleshooting; install only if asked |
| `tools\xhciqual\` | DOS | `XHCIQUAL.EXE`, the qualifier, and `QUALREP.BAT`, the reading a bug report asks for |
| `tools\xhcisnap\` | Windows | `XHCISNAP.EXE`, the log snapshot reader, and `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT`, which turn its channel on, write the report a bug report asks for, and turn the channel off |
| `tools\portsw\` | Windows | `portsw.vbs`, `portsw-on.bat` and `portsw-off.bat`, the switch for the Intel port switchover |
| `tools\silent\` | Windows 98 SE and ME | `SILENT.REG`, `SILENT.VBS`, `silent-on.bat` and `silent-off.bat`, the silent install's list and its switch (see "Tuning") |
| `tools\xhcicln\` | Windows | `XHCICLN.EXE` (32-bit) and `XHCICL64.EXE` (64-bit), an optional tool that lists the old `XHCI98\` entries an update from `2.3.x` or earlier leaves behind and, on Windows 2000 to 7, removes them; nothing needs it |
| `INSTALL.BAT` | Windows | installs, updates or uninstalls the drivers; see "Install with INSTALL.BAT" |
| `tools\xhciinst\` | Windows | `XHCIINST.EXE` (32-bit) and `XHCIIN64.EXE` (64-bit), the program `INSTALL.BAT` runs |
| `readme.txt` | | the release notes in plain text |

In each driver directory:

- `xhci98.sys` and `xhci98.inf`: the controller driver.
- `xhciuas.sys` and `xhciuas.inf`: the UAS storage driver.
- `xhcicm.sys` and `xhcicm.inf`: the Thunderbolt and USB4 driver, and in `nt\` the filter for the controller's PCI ports on Windows 2000 to 7 (`xhcicmf.inf`, with its own copy of `xhcicm.sys`).
- `xhcihlpr.sys` (32-bit only): the helper. 32-bit Windows 2000, XP, Vista and 7 use this small second driver for a finer system timer while the controller is polled (see [Polling and message-signalled interrupts](docs/using/tuning.md#polling-and-message-signalled-interrupts-xhciinterruptmode-xhcipollidlems-xhcipollactivems-xhcipollfinetimer)), and 32-bit Vista and 7 also to give the controller a message-signalled interrupt. The INF installs it on those four systems, as a service that starts at boot; Vista and 7 ask for a restart for it, 2000 and XP do not, and the finer timer, when `XhciPollFineTimer` is set to `1` (it is off by default), starts there at the next boot. Windows 98 SE and ME ignore it. Releases `2.3.0.0` and `2.3.1.0` carried the MSI half alone, as `xhcimsi.sys`, for Vista and 7 only.
- `txtsetup.oem`: for F6 during Windows 2000 or XP Setup.
- `xhci98-test.cer` (64-bit only): the public half of the test certificate the x64 drivers are signed with. You do not need to import it.
- `nvme98` files (`release-x86\` and `release-x64\` only): the bundled NVMe driver, a separate project; see "NVMe drives".

`release-x86\` or `release-x64\` is the one to install.

### Install with INSTALL.BAT

Unzip the whole package and run `INSTALL.BAT` from its top directory: double-click it on 98 SE and ME, run it as an administrator on 2000 and XP, and with Run as administrator on Vista and 7. It shows its plan, asks about a few options (Enter keeps a value), then asks once before it changes anything.

- **Windows 2000 to 7:** it installs or updates every driver on the devices present, controller first, and stages the package for devices plugged in later. It handles Have Disk on 2000, the root hub's update and Vista and 7's same `DriverVer`. Answer Windows' unsigned-driver questions.
- **Windows 98 SE and ME:** it copies the package under `XHCI98` in the Windows directory, adds that folder to the wizards' search path and prints the Device Manager steps left. Run it again after the restart; it then also offers the silent install (off by default; see "Tuning").

When a run changed anything and finished, it ends with "Changes were made. Please restart Windows now." If it stopped with steps left for you (a Found New Hardware window to answer, for instance) it says so instead; do them and run it again. It never runs `XHCICLN`.

It also asks "Install Thunderbolt 3 / USB4 support?": Enter answers Y when a Thunderbolt or USB4 controller is present and N when none is found. Some laptops show their controller only while a device is plugged in, or only with a BIOS setting, so answer Y if yours has the port; `INSTALL /thunderbolt` includes it without asking. N leaves a Thunderbolt driver already installed as it is.

`INSTALL /list` shows the plan and changes nothing. `INSTALL /uninstall` removes the drivers after a warning and the word REMOVE. Each run but `/list` writes `XHCIINST.LOG` at the root of the system drive; attach it to a bug report. `tools\xhciinst\readme.txt` lists the switches and exit codes.

### Install

To install by hand:

1. Put the unzipped package somewhere the machine can read: a floppy, a CD, a shared folder.
2. Install the prerequisites above. On Windows 98 SE the order for a fresh install is NUSB, then xhci98, then the UAS disk.
3. In Device Manager, find the unrecognised xHCI controller. It sits unclaimed with a yellow mark, usually under "Other devices" such as "Universal Serial Bus Controller".
4. Properties -> Driver -> Update Driver -> Specify a location/Have Disk -> the `release-x86\` or `release-x64\` directory.
5. It installs as "xHCI98 USB 3.x eXtensible Host Controller" with "xHCI98 USB 3.x Root Hub" underneath it. Windows 98 SE may ask for its CD for `usbd.sys`.
6. Reboot if requested.
7. Plug in a UAS disk, and when the wizard asks for "xHCI98 USB Attached SCSI Storage", point it at the same directory. No restart is needed for `xhciuas.sys`.
8. An external hub installs as "xHCI98 USB Hub" from the driver already installed. On Windows 98 SE and 2000 it installs with nothing to answer. On Windows XP every newly plugged hub brings the Found New Hardware wizard and the unsigned-driver warning (Continue Anyway).

### Windows Vista x64 and 7 x64: test-signing mode

The 64-bit `xhci98.sys`, `xhciuas.sys` and `xhcicm.sys` are test-signed, so on Windows Vista x64 and Windows 7 x64 the driver can run in test-signing mode instead of disabling driver signature enforcement with F8 at every boot. Once:

1. Open an elevated Command Prompt: Start, type `cmd`, right-click it, Run as administrator.
2. `bcdedit /set testsigning on` - a boot setting of the current Windows entry, so it stays on for every boot until it is turned off; no F8 is needed.
3. Restart. "Test Mode" shows on the desktop while it is on: at the bottom right on 7, with the version and build, and in the corners on Vista.
4. Install the driver as above. Windows still says it cannot verify the publisher; choose "Install this driver software anyway". Afterwards the Driver tab says "Not digitally signed". Both are expected: the driver is test-signed, not signed by Microsoft.

Check: `bcdedit /enum {current}` shows `testsigning Yes`. Turn it off: `bcdedit /set testsigning off` and restart; the driver then needs F8 again, or it does not load and the controller shows Code 39 on Vista, Code 52 on 7.

- With BitLocker on the system drive, suspend BitLocker before changing the boot setting, or the next boot asks for the recovery key.
- This is not Microsoft signing: the certificate is the project's own test certificate, which no Windows trusts by itself.
- F8 -> "Disable Driver Signature Enforcement" still works as before, for one boot, without test mode.

### Installing Windows 2000 or XP, or using the Recovery Console

This is for a machine whose keyboard or install medium is on the xHCI controller.

1. Copy the files of `release-x86\` (`release-x64\` for XP x64) to the root of a floppy.
2. **Windows 2000 only:** from the `I386` folder of the Windows 2000 CD you install from, copy `USBD.SY_`, `HIDCLASS.SY_` and `HIDPARSE.SY_` to the root of the same floppy, unchanged (do not expand or rename them). The package does not carry them.
3. Press F6 when text-mode Setup offers it.
4. Press S and pick "xHCI98 USB 3.x Host Controller (32-bit Windows 2000/XP)" (the x64 entry on XP x64), or on Windows 2000 the second entry, "xHCI98 USB 3.x Host Controller (32-bit Windows 2000, files from CD)".

On Windows 2000, if a USB keyboard, mouse or stick is used during Setup, the first entry leads to `STOP c000026c` at the new system's first start; the second entry without the three files on the floppy stops Setup while it copies files, asking for them in drive A: (F3 quits). The [release notes](docs/using/release-notes.md) have the details.

The same floppy serves the Recovery Console of Windows 2000 and XP. Press F6 as for an install, then R at Setup's Welcome screen (on Windows 2000, then C for the console). The USB keyboard logs in and types commands.

Things to know:

- Pressing F6 needs the BIOS's own USB keyboard support.
- A disk that offers only UAS is not usable until GUI-mode Setup; a plain USB flash stick is, and so is a UAS disk that also offers Bulk-Only, as most do.
- A USB drive present at the partition screen takes `C:`. Unplug the USB drives you do not need, or Windows installs to the next letter.
- On Windows 2000, plug the USB keyboard and stick in before Setup starts.
- To install Windows XP and XP x64, have a PS/2 keyboard or a laptop's built-in keyboard at hand. Later in Setup the USB keyboard does not work until a step that first asks about this unsigned driver (see "Known limitations/issues"). The Recovery Console runs in text mode throughout and is not affected.

The [release notes](docs/using/release-notes.md) have the limits.

<img src="images/xhci98-driver-info.jpg" width="800">

The controller in Device Manager on the P14s Gen 1 under Windows 98 SE.

### Upgrading the driver
Run `INSTALL.BAT` to install, update or uninstall (see "Install with INSTALL.BAT"). By hand:

Run Update Driver on "xHCI98 USB 3.x eXtensible Host Controller", then on "xHCI98 USB 3.x Root Hub", pointed at `release-x86\` or `release-x64\`.

- **Windows 98 SE and ME:** restart afterwards, although Windows does not ask. The old driver runs until then.
- **Windows 2000:** use Have Disk (Update Driver -> "Display a list of the known drivers" -> Have Disk -> `release-x86\`) and pick the first model listed. Letting Windows search keeps the old driver.
- **XP, Vista and 7:** the new driver runs at once, after the unsigned-driver warning. 32-bit Vista and 7 ask for a restart for the helper `xhcihlpr.sys`. 64-bit Vista and 7 need test-signing mode, as for the first install.
- **Vista and 7:** a build with the same `DriverVer` as the installed one is not taken. Use a newer build.
- A device that Windows finds again as new hardware loses its hidusbf setting. Set it again.

**From `2.3.x` or earlier:** update the controller only. The root hub, hubs and composite devices are found again as new hardware and install by themselves. On 98 SE, XP and XP x64 keep a PS/2 or built-in keyboard and mouse at hand: the new-hardware questions come before the USB ones work.

The release notes' "From 2.3.x or earlier" has the details. `tools\xhcicln\` lists the old entries this leaves behind and, on 2000 to 7, removes them; nothing needs it.

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one installed, "Search for a better driver" keeps the cached INF.

Instead, Update Driver on "xHCI98 USB Attached SCSI Storage", choose "Display a list of all the drivers in a specific location", then Have Disk -> the package directory.

## Tuning

Each value is a `DWORD` in the controller's driver (software) key:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000, XP, Vista, 7 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is "xHCI98 USB 3.x eXtensible Host Controller". The number varies from machine to machine.

The settings most often changed:

| Value | What it sets | Minimum | Maximum | Default when absent | Written by the install |
|---|---|---|---|---|---|
| `XhciForceBulkOnly` | Bulk-Only instead of UAS (see [Forcing Bulk-Only instead of UAS](docs/using/tuning.md#forcing-bulk-only-instead-of-uas-xhciforcebulkonly)) | `0` | `1` | `1` | `0` |
| `XhciInterruptMode` | How the controller's events arrive: `0` automatic, `1` the interrupt line only, `2` polling, `3` a message-signalled interrupt only (see [Polling and message-signalled interrupts](docs/using/tuning.md#polling-and-message-signalled-interrupts-xhciinterruptmode-xhcipollidlems-xhcipollactivems-xhcipollfinetimer)) | `0` | `3` | `0` | `0` |
| `XhciIntelPortSwitch` | The Intel 7/8/9-series port switchover; `1` turns it on, `2` applies it to any Intel controller **at your own risk** (`portsw.vbs` sets `0` or `1`; see [The Intel 7/8/9-series port switchover](docs/using/tuning.md#the-intel-789-series-port-switchover-xhciintelportswitch)) | `0` | `2` | `0` | `0` |
| `XhciSilentInstall` | Windows 98 SE and ME: `1` installs a device whose class is in the `SilentClasses` list with no wizard (`silent-on.bat` and `silent-off.bat` in `tools\silent\`; see [The silent install](docs/using/tuning.md#the-silent-install-xhcisilentinstall)) | `0` | `1` | `0` | `0` |
| `XhciLogVerbosity` | The driver's log, read by `XHCISNAP` (the [release notes](docs/using/release-notes.md), "The log, and how to send one") | `0` | `4` | `0` | `0` |

Restart Windows after changing a value. The install puts every value in the key, and an update keeps a value you changed.

[Tuning](docs/using/tuning.md) explains each of these in full, and has every other setting:

- the interrupt moderation interval (`XhciImodInterval250ns`)
- a Low- or Full-Speed mouse polled above 1000 Hz (`XhciFastPollFsLs`)
- how long the first report waits for a hub (`XhciFirstEnumWaitMs`, `XhciFirstEnumPortMs`)
- the polling periods and a finer system clock (`XhciPollIdleMs`, `XhciPollActiveMs`, `XhciPollFineTimer`)
- the largest UAS command and two transfer pieces at once (`XhciMaxTransferKB`, `XhciWideMap`)
- selective suspend (`XhciSelectiveSuspend`)
- the handling of controller faults (`XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`)
- the fixes for particular controllers: the Intel 7/8/9-series interrupt routing, the VIA built-in hub, controllers below xHCI 1.0 (`XhciIntelIrqRoute`, `XhciUsb2HubSpeedFallback`, `XhciAccept096`)
- the fourteen controller quirks taken from Linux (`XhciMsiAvoid` and the rest)
- the log sent to DebugView (`XhciLogDebugView`)

## What is tested, and what is not

Windows 98 SE and Windows 2000 SP4 are the primary targets. Real hardware: Windows 98 SE on the ThinkPad E460 and the ThinkPad P14s Gen 1, and Windows XP x64 and 7 x64 on the Dell Latitude E7250. Every other target, Windows 2000 included, has run in QEMU virtual machines only. The [release notes](docs/using/release-notes.md)' "Targets and their standing" has the full detail per system.

| Target | State |
|---|---|
| Windows 98 SE | VMs under NUSB and SweetLow's stack: HID, storage, composite audio, the ASIX Ethernet adapter, hubs to five tiers, plug soaks, SuperSpeed storage and UAS. A stock install runs HID and audio, with no storage driver. Real hardware: the E460 and the P14s Gen 1. |
| Windows 2000 SP4 | VMs only, under Driver Verifier and on a multiprocessor guest: the same as Windows 98 SE, including UAS and forced Bulk-Only. SuperSpeed hubs untested. |
| Windows ME | VMs only, under SweetLow's stack: HID, storage, hubs, composite audio, soaks, SuperSpeed storage and UAS. A stock install with no USB 2.0 stack: HID, a hub, composite audio, storage and UAS, disable, enable, remove and rescan. |
| Windows XP, 32-bit and x64 | VMs: HID, storage, hubs, composite audio, disable and enable, soaks, SuperSpeed storage and UAS. Real hardware: XP x64 on the E7250. |
| Windows Vista SP2 and 7 SP1, 32-bit and x64 | VMs: the same, on four virtual processors. On the x64 systems, in test-signing mode or with driver signature enforcement disabled (F8). Real hardware: 7 x64 on the E7250. |

| Machine | Controller | Tested by |
|---|---|---|
| 2012 Lenovo B490 | Intel Ivy Bridge, Panther Point HM77 (7-series) PCH, with EHCI beside it. xHCI 1.0. | Me |
| 2015 Dell Latitude E7250 | Intel Broadwell-U, Wildcat Point-LP (9-series) PCH, with EHCI beside it. xHCI 1.0. | Me |
| 2016 ThinkPad E460 | Intel Skylake, Sunrise Point-LP (100-series) PCH. xHCI 1.0. | Me |
| 2018 HP EliteBook 850 G5 | Intel Sunrise Point-LP (`8086:9D2F`, the E460's controller), xHCI 1.0. | Community |
| 2020 ThinkPad P14s Gen 1 | Intel Comet Lake PCH-LP (400-series). xHCI 1.1. | Me |
| Omores' Intel and AMD desktops | H110, B360, B550, X570, X670 | [Omores](https://www.reddit.com/r/windows98/comments/1whzyoa/xhci98_windows_98_gets_usb_3x_controller_support/) (1.x.x.x) |

The devices, each characterised in [test-equipment.md](docs/contributing/test-equipment.md):

| Device | VID:PID | Speed |
|---|---|---|
| Terminus 7-port hub, multi-TT | `1A40:0201` | High |
| Terminus 4-port hub, single-TT | `1A40:0101` | High |
| Genesys 7-port hub (two cascaded chips), single-TT | `05E3:0608` | High |
| Genesys USB 3.0 hub | `05E3:0610`, `05E3:0612` | SuperSpeed and High |
| A Full-Speed hub behind a High-Speed hub | | Full |
| Logitech USB Optical Mouse | `046D:C077` | Low |
| Microsoft Wired Keyboard 600 (composite) | `045E:0750` | Low |
| SanDisk U3 Titanium flash drive | `0781:5408` | High |
| MSSU10-128GSR flash drive | `090C:2320` | SuperSpeed, UAS |
| SanDisk 3.2Gen1 flash drive | `0781:55AB` | SuperSpeed, Bulk-Only |
| StoreJet Transcend USB-to-SATA bridge (ASMedia) | `174C:5106` | SuperSpeed, UAS and Bulk-Only |
| ASIX AX88772A USB Ethernet | `0B95:7720` | High |
| Sound Blaster Play! 2 (UAC 1.0 composite) | `041E:323D` | Full |
| C-Media USB Audio Device (UAC 1.0 composite) | `0D8C:0014` | Full |

## Known limitations/issues

### Outside this driver's control

These come from Windows, NUSB or the driver being unsigned, and no change to this driver can remove them.

| Limitation | Detail |
|---|---|
| Vista x64 and 7 x64 need test-signing mode, or driver signature enforcement disabled | The x64 drivers carry the project's own test signature, not Microsoft's. Turn on test-signing mode once (see "Windows Vista x64 and 7 x64: test-signing mode"), or disable driver signature enforcement at every start (F8); otherwise the controller sits at Code 39 on Vista, Code 52 on 7. |
| No USB storage on a stock Windows 98 SE | With no NUSB installed there is no mass-storage driver at all. HID and audio still work. |
| Windows XP installed with the F6 floppy needs a PS/2 or built-in laptop keyboard | Setup copies Windows' own HID and USB helper files only with Microsoft's own USB controller drivers, so in GUI-mode Setup the USB keyboard and mouse wait for its device install, which first asks about the unsigned driver (default No). A USB-only keyboard cannot answer it; a PS/2 or built-in laptop keyboard can. Windows 2000 does not ask. |
| Windows ME: unplugging a device while Windows installs it | ME's own device manager stops responding if a device is unplugged while Windows is still installing its driver. ME does the same on Microsoft's own USB stack. Wait for the install to finish before unplugging. |
| Windows ME: Shut Down with an unopened USB disk | Shut Down can hang if a USB disk is attached but has not been opened since Windows started. ME does the same on Microsoft's own USB stack. Open the disk once (a `dir` of it is enough), or remove it, before shutting down. |

### Not planned, though a later release might address them

| Limitation | How likely | Detail |
|---|---|---|
| Windows 2000: a mounted USB drive comes back at Code 31 after a controller recovery or hibernation | Possible after hibernating with a USB drive mounted; otherwise rare | Other devices come back. Unplug and replug the drive, or disable and enable it. |
| Windows ME: a UAS drive as the first USB storage device shows Code 2 | Rare: once per installation, only if a UAS drive is plugged in before any ordinary stick | ME has not yet copied its own USB storage files. Plug in any ordinary USB stick once, then replug the UAS drive. |
| Disabling a controller the driver closed off hangs | Very rare: the controller must fail in a way the driver cannot recover from | The driver keeps every transfer in flight so the controller cannot write into freed memory, and the disable waits on them. Restart instead. |
| Intel 7-series (Panther Point, `8086:1E31`): too many periodic devices may lose data instead of being refused | Not measured: it needs more audio, mouse and other periodic traffic at once than the controller can serve | Linux keeps its own bandwidth accounting on this controller (`XHCI_SW_BW_CHECKING`) because the controller accepts schedules it cannot serve. This driver relies on the controller's own admission, so an over-subscribed schedule there may lose data rather than be refused. |

### Untested ground

| Area | State |
|---|---|
| SuperSpeed isochronous transfers | Built from the specification. No SuperSpeed isochronous device has been held and QEMU models none. |
| SuperSpeedPlus (USB 3.1 Gen 2, USB 3.2 Gen 1x2 and Gen 2x2) | Accepted at its trained rate, built from the specification. Not read on any hardware: no Gen 2 device has been tested, so every mode is untested. |
| A UAS-only drive at SuperSpeed on a controller without streams | It is sent back to its USB 2.0 port and runs UAS at High Speed, or is refused if it has no USB 2.0 port. Built from the specification; no such controller has been held. |
| Polling above 1000 Hz (`XhciFastPollFsLs`) | Outside the xHCI specification. Not read on any real controller or in any virtual machine. |
| The Intel port switchover beyond the B490 and the E7250 | Device ids `8C31`, `9C31`, `8CB1` and `8D31`, systems other than Windows 98 SE and 7 x64. |
| Controllers below xHCI 1.0 (0.96: NEC/Renesas uPD720200 and uPD720200A, ASMedia ASM1042 and others) | Accepted by default (`XhciAccept096`) and driven as xHCI 1.0 controllers but for the fields Linux writes differently on them; a resume from standby re-enumerates their devices. Built against host tests and Linux's handling of the same parts; read on no 0.96 controller, and isochronous transfers (audio, video) on them are the least certain. Set `XhciAccept096` to `0` to refuse such a controller as `2.3.1.0` did. |
| Linux's controller quirks (`XhciMsiAvoid` and the others) | Built from Linux's handling and the xHCI specification, against host tests. None has been read on a controller it lists; QEMU's controller is on no list. |
| Thunderbolt 3 and USB4 (`xhcicm.sys`) | Built from captures of the ThinkPad P14s Gen 1's Alpine Ridge controller and of a USB4 NVMe enclosure, and from Linux's driver, and run in an emulator of them. Not yet run on a real Thunderbolt or USB4 machine; every controller but `8086:15BF` has no capture at all. |

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

4. Build the two tools that ship beside the drivers:
   - `xhciqual\build.cmd` produces `XHCIQUAL.EXE`, the DOS qualifier. It needs [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2/releases) at `C:\WATCOM` (or wherever `WATCOM` points), the only tool installed normally on the host; the drivers never use it.
   - `xhcisnap\build.cmd` produces `XHCISNAP.EXE`, the snapshot reader, with the in-repo MSVC 6.0.
   - `xhcicln\build.cmd` produces `XHCICLN.EXE` with the in-repo MSVC 6.0 and `XHCICL64.EXE` with the in-repo WDK 7.1 (`build.cmd x86` for the first alone), the cleanup tool.
   - `xhciinst\build.cmd` produces `XHCIINST.EXE` and `XHCIIN64.EXE` the same way, the install tool `INSTALL.BAT` runs.

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
  cm/           The Thunderbolt and USB4 driver xhcicm.sys, its four INFs and
                build files
test/           Host-side unit tests for the DDK-free core (test\run-host-tests.cmd)
scripts/        Setup helpers, build wrapper, the import/INF/packaging gates,
                and vm-matrix/ - the automated device matrix
xhciqual/       The DOS hardware-qualification tool (Open Watcom) and its
                bare-metal run logs under results/
xhcisnap/       The snapshot reader for the driver's log channel
xhcicln/        The optional cleanup tool for the old devnodes under XHCI98\
xhciinst/       The install, update and uninstall tool and INSTALL.BAT
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
.github/        Issue forms - the bug report
AGENTS.md       Guide for AI agents working on this project
```

## Documentation

Everything is under [docs/](docs/), indexed by [docs/README.md](docs/README.md).

Using the driver:

- [Release notes](docs/using/release-notes.md) - what the drivers do, do not, and do not yet claim, per target
- [Tuning](docs/using/tuning.md) - every registry setting, its range, default and effect
- [Release acceptance test](docs/using/release-acceptance-test.md) - the fixed procedure for checking a release on a new machine
- [Release history](releases/history.md) - what each version changed

Working on the driver:

- [Build and test](docs/contributing/build-and-test.md) - toolchain setup, builds, VMs, install, debugging, packaging, recovery
- [Roadmap for 2.0.0.0 onward](docs/contributing/roadmap-hcd.md) - the host controller driver, Phases 25 onward, UAS in Phase 31; the 1.x miniport's [Phases 0-16](docs/contributing/roadmap.md) and [17-24](docs/contributing/roadmap-miniport-updates.md) are closed records
- [Roadmap for 3.0.0.0 onward](docs/contributing/roadmap-usb4.md) - Thunderbolt 3 and USB4, Phases 39 onward; design records [21](docs/contributing/design/21-thunderbolt3-icm-client.md) (the Thunderbolt firmware connection manager's client) and [22](docs/contributing/design/22-usb4-software-connection-manager.md) (the USB4 connection manager)
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

The repository tracks no third-party binary on its own, although the tool executables it tracks under `releases/` carry statically linked third-party runtimes.

* `xhci98.sys` and `xhciuas.sys` link no runtime or extender. The 32-bit builds link no third-party object either; the 64-bit builds, linked by WDK 7.1, carry that kit's `/GS` stack-cookie handler (`__security_check_cookie` / `__report_gsfailure`), which nothing in this project's source calls.

* Some files of `xhcicm.sys`'s source, under `src/cm/`, are adapted from the `usb4-emu` project (some lifted from its `tools/usb4cm` under its earlier MIT licence) and from Linux's `drivers/thunderbolt`, and keep their copyright lines and licence notices. The [legal and provenance record](docs/contributing/legal-provenance.md), section 2a, lists each file.

* `XHCIQUAL.EXE` embeds the Open Watcom runtime and the DOS/32A extender.

* `XHCISNAP.EXE` embeds the MSVC 6.0 runtime.

* `XHCICLN.EXE` embeds the MSVC 6.0 runtime, and `XHCICL64.EXE` the WDK 7.1 one.
* `XHCIINST.EXE` embeds the MSVC 6.0 runtime, and `XHCIIN64.EXE` the WDK 7.1 one. Neither contains or runs a Microsoft setup program such as devcon: they call the setup routines of the Windows they run on.

Each tool ships with a `NOTICE.TXT` recording it, and the `LICENSE` scope note says that those runtimes carry their own terms and are outside the grant. Neither quotes the terms themselves.

The release download's `release-x86\` and `release-x64\` carry nvme98, a released build of a separate BSD-3-Clause project, unchanged; its `LICENSE` and `NOTICE.TXT` ship beside it as `nvme98-LICENSE.txt` and `nvme98-NOTICE.txt`. The repository tracks none of its files.

The release download carries no Microsoft file. `usbd.sys` and `usbui.dll` are the OS's own, copied by Windows Setup from its own install source. On Windows 98, `xhciuas.inf` names NUSB's `USBNTMAP.SYS` as a filter on the UAS device; the file is named, never shipped, and NUSB's mass-storage install places it.

The full inventory, provenance methods and redistribution boundaries are in [docs/contributing/legal-provenance.md](docs/contributing/legal-provenance.md), which states facts, not legal conclusions.
