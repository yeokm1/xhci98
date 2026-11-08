# xhci98 - Release Notes

This file describes package version `3.4.2.0`, the twenty-first release and
the fourteenth of the host controller driver, dated 2026-11-08 (`DriverVer`
`11/08/2026,3.4.2.0` in every INF of the package: those of `xhci98.sys`,
the UAS driver and the connection manager, and Stor98's). What it changes from `3.4.1.0` is "What `3.4.2.0`
changes", below, and is marked "new in `3.4.2.0`" or "from `3.4.2.0`" where
it applies below. What `3.4.1.0` changed from `3.4.0.0` is "What `3.4.1.0`
changes", below, and is marked "new in `3.4.1.0`" or "from `3.4.1.0`" where
it applies. What `3.4.0.0`
changed from `3.3.0.0` is "What `3.4.0.0` changes", below, and is marked
"new in `3.4.0.0`" or "from `3.4.0.0`" where it applies. What `3.3.0.0`
changed from `3.2.0.0` is "What `3.3.0.0` changes", below, and is marked
"new in `3.3.0.0`" or "from `3.3.0.0`" where it applies. What `3.2.0.0`
added to `3.1.0.0` is its entry in `releases/history.md` and is marked "new
in `3.2.0.0`" or "from `3.2.0.0`" where it applies; what `3.1.0.0` changed
from `3.0.0.0` is "What `3.1.0.0` changes", below. What `3.0.0.0` added to
`2.4.0.0` is "What `3.0.0.0` adds"; what `2.4.0.0` added to `2.3.1.0` is
"What `2.4.0.0` adds" after it, what `2.3.1.0` changed from `2.3.0.0` is
"What `2.3.1.0` changed" after that, and what `2.3.0.0` changed from
`2.2.0.0` is "What `2.3.0.0` changed"; what `2.2.0.0` changed from
`2.1.1.0`, `2.1.1.0` from `2.1.0.0`, and `2.1.0.0` from `2.0.0.0`, are their
entries in `releases/history.md`. Where this file and
`docs/contributing/roadmap-hcd.md`, `docs/contributing/build-and-test.md` or
`xhciqual/README.md` disagree, the other document wins and this one is the
copy to fix.

The notes for `1.2.0.0`, the last release of the USB 2.0-only miniport, stay
as they were published: on the `1.2.0.0` branch,
[docs/using/release-notes.md](https://github.com/yeokm1/xhci98/blob/1.2.0.0/docs/using/release-notes.md),
and in plain text in that release's own
[readme.txt](../../releases/1.2.0.0/readme.txt). Read them if you run
`1.2.0.0`; nothing below describes it except where it says so.

---

## What this is

`xhci98.sys` is a USB host controller driver for xHCI (USB 3.x) controllers,
for Windows 98 SE, Windows ME, Windows 2000 SP4, 32-bit Windows XP, Windows
XP x64, and Windows Vista and Windows 7 in both architectures, on machines
whose only USB controller is xHCI. On Windows 8, 8.1 and 10, in both
architectures, the package's installer installs the Thunderbolt 3 / USB4
connection manager by default, Windows' own USB 3 driver keeping the USB
controllers; the USB drivers are an opt-in install there, and the bundled
Stor98 is ticked with the Thunderbolt 3 / USB4 support on 8 and 8.1 and
unticked on 10, where Windows' own `stornvme` keeps a tunnelled drive.
Secure Boot must be off on those systems, and their x64 editions load the
drivers in test-signing mode (below).

Since `2.0.0.0` it is a whole USB host stack below the class drivers rather
than a miniport. It owns the controller, creates the root hub, drives every
hub behind it and splits composite devices into their functions itself, with
no `usbport.sys`, `usbhub.sys`, `usbhub20.sys` or `usbccgp.sys` involved.
Windows' own class drivers sit on top of it unchanged: HID, `usbstor.sys`,
USB audio, and vendor drivers such as ASIX's Ethernet one. Because nothing of
the old USB 2.0-era stack is left underneath it, it is no longer limited to
USB 2.0:

- SuperSpeed (USB 3.x, 5 Gbit/s) devices on the root ports;
- SuperSpeed hubs, whose USB 2.0 half is driven as a USB 2.0 hub;
- High-, Full- and Low-Speed devices and USB 2.0 and 1.1 hubs, every device
  reported to Windows at its true speed;
- UAS (USB Attached SCSI) storage, through a second driver in the same
  package, `xhciuas.sys`, since none of these systems up to Windows 7 has a
  UAS driver of its own (Windows 8 to 10 have `uaspstor.sys`).

A link that trains above 5 Gbit/s (SuperSpeedPlus: USB 3.1 Gen 2, USB 3.2
Gen 1x2 and Gen 2x2) is accepted at its trained rate.

In Device Manager the controller is "xHCI98 USB 3 Controller", with
"xHCI98 USB 3 Root Hub" beneath it and the USB devices beneath that
("xHCI98 USB 3.x eXtensible Host Controller" up to `3.1.0.0`, and "xHCI98
USB 3.x Root Hub" up to `3.2.0.0`). Since `2.1.0.0` an external hub has an entry of its own,
"xHCI98 USB Hub", with the devices behind it beneath it, and a device that
none of Windows' own INFs names is listed under its own product name; see "Devices and hubs in Device
Manager". A UAS device appears as "xHCI98 USB Attached SCSI Storage".

The package carries two architectures. The 32-bit drivers serve Windows 98
SE, ME, 2000, 32-bit XP and 32-bit Vista to 10; the 64-bit ones are a
separate build from a separate toolchain, in their own directories with their
own INFs, for Windows XP x64 and Vista x64 to 10 x64. Picking the wrong
directory is harmless: Windows finds no driver in it and says so.

**From `2.3.0.0` the 64-bit `xhci98.sys` and `xhciuas.sys` are
test-signed, and from `3.0.0.0` `xhcicm.sys` too, so on Windows Vista x64 and Windows 7 x64 the driver can run
in test-signing mode instead of disabling driver signature enforcement with
F8 at every boot.** Those systems refuse a kernel-mode driver without a
signature they accept; up to `2.2.0.0` the drivers were not signed, and
enforcement had to be disabled at every start. Once:

1. Open an elevated Command Prompt: Start, type `cmd`, right-click it, Run
   as administrator.
2. `bcdedit /set testsigning on` - a boot setting of the current Windows
   entry, so it stays on for every boot until it is turned off; no F8 is
   needed.
3. Restart. "Test Mode" shows on the desktop while it is on: at the bottom
   right on 7, with the version and build, and in the corners on Vista.
4. Install the driver as usual. Windows still says it cannot verify the
   publisher; choose "Install this driver software anyway". Afterwards the
   Driver tab says "Not digitally signed". Both are expected: the driver is
   test-signed, not signed by Microsoft, and its catalog (from 3.2.0.0) is
   signed by the same test certificate, which Windows does not trust.

Check: `bcdedit /enum {current}` shows `testsigning Yes`. Turn it off:
`bcdedit /set testsigning off` and restart; the driver then needs F8 again,
or it does not load and the controller shows Code 39 on Vista, Code 52 on
7, with nothing plugged into it working inside Windows, a USB keyboard or mouse
included.

- **Keep test-signing mode on while the Thunderbolt 3 / USB4 support is
  installed** on 64-bit Windows with a Thunderbolt or USB4 controller,
  whether or not anything is plugged in: without it the connection manager
  does not load. Windows makes it a boot driver when it puts its filter on
  the controller's PCI ports, which exist with nothing attached. From
  `3.3.0.0` the installer sets it back to start on demand at the end of each
  run, and the safe-removal mark service does the same after an install the
  installer did not run, such as a port that appears later or Update Driver
  by hand (roadmap-usb4 tasks 44.32 and 44.12; design record 23 section
  5.14). A start with test-signing mode off then reaches the desktop with
  the install as it was, and the Thunderbolt controller, its PCI ports and
  the USB controllers this package runs show Code 52 (Code 39 on Vista);
  turn test-signing mode on again and restart, and they work. Under
  `3.2.0.0`, which left it a boot driver, such a start stopped 64-bit
  Windows 8 in Automatic Repair, and Windows 7 x64 started but silently
  rolled the whole xHCI98 install back; that is still what happens while it
  is a boot driver - after an install the installer did not run, with the
  mark service not running or before its next check - and the way back is
  one start with signature enforcement disabled (F8 on Vista and 7, Startup
  Settings on 8 to 10), test-signing mode turned on there, a restart, and
  the installer run again. Where the disk Windows starts from sits below a
  device the filter is on, it stays a boot driver, and the installer says
  so. On Vista x64 and 7 x64, as on 64-bit 8 to 10, the installer installs
  the Thunderbolt 3 / USB4 support only in test-signing mode. A Windows
  feature update, turning Secure Boot on, or a boot repair can turn
  test-signing mode off too.

- With BitLocker on the system drive, suspend BitLocker before changing the
  boot setting, or the next boot asks for the recovery key.
- This is not Microsoft signing: the certificate is the project's own test
  certificate, which no Windows trusts by itself.
- Importing the `xhci98-test.cer` in `nt-x64\` is not needed: test-signing
  mode works without it, and it does not remove the install prompt either.
  It is not recommended.
- F8 -> "Disable Driver Signature Enforcement" still works as before, for
  one boot, without test mode. After an install on such a start the Program
  Compatibility Assistant may say "Windows requires a digitally signed
  driver"; the driver still loads on a start with enforcement disabled.
- 32-bit Vista and 7, and XP x64, need neither: they do not refuse unsigned
  drivers.

## What this is not

- It is not a mass-storage driver for Windows 98 SE. Storage on Windows 98
  SE, Bulk-Only and UAS alike, needs NUSB's mass-storage files, which from
  `3.2.0.0` the download carries and the installer offers; see "Windows 98
  SE and ME: the stock install, and storage".
- It is not signed by Microsoft. The 32-bit drivers are not signed at all:
  Windows 98 SE and ME do not check; Windows 2000 and XP warn and install;
  32-bit Vista to 10 warn and install (8, 8.1 and 10 only with Secure Boot
  off). From `2.3.0.0` the 64-bit drivers carry the project's own test
  signature, which Vista x64 to 10 x64 accept only in test-signing mode
  (on 8, 8.1 and 10 with Secure Boot off), and otherwise load only while
  driver signature enforcement is disabled (above).
- It does not put idle devices to sleep unless you turn that on. Selective
  suspend is new in `2.3.0.0` and off by default; see
  "XhciSelectiveSuspend: putting idle devices to sleep".
- It does not write to your disk. The driver creates no file. Its log is read
  out of the running driver by the xHCI98 USB Tree when you ask for a report; see
  "The log, and how to send one".
- It is not the miniport. `1.2.0.0` is frozen and stays available; nothing
  of it is in this package. An install of this package replaces it, because both are
  called `xhci98.sys`.

## Benchmarking a disk

To benchmark a disk, use CrystalDiskMark or ATTO Disk Benchmark (suggestions, not part of this package). XHCIPERF stays a developer diagnostic in the source tree (`xhciperf\`), not shipped.

## Controller names in Device Manager (new in `3.2.0.0`)

Device Manager names each controller this package drives by its kind, then
its maker and product in parentheses, with no driver version (that is on
the Driver tab, and in the xHCI98 USB Tree):

- **USB 3 controllers:** `xHCI98 USB 3 Controller (Renesas uPD720202)`,
  `(ASMedia ASM3242)`, `(Intel 9 Series LP)` and so on, for the controllers
  of the table at the end of this section (design record 23 section 5.13 is
  the source of each name); any other hardware controller is `xHCI98 USB 3
  Controller`. The INF
  still matches every xHCI controller by its class and gives the plain name,
  so that Windows 8 to 10 still rank their own driver first; the product
  name is the device's FriendlyName, which `SETUP.EXE` and `INSTALL.BAT`
  write at every install and update and remove at the uninstall. A hand
  install from the INF shows the plain name. A name you gave the controller
  yourself is kept. Up to `3.1.0.0` the name was `xHCI98 USB 3.x eXtensible
  Host Controller`.
- **Thunderbolt and USB4 controllers:** `xHCI98 TB2 Controller (Intel
  Falcon Ridge)`, `xHCI98 TB3 Controller (Intel Titan Ridge)`, `xHCI98
  USB4 Router (Intel Meteor Lake)`, from the INF itself ("Which
  controllers", below); TB2 and TB3 are Thunderbolt 2 and Thunderbolt 3, and
  a USB4 Router is the USB4 host router. Any other USB4 host is `xHCI98
  USB4 Router`. Up to `3.1.0.0` they were `xHCI98 Thunderbolt 2
  Controller`, `xHCI98 Thunderbolt 3 Controller` and `xHCI98 USB4 Host
  Router`.
- **The xHCI98 USB Tree** shows the same names.

Every name is at most 46 characters: the longest that Windows 98 SE's Device
Manager showed whole at 640x480, in the list and in the Properties window's
title and General tab (the word "Properties" after a long name can still be
cut in the title). On an installed 98 SE the controller's list entry and
General tab show the name the installer wrote; up to `3.2.0.0` its
Properties title kept the plain "xHCI98 USB 3 Controller", 98 SE's title not
taking that name. From `3.3.0.0` the 32-bit INF's Windows 98 SE and ME
models section names each controller of the installer's table by its
hardware id, under its product name, so the title names it too (roadmap-usb4
task 44.5; design record 23 section 5.13.7): "xHCI98 USB 3 Controller
(Renesas uPD720202) Properties". A controller installed by an earlier
release takes it when the installer updates it. Windows 2000 and XP read the
same section, where such a controller now matches the INF by its hardware id
rather than its class; the installer still never installs over another
maker's driver.

From `3.3.0.0` the table names many more controllers: AMD's 400 to 800
Series chipsets and its processors from Zeppelin to Strix Halo, Intel's
200 to 800 Series chipsets and later processors, ASMedia's ASM4242 and
others (roadmap-usb4 task 44.55). The ASM4242's USB4 router is named
`xHCI98 USB4 Router (ASMedia ASM4242)`, from `3.4.0.0` by its own INF
line (by the installer in `3.3.0.0`), and on Windows 2000 to 7 its
downstream ports `xHCI98 USB4 PCI Bridge
(ASMedia ASM4242)`. The USB 3 controllers named:

| Controller | PCI ids | In Device Manager |
|---|---|---|
| AMD Navi 10 | `1002:7316` | xHCI98 USB 3 Controller (AMD Navi 10) |
| AMD Krackan | `1022:1118`, `1022:111C`, `1022:111E`, `1022:1128` | xHCI98 USB 3 Controller (AMD Krackan) |
| AMD Krackan2 USB 2 | `1022:1135` | xHCI98 USB 3 Controller (AMD Krackan2 USB 2) |
| AMD Krackan2 | `1022:1138`, `1022:1139`, `1022:113A` | xHCI98 USB 3 Controller (AMD Krackan2) |
| AMD Ariel | `1022:13ED`, `1022:13EE` | xHCI98 USB 3 Controller (AMD Ariel) |
| AMD Family 17h | `1022:145C` | xHCI98 USB 3 Controller (AMD Family 17h) |
| AMD Zeppelin | `1022:145F` | xHCI98 USB 3 Controller (AMD Zeppelin) |
| AMD Starship | `1022:148C` | xHCI98 USB 3 Controller (AMD Starship) |
| AMD Matisse | `1022:149C` | xHCI98 USB 3 Controller (AMD Matisse) |
| AMD Strix | `1022:151A`, `1022:151B`, `1022:151E`, `1022:151F` | xHCI98 USB 3 Controller (AMD Strix) |
| AMD Strix Halo | `1022:1587`, `1022:1588`, `1022:1589`, `1022:158B` | xHCI98 USB 3 Controller (AMD Strix Halo) |
| AMD Raphael | `1022:15B6`, `1022:15B7` | xHCI98 USB 3 Controller (AMD Raphael) |
| AMD Raphael USB 2 | `1022:15B8` | xHCI98 USB 3 Controller (AMD Raphael USB 2) |
| AMD Phoenix2 | `1022:15BB`, `1022:15C2`, `1022:15C3` | xHCI98 USB 3 Controller (AMD Phoenix2) |
| AMD FireFlight | `1022:15D4`, `1022:15D5` | xHCI98 USB 3 Controller (AMD FireFlight) |
| AMD Rembrandt | `1022:15D6`, `1022:15D7`, `1022:161A`, `1022:161B`, `1022:161C`, `1022:161D`, `1022:161E`, `1022:161F` | xHCI98 USB 3 Controller (AMD Rembrandt) |
| AMD Raven | `1022:15E0`, `1022:15E1` | xHCI98 USB 3 Controller (AMD Raven) |
| AMD Raven2 | `1022:15E5` | xHCI98 USB 3 Controller (AMD Raven2) |
| AMD Renoir/Cezanne | `1022:1639` | xHCI98 USB 3 Controller (AMD Renoir/Cezanne) |
| AMD Van Gogh | `1022:163A`, `1022:163B` | xHCI98 USB 3 Controller (AMD Van Gogh) |
| AMD X370 | `1022:43B9` | xHCI98 USB 3 Controller (AMD X370) |
| AMD X399 | `1022:43BA` | xHCI98 USB 3 Controller (AMD X399) |
| AMD 300 Series | `1022:43BB` | xHCI98 USB 3 Controller (AMD 300 Series) |
| AMD A320 | `1022:43BC` | xHCI98 USB 3 Controller (AMD A320) |
| AMD 400 Series | `1022:43D5` | xHCI98 USB 3 Controller (AMD 400 Series) |
| AMD A520 | `1022:43EC` | xHCI98 USB 3 Controller (AMD A520) |
| AMD 500 Series | `1022:43EE` | xHCI98 USB 3 Controller (AMD 500 Series) |
| AMD 600 Series | `1022:43F7` | xHCI98 USB 3 Controller (AMD 600 Series) |
| AMD A620 | `1022:43FA` | xHCI98 USB 3 Controller (AMD A620) |
| AMD 800 Series | `1022:43FC`, `1022:43FD` | xHCI98 USB 3 Controller (AMD 800 Series) |
| AMD FCH | `1022:7812`, `1022:7814`, `1022:7914` | xHCI98 USB 3 Controller (AMD FCH) |
| NEC uPD720200 | `1033:0194` | xHCI98 USB 3 Controller (NEC uPD720200) |
| TI TUSB73x0 | `104C:8241` | xHCI98 USB 3 Controller (TI TUSB73x0) |
| NVIDIA TU102 | `10DE:1AD6` | xHCI98 USB 3 Controller (NVIDIA TU102) |
| NVIDIA TU104 | `10DE:1AD8` | xHCI98 USB 3 Controller (NVIDIA TU104) |
| NVIDIA TU106 | `10DE:1ADA` | xHCI98 USB 3 Controller (NVIDIA TU106) |
| NVIDIA TU116 | `10DE:1AEC` | xHCI98 USB 3 Controller (NVIDIA TU116) |
| VIA VL800/VL801 | `1106:3432` | xHCI98 USB 3 Controller (VIA VL800/VL801) |
| VIA VL805/VL806 | `1106:3483` | xHCI98 USB 3 Controller (VIA VL805/VL806) |
| Renesas uPD720201 | `1912:0014` | xHCI98 USB 3 Controller (Renesas uPD720201) |
| Renesas uPD720202 | `1912:0015` | xHCI98 USB 3 Controller (Renesas uPD720202) |
| ASMedia ASM1040 | `1B21:1040` | xHCI98 USB 3 Controller (ASMedia ASM1040) |
| ASMedia ASM1041 | `1B21:1041` | xHCI98 USB 3 Controller (ASMedia ASM1041) |
| ASMedia ASM1042 | `1B21:1042` | xHCI98 USB 3 Controller (ASMedia ASM1042) |
| ASMedia ASM1042A | `1B21:1142` | xHCI98 USB 3 Controller (ASMedia ASM1042A) |
| ASMedia ASM1142 | `1B21:1242` | xHCI98 USB 3 Controller (ASMedia ASM1142) |
| ASMedia ASM1143 | `1B21:1343` | xHCI98 USB 3 Controller (ASMedia ASM1143) |
| ASMedia ASM2142 | `1B21:2142` | xHCI98 USB 3 Controller (ASMedia ASM2142) |
| ASMedia ASM4242 | `1B21:2426` | xHCI98 USB 3 Controller (ASMedia ASM4242) |
| ASMedia ASM3042 | `1B21:3042` | xHCI98 USB 3 Controller (ASMedia ASM3042) |
| ASMedia ASM3142 | `1B21:3142` | xHCI98 USB 3 Controller (ASMedia ASM3142) |
| ASMedia ASM3241 | `1B21:3241` | xHCI98 USB 3 Controller (ASMedia ASM3241) |
| ASMedia ASM3242 | `1B21:3242` | xHCI98 USB 3 Controller (ASMedia ASM3242) |
| Etron EJ168 | `1B6F:7023` | xHCI98 USB 3 Controller (Etron EJ168) |
| Etron EJ188/EJ198 | `1B6F:7052` | xHCI98 USB 3 Controller (Etron EJ188/EJ198) |
| Fresco Logic FL1000 | `1B73:1000` | xHCI98 USB 3 Controller (Fresco Logic FL1000) |
| Fresco Logic FL1009 | `1B73:1009` | xHCI98 USB 3 Controller (Fresco Logic FL1009) |
| Fresco Logic FL1100 | `1B73:1100` | xHCI98 USB 3 Controller (Fresco Logic FL1100) |
| Fresco Logic FL1400 | `1B73:1400` | xHCI98 USB 3 Controller (Fresco Logic FL1400) |
| Zhaoxin ZX-100 | `1D17:9202` | xHCI98 USB 3 Controller (Zhaoxin ZX-100) |
| Zhaoxin ZX-200 | `1D17:9203` | xHCI98 USB 3 Controller (Zhaoxin ZX-200) |
| Zhaoxin KX-6000 | `1D17:9204` | xHCI98 USB 3 Controller (Zhaoxin KX-6000) |
| Zhaoxin KH-40000 | `1D17:9205` | xHCI98 USB 3 Controller (Zhaoxin KH-40000) |
| Zhaoxin KH-50000 | `1D17:9207` | xHCI98 USB 3 Controller (Zhaoxin KH-50000) |
| Intel 400 Series LP | `8086:02ED` | xHCI98 USB 3 Controller (Intel 400 Series LP) |
| Intel 400 Series | `8086:06ED`, `8086:A3AF` | xHCI98 USB 3 Controller (Intel 400 Series) |
| Intel Broxton | `8086:0AA8`, `8086:1AA8` | xHCI98 USB 3 Controller (Intel Broxton) |
| Intel Goshen Ridge | `8086:0B27` | xHCI98 USB 3 Controller (Intel Goshen Ridge) |
| Intel Bay Trail | `8086:0F35` | xHCI98 USB 3 Controller (Intel Bay Trail) |
| Intel Maple Ridge | `8086:1135`, `8086:1138` | xHCI98 USB 3 Controller (Intel Maple Ridge) |
| Intel Alpine Ridge | `8086:15B5`, `8086:15B6`, `8086:15C1`, `8086:15D4`, `8086:15DB` | xHCI98 USB 3 Controller (Intel Alpine Ridge) |
| Intel Titan Ridge | `8086:15E9`, `8086:15EC`, `8086:15F0` | xHCI98 USB 3 Controller (Intel Titan Ridge) |
| Intel Denverton | `8086:19D0` | xHCI98 USB 3 Controller (Intel Denverton) |
| Intel 7 Series | `8086:1E31` | xHCI98 USB 3 Controller (Intel 7 Series) |
| Intel Cherryview | `8086:22B5` | xHCI98 USB 3 Controller (Intel Cherryview) |
| Intel Pentium Silver | `8086:31A8` | xHCI98 USB 3 Controller (Intel Pentium Silver) |
| Intel Ice Lake-LP | `8086:34ED` | xHCI98 USB 3 Controller (Intel Ice Lake-LP) |
| Intel 500 Series | `8086:43ED` | xHCI98 USB 3 Controller (Intel 500 Series) |
| Intel Alder Lake | `8086:461E` | xHCI98 USB 3 Controller (Intel Alder Lake) |
| Intel Alder Lake-N | `8086:464E`, `8086:54ED` | xHCI98 USB 3 Controller (Intel Alder Lake-N) |
| Intel Elkhart Lake | `8086:4B7D` | xHCI98 USB 3 Controller (Intel Elkhart Lake) |
| Intel Jasper Lake | `8086:4DED` | xHCI98 USB 3 Controller (Intel Jasper Lake) |
| Intel Alder Lake PCH | `8086:51ED` | xHCI98 USB 3 Controller (Intel Alder Lake PCH) |
| Intel Barlow Ridge | `8086:5782`, `8086:5785` | xHCI98 USB 3 Controller (Intel Barlow Ridge) |
| Intel Apollo Lake | `8086:5AA8` | xHCI98 USB 3 Controller (Intel Apollo Lake) |
| Intel Core Ultra 200 | `8086:777D`, `8086:7EC0`, `8086:A831`, `8086:A87D` | xHCI98 USB 3 Controller (Intel Core Ultra 200) |
| Intel 700 Series | `8086:7A60` | xHCI98 USB 3 Controller (Intel 700 Series) |
| Intel 600 Series | `8086:7AE0` | xHCI98 USB 3 Controller (Intel 600 Series) |
| Intel Meteor Lake-P | `8086:7E7D` | xHCI98 USB 3 Controller (Intel Meteor Lake-P) |
| Intel 800 Series | `8086:7F6E` | xHCI98 USB 3 Controller (Intel 800 Series) |
| Intel Ice Lake | `8086:8A13` | xHCI98 USB 3 Controller (Intel Ice Lake) |
| Intel 8 Series | `8086:8C31` | xHCI98 USB 3 Controller (Intel 8 Series) |
| Intel 9 Series | `8086:8CB1` | xHCI98 USB 3 Controller (Intel 9 Series) |
| Intel C610/X99 | `8086:8D31` | xHCI98 USB 3 Controller (Intel C610/X99) |
| Intel Tiger Lake | `8086:9A13`, `8086:9A17` | xHCI98 USB 3 Controller (Intel Tiger Lake) |
| Intel 8 Series LP | `8086:9C31` | xHCI98 USB 3 Controller (Intel 8 Series LP) |
| Intel 9 Series LP | `8086:9CB1` | xHCI98 USB 3 Controller (Intel 9 Series LP) |
| Intel 100 Series LP | `8086:9D2F` | xHCI98 USB 3 Controller (Intel 100 Series LP) |
| Intel 300 Series LP | `8086:9DED` | xHCI98 USB 3 Controller (Intel 300 Series LP) |
| Intel 500 Series LP | `8086:A0ED` | xHCI98 USB 3 Controller (Intel 500 Series LP) |
| Intel 100 Series | `8086:A12F` | xHCI98 USB 3 Controller (Intel 100 Series) |
| Intel C620 Series | `8086:A1AF`, `8086:A22F` | xHCI98 USB 3 Controller (Intel C620 Series) |
| Intel 200 Series | `8086:A2AF` | xHCI98 USB 3 Controller (Intel 200 Series) |
| Intel 300 Series | `8086:A36D` | xHCI98 USB 3 Controller (Intel 300 Series) |
| Intel Raptor Lake | `8086:A71E` | xHCI98 USB 3 Controller (Intel Raptor Lake) |

## What `3.4.2.0` changes

The full list: the `3.4.2.0` entry of `releases/history.md`.

### USB
- 2000, XP and XP x64: a USB 3 controller the BIOS gives no interrupt
  (some HP laptops of 2012 with the Intel 7 Series controller) is
  offered the line the chipset routes it to, on the listed computers by
  default (`XhciIntelNoRouteIrq`), or polled, where XP stopped it at
  Code 12; on Windows 2000 on the listed computers it is polled, where an
  ACPI computer type stopped at start with STOP 0x000000A5 ("Known
  limitations").
- ME: unplugging a USB audio device while Windows' own USB audio driver
  plays it no longer stops Windows (98 SE: "Known limitations").
- An endpoint stop the controller undoes is repeated, and where it does
  not hold the device alone is reconnected: a UAS drive's refused SMART
  read could leave its endpoint waiting for good, its volume lost and
  Windows frozen.
- A larger event ring (`XhciEventRingTrbs`, 1,024 entries) and eight
  requests in flight on an audio or video stream, where there were four;
  the USB Tree's snapshot times each such stream.
- 98 SE and ME: a device unplugged just before Windows starts its driver
  is answered until Windows removes it.
- A port whose device reports a cold attach and never trains is reset
  three times, then left.
- The snapshot notes the BIOS handoff's USB legacy-support registers.

### Thunderbolt and USB4
- `XhciCmPreOsCleanup` `2`: a second start of the driver within two
  minutes in one boot keeps the tunnel its first start rebuilt, where it
  is wholly the driver's own; a chained device is still cleaned.
- The USB Tree names a Thunderbolt 1 or 2 router as such, and says when
  the ASM4242 handover was not sent for a firmware build it does not
  know (`XhciCmAsm4242Structural` `0`).
- Improved Thunderbolt networking compatibility.

### Storage
- Stor98: 4 KiB-sector drives work on every system, with 512-byte
  sectors on 98 SE to 7 and their own 4096-byte sectors on 8 to 10
  ("Known limitations").
- Stor98: VERIFY reads the drive; READ CAPACITY(16)'s short form is
  answered, so a drive above 2 TiB has its size on 7.
- Stor98 on 98 SE and ME leaves a drive the BIOS set up to the BIOS
  also with its memory access off; on 2000 to 10 a restart of Stor98 on
  a drive no longer clears its queues before the drive is seen stopped.
- UAS, per drive, off (`0`) by default: `XhciUasNoAtaPassThrough` `1`
  answers ATA and SMART requests in the driver, and `XhciUasFastReset`
  `1` resets a drive whose commands keep timing out without progress at
  the second timeout (`docs/using/tuning.md`).
- A drive that offers UAS and Bulk-Only, used as Bulk-Only (on a
  controller that cannot stream, or with `XhciForceBulkOnly`), has a
  Device Manager entry of its own for that, which Windows installs with
  its own Bulk-Only driver, so it no longer stops at Code 10 where its
  entry was installed with the UAS driver (`XhciBulkOnlyOwnId`,
  "SuperSpeed and UAS").

### Installer and package
- XP x64 and Server 2003 x64: Setup copies `usbui.dll` from Windows' own
  files when it is missing, for the Advanced and Power tabs; answer
  Windows' "Windows Logo testing" box with Continue Anyway.
- 98 SE: the uninstall also removes Windows Setup's records of the INFs
  it placed and the removed USB disks' entries.
- The Free USB drivers page's notice, the Progress page's wait line and
  "Shut down now" reworded; `/uninstall /list` changes nothing.
- The USB Tree: Save Firmware Tables takes the DSDT from Windows'
  registry copy on XP x64 and never saves `MSDM` or `SLIC` (the
  computer's Windows licence data); times on a processor whose
  time-stamp counter is not invariant are marked approximate;
  `XHCITR.EXE` on 64-bit Windows starts `XHCITR64.EXE`; the snapshot
  lists each UAS drive's last failed commands; a UAS drive at Code 10 on
  a controller that cannot stream has a note in its details ("SuperSpeed
  and UAS").
- The USB Tree's Settings window (View > Settings..., or the icon's
  menu) shows and sets the drivers' registry settings as Setup offers
  them, for every controller or one; `XHCITR /set` does the same from a
  command line. Quick save, on the icon's menu or an optional hotkey,
  saves the report and the snapshot with no questions ("The xHCI98 USB
  Tree").
- XHCIQUAL: `--edd` (see "EDD reports", `docs/using/edd-reports.md`);
  no USB 2 ports a caveat, not a disqualification.

### Other drivers
- usb-wdm-free 0.1.9: see its release notes
  (https://github.com/yeokm1/usb-wdm-free/releases).
- usb-wdm-nonfree v1.3.0: Microsoft's USB smart card reader driver
  (usbccid) for 2000 and XP, copied only; point Windows' hardware wizard
  at its folder.

## What `3.4.1.0` changes

The full list: the `3.4.1.0` entry of `releases/history.md`.

### USB
- 98 SE and ME: the USB Tree's snapshot names a device that holds a
  controller's shared IRQ at its start (its PCI location and ids).
- 2000 to 7: one stream error no transfer was waiting for now
  recovers the UAS disk's endpoint; if that fails, only the disk is
  reconnected, never every device on its controller (issues 22 and 24).
- A command the controller never answers is named in the snapshot.

### Thunderbolt and USB4
- 98 SE and ME: on an ASMedia ASM4242 USB4 host whose firmware hands
  USB4 over only through ACPI `_OSC`, the driver sends that handover itself
  when it knows the firmware (`XhciCmAsm4242Handover`).
- 98 SE and ME: the driver stops writing to the registry when Windows
  announces a shutdown or standby.
- 2000, XP and XP x64: the ASM4242 handover too, with
  `XhciCmAsm4242Reads` `1`.
- Eject: the confirmation names the drive; "It is now safe to
  disconnect the device."
- The USB Tree names AMD's other USB4 hosts.

### Storage
- 98 SE and ME: Stor98 runs only the drive it was loaded for. It took
  every NVMe drive before, Windows' own NVMe system disk included, and
  the start could stop at "Initing stor98.mpd".
- 98 SE and ME: a drive the BIOS has set up stays the BIOS's. Its Stor98
  entry shows Code 10.
- Stor98 polls a drive whose interrupts stop arriving, until the drive
  next starts (`StorIntWatchdog`, `storintwatchdog=`), also on an IRQ
  shared with another device.

### Installer and package
- 98 SE and ME: an ASM4242 USB4 host is found while Windows has not yet
  listed its router, and the Thunderbolt and USB4 box is ticked.
- Settings... on the Components page holds the USB settings. The
  Thunderbolt and USB4 page has its own, for Thunderbolt / USB4 and
  Stor98.
- The Progress page's bar moves with the work done.
- The installer's texts reworded: every page, Help, the console and its
  readme. Stor98's Advanced shows `StorMaxTransferKB` up to 2048 KiB on
  98 SE and ME.
- The USB Tree's Save Snapshot asks only for a folder, then says where
  the files went.
- 8, 8.1 and 10: the UAS disk driver is no longer installed.
- Updating on 2000 to 7 updates UAS disks that are not plugged in;
  on 98 SE and ME it no longer asks for the driver disk.
- Setup's log lines carry the time.

### Other drivers
- usb-wdm-nonfree 1.2.0: TASCAM's US-122 audio and MIDI interface on 98
  SE and ME.
- usb-wdm-free 0.1.8: `uac2` pops far less under CPU load, never
  turns a device's volume above 0 dB (`Uac2VolAboveZero`), and
  names each device by its own product name.

## What `3.4.0.0` changes

The full list: the `3.4.0.0` entry of `releases/history.md`.

### USB
- Devices on the computer's own ports kept across a resume that restarts
  the controller (`XhciResumeKeep`).
- A device that comes back with its serial number unreadable is no
  longer taken for a new one: if retries fail, it stays out until
  plugged in again.
- USB Audio 2.0 and 3.0 kept from Windows' USB Audio 1.0 driver (2000 to
  8.1); devices with several configurations split where eligible.
- Memory integrity's rules met on 8 to 10; smaller fixes throughout.
- 98 SE: a device with no working driver holding a shared IRQ slows or
  freezes Windows ("Known limitations").
- 98 SE and ME: rarely a start stops with "A fatal exception 0E has
  occurred at 019F:BFF72170" (98 SE) or "Error: 0E : 01A7 : BFF62170"
  (ME); turn the computer off and on.

### Thunderbolt and USB4
- 2000, XP and XP x64: every port of a hot-plugged dock placed
  (`XhciCmBridgeReq`); a first NVMe enclosure brings two wizards
  ("Thunderbolt and USB4: known limitations").
- Docks with an input device on their USB controller removed cleanly on
  2000 and XP under a USB4 host xHCI98 runs; elsewhere stop them first.
- Daisy chains behind such a host; on 2000's Standard PC HAL a drive
  chained after startup gets Code 10.
- A dock plugged in after startup needs bus numbers the BIOS reserves
  for the ports; without them, Code 31 on 7 and nothing behind the port
  on XP or 98 SE.
- USB4 version 2 routers run as version 2, Gen 4 links and their
  recovery included (`XhciCmUsb4V2`, `XhciCmGen4LinkRecovery`).
- Vista and 7 x86: a dock's USB controller starts at its first plug
  without a restart; `XhciCmRingInterrupt`; the ASM4242 named by its
  INF.
- A drive attached during the install is listed in Safely Remove after a
  restart or replug ("What `3.1.0.0` changes").
- XHCIPFW on 32-bit 8 and 8.1 (the service `xhcipff`); the installer
  readme's recovery steps reach the registry editor from the setup DVD's
  Repair your computer.
- 10 x64: waking from Sleep no longer stops Windows with 0x9F.

### Networking
- Thunderbolt networking (new): two computers on one Thunderbolt or USB4
  cable, in Linux's frame format; one link, not through a dock.
- SETUP's Thunderbolt and USB4 page has a Thunderbolt networking box,
  unticked by default (`/tbnet`, `/notbnet`); an update keeps the choice
  and the uninstall removes the adapter. The driver is in each folder's
  `cm\tbnet\`. On 98 SE the adapter's first install may ask for
  `xhcinet.sys`: type `C:\XHCI98`.

### Storage
- UAS drives answer pass-through and SMART (2000 to 10) and stop on
  their own in Safely Remove (2000 to 7); Stor98 on 8.1 fixed.
- A UAS drive behind a SuperSpeed hub is kept across the hub port's warm
  reset.

### Installer and package
- One folder per Windows family (`98-me\`, `nt-x86\`, `nt-x64\`,
  `INTEGRATE.TXT`); one build, `/debug` gone.
- A Thunderbolt and USB4 page, full Advanced lists, Help and F1. 98 SE
  and ME: more packages staged; a controller "not reached in this run"
  needs a restart and another run.
- The install writes each setting the drivers read at its default, only
  where it is missing; on 98 SE and ME the uninstall removes Stor98's
  settings, so a later install starts clean.
- The USB Tree's title shows its version.
- The `nt-x64\` folder goes into a Windows PE or Setup `boot.wim` with
  DISM, as well as `install.wim`; on XP x64 with no other USB driver, the
  controller has no Advanced tab and the root hub no Power tab.

### Other drivers
- usb-wdm-free 0.1.7: `usbprn` (printing, 98 SE and ME), `uac2` 0.1.6.0,
  `usbcom` 0.1.5.0 (FTDI, Prolific, Silicon Labs bridges); `rtl815x`
  beside the non-free one on XP to 7.

## What `3.3.0.0` changes

What `3.3.0.0` changed from `3.2.0.0` (roadmap-usb4 Phase 44; its entry in
`releases/history.md` is the short list). Every item was read in QEMU
virtual machines, the Thunderbolt 3 and USB4 ones on the `usb4-emu`
emulator's hosts and enclosure, unless it says otherwise; the records are
`docs/contributing/runs/run-44.md`, under each task's number. Every system
`3.2.0.0` supports is supported.

- **The connection manager is set back to start on demand** (roadmap-usb4
  tasks 44.32 and 44.12; design record 23 section 5.14). Windows makes it
  a boot driver when it puts its filter on the controller's PCI ports, and
  the installer sets it back at the end of each run (the safe-removal mark
  service after an install the installer did not run). On 64-bit Vista to
  10 a start without test-signing mode then leaves the Thunderbolt and
  USB4 devices at Code 52 (Code 39 on Vista) and the rest of the install as
  it was, where under `3.2.0.0` such a start stopped 64-bit 8 in Automatic
  Repair and rolled the install back on 7 x64. It stays a boot driver where
  Windows starts from a disk behind a port it filters. Keep test-signing
  mode on all the same ("What this is"). On Vista x64 and 7 x64 the
  installer now installs the Thunderbolt 3 / USB4 support only in
  test-signing mode, as on 64-bit 8 to 10.
- **Stor98 for drives behind a tunnel only** (task 44.3; design record
  31). The connection manager gives such a drive an id of its own, and on
  2000 to 8 Stor98's INF matches only that id, so Windows never offers
  Stor98 for an internal NVMe drive. On 8.1 and 10 Windows' own `stornvme`
  keeps an internal drive. On 98 SE and ME Windows may still offer it for
  an internal drive with no driver: decline it. An internal drive that
  already runs Stor98 keeps it ("NVMe drives behind Thunderbolt or USB4").
- **Stor98's settings named alike on every system**, each name starting
  with Stor: on 2000 to 10 the `DriverParameter` keys are renamed, and 98
  SE and ME gain `StorGoneMs`, `StorWriteIdleMs` and `StorHold9xMs`. A key
  of an old name is ignored after the update: rename it and keep its value
  ("NVMe drives behind Thunderbolt or USB4" lists them all).
- **Windows 8, 8.1 and 10: Secure Boot and Intel's Thunderbolt driver**
  (tasks 44.25 and 44.28). Where Secure Boot is on, the installer refuses
  a run with the USB drivers, and on 32-bit Windows one with the
  Thunderbolt 3 / USB4 support, before any change; on 64-bit 10 it says
  when memory integrity is on. Where Intel's Thunderbolt driver runs the
  controller, the Thunderbolt 3 / USB4 support is unticked; ticked, it
  replaces Intel's driver after a warning, and the uninstall gives the
  controller back to it. Read on a UEFI guest of 10 x64 with Secure Boot
  on, and with Intel's DCH Thunderbolt driver on the emulated controller.
- **Windows 8, 8.1 and 10: the uninstall of the USB drivers in one run**
  (tasks 44.22 and 44.24), read on 8 x86, 8.1 x86 and 10 x86 and x64; the
  installer no longer says it may need a restart and a second run.
- **The ASMedia ASM4242 USB4 host** (tasks 44.39 to 44.42): the
  connection manager finds the PCI ports its tunnels arrive on, so a
  tunnelled drive there is treated as one behind any other USB4 host.
  Read on the emulator's ASM4242 host; not yet read on real hardware.
  `XhciCmAsmediaPcie7Credits` is a bench switch for that host, absent by
  default ("The connection manager's registry values").
- **XHCIPFW** (tasks 44.49 and 44.72; design record 32): a small part of
  the install that runs at Windows start, `XHCIPFW.VXD` on 98 SE and ME
  and `XHCIPFW.SYS` on 32-bit 2000 to 7. Where the firmware leaves the
  USB4 ports' windows above 4 GB it closes them, so Windows finds the
  devices behind them. It is a choice under the Thunderbolt 3 / USB4 box,
  ticked with it, and can be unticked. It is not offered on 32-bit 8, 8.1
  and 10 ("Thunderbolt and USB4: known limitations"). Read on the
  emulator's ASM4242 layout.
- **A tunnel this driver built, built again after a hibernation** (task
  44.26; design record 22 section 9.4), also when Windows had restarted
  the controller's driver in between; the drive was missing after the
  resume. Standby or hibernation with a tunnelled device attached is still
  not supported.
- **98 SE and ME: a tunnelled NVMe drive with no interrupt starts** (task
  44.30; design record 28 section 12), Stor98 polling it, when the
  installer installed or staged the bundled Stor98 (`XhciCmNoIrqStart`,
  on by default). The install now writes `XhciCmBridgeNumbering` and
  `XhciCmNoIrqStart` at their default `1` where they are missing (task
  44.46); `INSTALL /set` sets both.
- **Windows 2000 with the Standard PC HAL: a tunnelled NVMe drive plugged
  after the start gets an interrupt** from the connection manager and
  starts on Stor98, which runs it polled (task 44.53;
  [issue 19](../issues/19-w2k-standard-pc-hot-plug-interrupt.md)); it was
  left at Code 10.
- **98 SE and ME, USB4: a tunnelled drive that drops off with the whole
  USB4 controller comes back** with its files once the controller returns,
  with no restart (task 44.54; design record 21 section 6.1.2). While the
  controller is away the drive stays listed and its reads and writes fail.
  Read on the emulated ASM4242 host.
- **Fixed: Windows 98 SE and ME could hang at Shut Down or Restart** with
  a USB4 enclosure attached and the connection manager running (tasks
  44.43 and 44.47; design record 21 section 5.6).
- **Fixed: on 98 SE and ME Stor98 looked for its drive on the first 16 PCI
  buses only**; it now looks on every bus, as a drive behind a USB4 port can
  be (on the emulator's ASM4242 those ports have buses 19h to 78h).
- **Fixed: on Windows 2000 and XP the first plug of an enclosure never
  seen before could leave its drive without a driver** after its first
  Found New Hardware wizard (task 44.52); the safe-removal mark service's
  restart of the enclosure now sets the drive's install to run again, so
  Windows offers Stor98 for it.
- **Fixed: on Windows 2000 a controller's restart, disable or update no
  longer fails with Code 31** until a restart when Windows' own EHCI
  driver, or a second controller on this driver, starts after it (task
  44.81; [issue 20](../issues/20-w2k-hcd-link-collision-code-31.md)). The controller's name on Windows 2000 and later is now
  `\DosDevices\XHCI98HC<k>`, with an `HCD<m>` alias on 2000, XP and XP x64
  ("Known limitations" has what an update from `3.2.0.0` may still show).
- **Fixed: on Windows 2000 the update over `3.2.0.0` with the
  Thunderbolt 3 / USB4 support could stop** with "XHCIMARK.EXE could not
  be put into ... (error 5)" (task 44.76); the installer now waits for the
  old service to end.
- **Fixed: on 98 SE a leftover `xhci98.tmp` could replace `xhci98.sys`**
  at the start after a controller was added (task 44.13); the installer
  deletes such a leftover. `INSTALL.BAT` refusing too many arguments exits
  with errorlevel 1 on 98 SE and ME (task 44.4).
- **The installer's pages** (tasks 44.7 and 44.10): the Other USB
  drivers page is two pages, "Free USB drivers" and "Non-free USB drivers",
  each shown only when the download carries that set, and the Welcome page
  shows more of the device list. On 98 SE the free `uac2` and `usbmidi`,
  and on ME `uac2`, can again be ticked for Windows to use at a device's
  first plug (task 44.11). On 98 SE and ME the installer puts `stor98.mpd`
  in place before the first tunnel drive is plugged, so Windows no longer
  asks for the "Stor98 Storage Controller Disk". An install of the
  connection manager alone is named "xHCI98 Thunderbolt 3 / USB4 connection
  manager" in Add/Remove Programs on 98 SE and ME (task 44.60). Updating
  from `3.2.0.0` with an ASM4242 controller, the first run leaves Stor98 on
  a tunnel drive it already runs and says to run the installer again after
  the restart (task 44.77).
- **Names: "USB 3", not "USB 3.x"** (tasks 44.5, 44.55 and 44.61): "xHCI98
  USB 3 Root Hub", "xHCI98 USB 3 Hub", the service, the F6 floppy's list,
  the USB Tree's port counts and the installer's title, pages and
  Add/Remove Programs entry ("xHCI98 USB 3 drivers"). On 98 SE and ME the
  controller's Properties window names its maker and product, and Device
  Manager names many more controllers ("Controller names in Device
  Manager").
- **The xHCI98 USB Tree** (tasks 44.8, 44.9, 44.35, 44.62 and 44.80; "The
  xHCI98 USB Tree"): a USB4 device on a Thunderbolt 3 controller reads
  "USB4 v1.0 device, running as Thunderbolt 3"; the status bar says when a
  driver update waits for a restart; a USB storage device shows whether it
  runs UAS or Bulk-Only and why; a new item, "PCI and bridge
  diagnostics"; the "can run faster" notice no longer asks for more than
  the computer gives; and a new option, "Watch Windows' own USB ports
  too", off by default. XHCITR `3.2.0.0` cannot take a snapshot from this
  driver on Vista and later; use this release's.
- **Stor98 reports the SCSI status GOOD** for a request it answers itself
  successfully (task 44.58); on Windows ME a program using ASPI could read
  a target status of 05h after another such program had run.
- **USB4 hosts with a drive attached at power-on whose host names no tunnel
  port to it**: the connection manager no longer reads the tunnel ports
  every 200 ms for as long as Windows runs; it tries for a minute and then
  stops (task 44.57).
- **The other USB drivers** (tasks 44.34, 44.44, 44.68, 44.70, 44.71 and
  44.73): the `-free` download carries usb-wdm-free 0.1.4 and the
  `-nonfree` download usb-wdm-nonfree 1.1.0's Ethernet, HID and serial
  drivers, without its Wi-Fi drivers ("Licensing" and
  `releases/history.md` list what each adds).
- **The licence**: this project's own source is under GPL-2.0-or-later
  (task 44.38); every release up to `3.2.0.0` was GPL-2.0-only. Stor98
  stays BSD-3-Clause.
- **`XHCICLN` dropped** (task 44.82): the cleanup tool is no longer in the
  download; the downloads from `2.4.0.0` to `3.2.0.0` still carry it.
- **Known limitation, kept: the rare stall on 98 SE and ME** after closing
  a program that had a file open on an unplugged Thunderbolt or USB4 drive
  was read to be Windows 98 SE's own mouse driver, `VMOUSE.VXD` (task
  44.29; "Thunderbolt and USB4: known limitations").

## What `3.1.0.0` changes

What `3.1.0.0` changed from `3.0.0.0`, as written when it was cut. Later
releases changed some of it, and where they did the item says so or "NVMe
drives behind Thunderbolt or USB4" and "Targets and their standing" have
the current state.

- **The x64 package's filter INF is in the main folder.** In
  `release-x64\` and `debug-x64\`, `xhcicmf.inf` now sits beside
  `xhcicm.inf` and `xhcicm.sys`, with no `nt\` subdirectory; the x86
  package keeps `nt\` (never point Windows 98 or ME at it).
- **`XHCISNAP` removed.** `XHCISNAP.EXE`, `SNAPON.BAT`, `SNAPGET.BAT`
  and `SNAPOFF.BAT` are no longer in the package (`tools\xhcisnap\` is
  gone), and the xHCI98 USB Tree (`XHCITR.EXE`, `XHCITR64.EXE`) does
  what they did ("The log, and how to send one"):
  - The field report, `SNAPGET.BAT` (`XHCISNAP -report`): File > Save
    Report... or `XHCITR /report <file>`, whose report now ends with a
    "Snapshot" part for each xHCI controller `xhci98.sys` drives - the
    device report the `.DEV` held and the decoded dump the `.TXT` held,
    the dump left out at level 4, where it can hold internal addresses.
    That one file is the bug report. The `.DEV`, `.TXT`, `.BIN` and
    `.PSC` files themselves: File > Save Snapshot... or
    `XHCITR /snapshot [NAME]`, with the same names and exit codes.
  - `SNAPON.BAT`, `SNAPOFF.BAT` and `XHCISNAP -verbosity`, `-on`,
    `-disable`: View > Logging level... or `XHCITR /setlevel xhci98=<0-4>`
    (both in `3.0.0.0` already), then a restart.
  - `XHCISNAP -dump` and `-o NAME`: `XHCITR /dump [/o NAME]`.
  - `XHCISNAP -probe`: `XHCITR /probe`.

  `xhci98.sys` is unchanged by it: Windows' own property tabs and the
  tool read it through the same door. Where a section below names
  `XHCISNAP`'s report, read the snapshot in the tool's report: the same
  text, from the same code.
- **The Thunderbolt 3 / USB4 support is experimental, and never chosen
  for you.** Its box on the Components page reads "Thunderbolt 3 / USB4
  support (experimental)" and is unticked whether or not a controller is
  found; the line under it says whether one was found, and its name.
  Tick it on purpose; the console asks with Enter for N, `/yes` leaves
  it out, and `/thunderbolt` includes it. The connection manager alone
  (USB drivers unticked, and Windows 8 and 8.1) is unchanged. An update
  over an install that has the Thunderbolt driver leaves that driver as
  it is unless you tick the box.
- **Stor98 comes with the Thunderbolt 3 / USB4 support.** Ticking the
  Thunderbolt box ticks the "Stor98 (hot-pluggable NVMe driver)" box under
  it, which you can untick;
  in the console `/thunderbolt` includes Stor98 too (asked, Enter for Y;
  unasked with `/yes`), and `/nostor98` leaves it out.
- **Windows 98 SE and ME: Stor98 is updated as a file.** An older
  `stor98.mpd` is no longer replaced by reinstalling the drive while
  Stor98 runs it, which stopped Windows 98 SE with "fatal exception 0E"
  when an update ran with a Thunderbolt or USB4 enclosure attached. The
  installer writes the new file beside the old one and has Windows swap
  them at the next start (`WININIT.INI`), refreshes the copy of
  `stor98.inf` Windows keeps, and asks you to shut down and start
  Windows again. It does so whether or not the enclosure is plugged in,
  so an enclosure that was unplugged during the update no longer keeps
  the old Stor98. At Install the window first asks "Please disconnect
  all external NVMe drives before proceeding", OK or Cancel (Cancel stays
  on the Summary page); the console asks the same, Enter for N, which
  leaves Stor98 for a later run.
  A drive first plugged after the start binds to the new Stor98. And an
  install the helper could not finish is now reported as unfinished,
  with steps, rather than as "nothing to do".
- **No Stor98 record without a Stor98 change.** A run that leaves a
  Stor98 installed by hand alone no longer writes
  `HKLM\Software\xhci98\Stor98`; its `Version` is written only when the
  run installed or updated the bundled Stor98. The uninstall removes
  that key, and `Software\xhci98` when nothing else is in it, also when
  Stor98 itself stays.
- **A warning before Thunderbolt 3 and USB4 are turned off.** When a run
  will write `XhciCmMode` `0`, the Summary page and the console say
  "Thunderbolt 3 and USB4 will be turned off (XhciCmMode 0)".
- **The bundled Stor98 goes on tunnel drives only.** The installer
  installs Stor98 only when it is chosen (its box, ticked with the
  Thunderbolt 3 / USB4 one) and only with
  the Thunderbolt 3 / USB4 support in the same run, and then only on NVMe
  drives behind a Thunderbolt or USB4 tunnel, as the connection manager
  reports them; the installer selects tunnel drives only. An internal NVMe
  drive is left as it is, and so is one whose place cannot be read; the
  installer says which and why. Stor98 is not for internal NVMe drives:
  do not install it on one. The rest of this item held up to `3.2.0.0`;
  from `3.3.0.0` Stor98's INF for 2000 to 8 matches only the id the
  connection manager gives a drive behind a tunnel ("NVMe drives behind
  Thunderbolt or USB4 (new in `3.0.0.0`)" has the current state). On 2000
  to 7 Stor98's INF matches every
  NVMe controller (`PCI\CC_010802`), so Windows may still offer Stor98
  for an internal NVMe drive that has no driver, later: decline it there.
  On Vista x64 and 7 x64 with this package's test certificate trusted, Windows puts the
  staged Stor98 on such a drive at the next start without asking (read in
  a virtual machine), as on 8 below.
  Windows 8 has no NVMe driver of its own, and there Windows itself puts
  the staged Stor98 on an internal NVMe drive that has no driver: with
  this package's test certificate trusted it does so without a prompt,
  and otherwise through the "can't verify the publisher" prompt (not
  read). The drive then runs on Stor98.
  On 8.1 and 10 it names that id as a compatible id only, which Windows'
  own NVMe driver (`stornvme`) outranks, so there Windows never chooses
  Stor98 by itself: it goes only on a drive the installer or you put it
  on. Where a
  Stor98 the installer did not put on a tunnel drive is installed (by
  hand, on an internal drive), the installer changes none of its
  settings, and when that Stor98 is another release than the bundled one
  it changes nothing of Stor98 at all, since every Stor98 drive shares
  one driver file. The uninstall removes the bundled Stor98 also from a
  tunnel drive Windows bound to it after the install (an enclosure first
  plugged after the restart), when Stor98 was not there before this
  package's installer. While a drive the installer did not put Stor98 on
  uses it, such as an internal drive Windows 8 gave it, the uninstall
  leaves Stor98 in place, its service and INF with it, and the tunnel
  drives keep it too.
- **The Thunderbolt 3 / USB4 connection manager alone.** The installer
  can install `xhcicm.sys` (with its tunnel-port filter and, on NT, the
  safe-removal mark service) without `xhci98.sys` and `xhciuas.sys`, for
  a computer whose USB 3 controllers already run another driver: on
  Windows 2000 to 7 untick the USB drivers on the Components page. On
  Windows 98 SE and ME the USB drivers stay
  required, since those systems have no USB 3 driver of their own. The
  connection manager needs nothing of `xhci98.sys` on NT: a tunnel's
  devices are driven by whatever driver Windows has for them, the USB
  controller of a dock and a USB4 port's USB 3 devices by the computer's
  own USB 3 driver (design record 21 section 3.5). A run without the USB
  drivers leaves an installed `xhci98.sys` as it is; the uninstall
  removes everything, and Add/Remove Programs shows one entry, named for
  what is installed. `tools\xhciinst\readme.txt`, "The connection manager
  alone".
- **The installer opens from `SETUP.EXE`.** To install or update,
  double-click `SETUP.EXE` at the top of the package (to uninstall, Remove
  in Add/Remove Programs, or `SETUP /uninstall`): it opens
  the installer's window with no console window behind it, and on Vista
  and 7 Windows asks once, for the window. `INSTALL.BAT` still works and
  is kept for the command line and scripts (`INSTALL /console ...`); run
  with no arguments it opens the same window.
- **Windows 8 and 8.1: the connection manager alone.** This item is as
  `3.1.0.0` shipped it: from `3.2.0.0` the USB drivers are an opt-in
  install on 8, 8.1 and 10, Windows 10 is admitted, and the bundled Stor98
  is offered there ("Targets and their standing"). The installer now
  starts on Windows 8 and 8.1 (it reads the version through
  `RtlGetVersion`) and installs the connection manager alone there,
  without offering the USB drivers: Windows' own `usbxhci.sys` runs the
  USB controllers. Windows 8 and 8.1 were not then a target of this
  package's USB drivers and had never been run with it: the connection manager
  installs there through the same INF sections as on Vista and 7,
  untested. On 64-bit 8 and 8.1 it loads only in test-signing mode, which
  those systems let you turn on only with Secure Boot off, or on a start
  with driver signature enforcement disabled; it is not Microsoft-signed.
  The bundled Stor98 was not offered there. Windows 10 and later were still
  refused (Windows 11 has a USB4 connection manager of its own).
- **Stor98 is built with the package, at its version: `3.1.0.0`.** The
  bundled NVMe driver is now built from this source with the USB drivers
  (its source has been in this repository since 2026-10-19) and carries
  the package's version and date, like every other file of the download:
  `stor98.mpd`, both `stor98.sys` and `stor98.inf` say `3.1.0.0`, dated
  10/19/2026. Its last release on its own was Stor98 `0.4.0.0`, bundled in
  `3.0.0.0`; `3.1.0.0` is the next version after it, so the installer
  updates a `0.4.0.0` (or `0.3.0.x`) it finds, as it updates any older
  Stor98. What changed in Stor98 since `0.4.0.0` is in its readme's
  release history (`stor98-readme.txt` points to it). Its link-state
  interface with `xhcicm.sys` (interface 4) is unchanged.
- **One signature for every x64 driver.** The 64-bit `stor98.sys` is now
  test-signed with the same certificate as `xhci98.sys`, `xhciuas.sys`
  and `xhcicm.sys`, in the same step: `xhci98-test.cer` covers them all.
  `stor98-test.cer`, the certificate of Stor98's own that signed it up
  to `0.4.0.0`, is no longer in the download. Test-signing mode on Vista
  x64 and 7 x64 loads them all, as before.
- **Stor98 in the published tree.** Stor98's files are now part of each
  `release-` directory as published, like the USB drivers, rather than
  added to the download alone.
- **Known limitation: a drive attached during the install.** On Windows
  2000 to 7, a Thunderbolt or USB4 drive that is already attached when the
  package is installed or updated is not listed in Safely Remove Hardware
  until Windows restarts or the drive is plugged in again (`3.0.0.0` did
  the same).
- **Known limitation: the eject under the USB4 connection manager on 2000
  to 7.** On a Windows 2000 to 7 computer with ACPI, where the package's
  own USB4 connection manager runs, ejecting a device that connects
  through a PCI bridge (a dock, or an enclosure with a switch in it) stops
  it in Windows, but the tunnel to it stays up until you unplug it. Seen
  on Windows 7; the cause, ACPI's bus filter left on the ejected bridge,
  is the same on 2000, XP and Vista.

## What `3.0.0.0` adds

`3.0.0.0` adds a third driver, `xhcicm.sys`, for Thunderbolt and USB4
controllers, an install tool and a graphical tool, the xHCI98 USB Tree.
`xhci98.sys` and `xhciuas.sys` carry
`2.4.0.0`'s code under the new version number.

- **Thunderbolt 3 and USB4, with `xhcicm.sys`.** A Thunderbolt or USB4
  dock, enclosure or NVMe case plugged into the machine reaches Windows:
  its PCI Express tunnel is set up, and the devices behind it (an NVMe
  drive, a dock's own USB controller, a network adapter) appear in Device
  Manager and run under their own drivers. One driver, two ways of doing
  it, chosen per controller at its start: on an Intel Thunderbolt
  controller whose firmware runs its own connection manager (Thunderbolt
  2 and 3, and the USB4-generation Intel controllers while their firmware
  does), `xhcicm.sys` is that firmware's client and approves each device;
  on a USB4 host whose firmware runs none (AMD, ASMedia, Intel Lunar Lake
  and later, Barlow Ridge), `xhcicm.sys` is the connection manager itself
  and builds the tunnels. "Which controllers", below, lists them; every
  one but a single Alpine Ridge part is untested, and it says what that
  means.
- **No Refresh.** When a tunnel comes up or goes down, `xhcicm.sys` makes
  Windows look at the ports behind it at once: through Windows 98 SE's and
  ME's configuration manager, and on Windows 2000 to 7 through a small
  filter on those PCI ports. A device plugged in appears without Scan for
  hardware changes, and one unplugged leaves.
- **Safe removal.** A device behind a tunnel is offered in Windows 2000's
  "Unplug or Eject Hardware" and XP's, Vista's and 7's "Safely Remove
  Hardware"; on every system, `INSTALL /eject` stops it too ("Removing a
  Thunderbolt or USB4 device safely").
- **`INSTALL.BAT`**, with `XHCIINST.EXE` and `XHCIIN64.EXE`: installs,
  updates and uninstalls every driver of the package in one run on
  Windows 2000 to 7, and stages it with the steps left printed on 98 SE and
  ME ("Installing, updating and uninstalling with INSTALL.BAT").
  INSTALL.BAT is beta in 3.0.0.0: if it fails or you prefer, install
  through Device Manager as before ("Installing").
- **A device viewer and eject in the same tool**: `INSTALL /devices` lists
  the Thunderbolt and USB4 devices connected now and the Windows devices
  behind each, and `INSTALL /eject` stops one, on every system (design
  record 24).
- **The xHCI98 USB Tree** (`XHCITR.EXE`, `XHCITR64.EXE` on 64-bit
  Windows): a window with every USB controller, hub and device and every
  Thunderbolt and USB4 controller and device, with their details and
  descriptors, and an icon in the notification area that starts at login
  and removes a USB storage device or a Thunderbolt or USB4 device safely,
  with no administrator rights ("The xHCI98 USB Tree", below; design record 25). `xhcicm.sys`
  gains a read-only query device for it.
- **PCI details Windows does not show**, in the same tool: the interrupt
  pin and line, MSI and MSI-X, the PCI Express link speed and width, the
  BARs and a bridge's buses and windows, for each xHCI controller this
  driver runs and for each Thunderbolt or USB4 controller, its tunnel
  ports and the devices behind them. The drivers only read; `xhci98.sys`
  gains a read-only query request for it and for its log (design record
  13 section 8.12).
- **The log records by default**: `XhciLogVerbosity` is `2` on a fresh
  install, so a log exists before anyone asks for one; an update keeps the
  value already there ("The log, and how to send one").
- **Device Manager names by generation**: "Thunderbolt and USB4 in Device
  Manager", below.
- **A link-state channel for Stor98**: `xhcicm.sys` publishes the
  channel of Stor98's interface 4 on every target, Windows 98 SE to 7,
  and states on it which NVMe drive is behind a tunnel; Stor98 `0.4.0.0`
  reads it on all of them ("NVMe drives behind a tunnel").
- **Stor98 in the download**: the package carries a release of Stor98,
  the NVMe driver for Windows 98 SE to 7, as its own project released
  it (which release: "NVMe drives behind Thunderbolt or USB4", under
  "Installing").
- **`XHCISNAP` to be deprecated.** The xHCI98 USB Tree now shows both
  drivers' logs and sets their logging level. `XHCISNAP.EXE` and its
  `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` still ship in this release
  and work as before; they will be deprecated in a subsequent release.

### Which controllers

`xhcicm.sys` claims these host controllers (the NHI, "native host
interface", function of a Thunderbolt or USB4 controller), by PCI id:

| Generation | PCI ids (`8086:` unless named) | Connection manager | Device Manager |
|---|---|---|---|
| Falcon Ridge (Thunderbolt 2) | `156A`, `156C` | the firmware's | `xHCI98 TB2 Controller (Intel Falcon Ridge)` |
| Alpine Ridge (Thunderbolt 3) | `15BF` (JHL6240), `1575`, `1577`, `15D2`, `15D9` | the firmware's | `xHCI98 TB3 Controller (Intel Alpine Ridge)` |
| Titan Ridge (Thunderbolt 3) | `15E8`, `15EB` | the firmware's | `xHCI98 TB3 Controller (Intel Titan Ridge)` |
| Ice Lake (integrated) | `8A0D`, `8A17` | the firmware's | `xHCI98 TB3 Controller (Intel Ice Lake)` |
| Maple Ridge | `1134`, `1137` | the firmware's while it runs, else `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Maple Ridge)` |
| Tiger Lake, Alder Lake, Raptor Lake, Meteor Lake (integrated) | `9A1B`, `9A1D`, `9A1F`, `9A21`; `463E`, `466D`; `A73E`, `A76D`; `7EB2`, `7EC2`, `7EC3` | the firmware's while it runs, else `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Tiger Lake)`, `(Intel Alder Lake)`, `(Intel Raptor Lake)`, `(Intel Meteor Lake)` |
| Lunar Lake, Panther Lake, Wildcat Lake (integrated) | `A833`, `A834`; `E333`, `E334`, `E433`, `E434`; `4D33` | `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Lunar Lake)`, `(Intel Panther Lake)`, `(Intel Wildcat Lake)` |
| Barlow Ridge | `5781`, `5784` | `xhcicm.sys`'s | `xHCI98 USB4 Router (Intel Barlow Ridge)` |
| AMD Pink Sardine | `1022:1668`, `1022:1669` | `xhcicm.sys`'s | `xHCI98 USB4 Router (AMD Pink Sardine)` |
| ASMedia ASM4242 | `1B21:2425`, by class | `xhcicm.sys`'s | `xHCI98 USB4 Router (ASMedia ASM4242)` |
| Any other USB4 host (PCI class `0C0340`) | by class | `xhcicm.sys`'s | `xHCI98 USB4 Router` |

Not claimed: Apple machines and Apple's controllers, the USB-only
Thunderbolt functions (`15DC`, `15DD`, `15DE`), and the older Light Ridge,
Cactus Ridge, Redwood Ridge and Win Ridge parts.
Thunderbolt 3 docks and enclosures work on every host above; a USB4
device on a Thunderbolt 3 host works as a Thunderbolt 3 device would,
subject to the host's firmware. Daisy chains (a device behind a device),
DisplayPort tunnels and host-to-host links are not supported in this
release; under the USB4 connection manager a Thunderbolt 3 device behind
a USB4 host gets its PCI Express tunnel, router by router.

The machine must boot through the BIOS (CSM, legacy boot), as for every
target here, and the BIOS must leave the controller switched on and
visible: see "Thunderbolt and USB4: known limitations" for the BIOS
settings.

**Untested hosts.** Only the Alpine Ridge JHL6240 (`8086:15BF`) has been
captured from a real machine, a ThinkPad P14s Gen 1, and the Thunderbolt 3
half was built and read against an emulator of that capture. It has since
been read on the owner's ThinkPad P14s Gen 1 under Windows 98 SE: hot plug,
Safely Remove and re-plug of an NVMe enclosure
(`docs/issues/18-p14s-gen1-tb3-nvme-hot-plug-98se.md`). Every other
Thunderbolt controller this driver claims - Falcon Ridge, the other
Alpine Ridge parts, Titan Ridge, Maple Ridge, and the Ice Lake to Meteor
Lake integrated controllers - is untested: it is driven to the Linux
driver's description of its messages, and no capture of it exists. Every
USB4 host is untested as well: the USB4 connection manager has run only
on the emulator's host models (Alder Lake's, from a capture of a
Framework laptop, a minimal one, and one of ASMedia's ASM4242). No
USB4 host has been read with this driver on real USB4 hardware. Each of them acts by default (owner, 2026-10-15). On such a
machine the driver may start and do nothing; the `debug` flavour's log says why.
`XhciCmMode` `0` turns the driver off without uninstalling it. A report
from any of these machines, working or not, is new information.

### Installing it

`INSTALL.BAT` installs `xhcicm.sys` with the rest of the package, and on
Windows 2000 to 7 it is the way to install it: besides the controller's
own driver it puts the package's filter on the controller's PCI ports
(`nt\xhcicmf.inf` in each flavour directory; from `3.1.0.0` the x64
directories have it at the root as `xhcicmf.inf`), which on XP and later
Windows takes only when it is forced, as the tool does. On Windows 2000
the first install of that filter takes the Thunderbolt switch's other
devices away until the restart the tool asks for. On Windows 98 SE and
ME the controller appears as an unknown PCI device or "Other device";
point its Update Driver at the flavour directory, as for the xHCI
controller. There is no filter on 98 SE and ME.

### Plugging in, and the approval

Plug the device in and wait a few seconds: under the firmware connection
manager `xhcicm.sys` approves it (well inside the nine seconds or so
after which a USB4 device on a Thunderbolt 3 host gives up its PCI
Express tunnel and falls back to plain USB), the tunnel comes up, and the
devices behind it are found. "Thunderbolt 3: which devices get a tunnel,
and what that means", below, is the approval and what it lets a device
do. Under the USB4 connection manager the same value, `XhciCmApprove`,
decides whether a newly connected device gets its PCI Express tunnel; at
`0` no new tunnel is built, tunnels found at the start are kept, and the
USB3 tunnel is left alone.

A device that was already tunnelled when Windows started - by the BIOS's
pre-boot support, or by a firmware connection manager - keeps its tunnel
and works: `xhcicm.sys` never resets a tunnel it finds at its start
(`XhciCmPreOsCleanup` below is the one exception, and only when set).

### The xHCI98 USB Tree

`XHCITR.EXE` (`XHCITR64.EXE` on XP x64, Vista x64 and 7 x64) is installed
by `INSTALL.BAT` in the install directory it asks for (`C:\XHCI98` on
the system's drive unless another is chosen, or given with `INSTALL /to
<path>`; an update keeps the directory an earlier install used, test
builds' `%windir%\XHCI98\` included, unless another is chosen, when the
tool moves it) and started at login as an icon in
the notification area. Each user can turn that off with "Start at login"
in its View menu or, from `3.4.2.0`, the Settings window's "xHCI98 USB
Tree" tab (the icon's right-click menu up to `3.4.1.0`), with no
administrator rights; `INSTALL /notray` turns it off for the whole machine. It runs as
the logged-on user, with no administrator rights.

- **The window** shows every USB host controller (this driver's and
  Windows' own), its hubs and devices with speed, port, IDs, driver and
  Device Manager status, and every Thunderbolt and USB4 controller with
  its devices, link, approval, tunnels and the Windows devices behind
  them. View > Descriptors decodes the selected device's descriptors; the
  View menu's check boxes hide or show each kind of item; File > Save
  Report writes it all to a text file.
- **From `3.4.2.0`, the Settings window** (View > Settings..., a
  controller's right-click, or the icon's right-click menu) shows the
  drivers' registry settings with the meanings, choices and defaults
  Setup shows, on a tab each for the USB driver, the Thunderbolt and
  USB4 driver and Stor98 where they are installed, and changes them.
  "Applies to" sets a value for every controller or for one; after
  Apply the window says what each change needs (a restart, or a drive
  plugged in again) and offers to restart Windows. On 2000 to 10 it asks
  for administrator rights, and it changes nothing while Setup runs.
  `XHCITR /set Name=Value [Name=Value ...] [/on <instance id>]` does the
  same from a command line, and `XHCITR /set /show` lists the values
  (`xhcitr\README.TXT` has the exit codes). Its "xHCI98 USB Tree" tab
  holds the tool's own choices: Start at login, the removal warning,
  "Tell me when a device can run faster", "Watch Windows' own USB ports
  too", the quick-save hotkey and its sound.
- **From `3.4.2.0`, Quick save** on the icon's right-click menu saves the
  report, the snapshot and the settings with no questions, in a new
  folder named by the date and time under the install folder's
  `Reports\` (such as `C:\XHCI98\Reports\2026-11-07_121530`), or under
  "xHCI98 Reports" in your documents where the install folder cannot be
  written. A balloon (on 98 SE a small window) says where; click it to
  open the folder (not on Windows 2000, whose balloons take no clicks). A hotkey, off by default, also starts it
  (`Ctrl+Alt+F11` when turned on, or another the Settings window
  offers); it works while Windows still takes keyboard input. The icon's
  right-click menu is now Open, Settings..., About, Quick save (report +
  snapshot) and Exit.
- **The icon's menu** lists the USB storage devices and the Thunderbolt
  and USB4 devices that can be removed safely. Removing one follows the rules of `INSTALL /console /eject`: the
  tool confirms, waits for a current reading from `xhcicm.sys`, asks
  Windows to stop the device and says when to unplug it.
- **Balloons** say when a device connects, waits for approval or
  disconnects, on Windows 2000, ME and later; on 98 SE the icon's tooltip
  shows the last event (from `3.2.0.0` again after a pointer rested on the
  icon until the tooltip timed out, after which 98 SE's shell showed it no
  more; roadmap task 43.72). A balloon never says how a device left: only a
  removal the tool made reports its outcome, and, from `3.2.0.0`, the
  unexpected-removal warning below. In `3.1.0.0` the tray
  warned "The Thunderbolt/USB4 controller stopped answering" after every
  sleep or hibernation with nothing wrong (read on 7 in a virtual
  machine, 2026-10-22); from `3.2.0.0` a sleep or a stop of the
  controller raises no warning, and a resume that fails still does.
- **From `3.2.0.0`, on Windows 2000 and later only**, a warning balloon
  says "A Thunderbolt or USB4 device was removed unexpectedly." when
  Windows removed a running device behind a Thunderbolt or USB4 tunnel
  that had not been stopped first: unplugged without Safely Remove, its
  link lost, or its own driver failed. It does not say whether any data
  was lost. The controller's details count these since it started, with
  the tunnel port of the last. A device that was already plugged in when
  the package was installed is watched from its next plug: until then
  Windows starts it without the driver's filter. Each user can turn the
  balloon off with
  "Warn when a device is removed unexpectedly" in the View menu or, from
  `3.4.2.0`, the Settings window's "xHCI98 USB Tree" tab (the icon's
  right-click menu up to `3.4.1.0`); the count is kept either way. On Windows 98 SE
  and ME such a device is shown as disconnected, as before.
- **From `3.3.0.0`** a USB4 device on a Thunderbolt 3 controller (Alpine
  Ridge, Titan Ridge, Ice Lake), or below a Thunderbolt 3 device, reads
  "Generation: USB4 v1.0 device, running as Thunderbolt 3": its link runs
  as Thunderbolt 3 there, and the link's rate is worded as a Thunderbolt
  3 device's. On a USB4 controller it reads "USB4 v1.0" as before.
- **From `3.3.0.0`** the status bar ends "- restart Windows to finish the
  driver update" when the running `xhci98.sys` or `xhcicm.sys` is another
  version than the file the next start loads (an update not yet
  restarted into; on 98 SE and ME the new file can wait in `WININIT.INI`
  until then), and the controller's details say both versions. A driver
  from before `3.3.0.0` does not say its version; under such a driver the
  tool warns only when the installed file is `3.3.0.0` or later.
- **From `3.3.0.0`** a USB storage device on this driver's controller shows
  the transport the bus chose and why (UAS or Bulk-Only; "SuperSpeed and
  UAS", below), and the "Driver log (xhci98)" counters count the
  decisions (`XportUas` to `XportNoStreams`).
- The link rate and width of a USB4 device are read from its lane
  adapter; a Thunderbolt 3 device's come from the controller's firmware.
  For a USB device the descriptors give the SuperSpeed rates it supports;
  the negotiated lane rate is not reported.
- Each controller is named with its maker and PCI ID, and the USB
  controller inside a Thunderbolt chip says so; a root hub shows how many
  of its ports are USB 2.0 and USB 3.x, and each port its kind.
- **USB sticks and disks** are stopped from the window or the icon on
  every system, 98 SE and ME included, by their drive letter: "USB Flash
  Disk (F:)". The tool confirms what will stop, asks Windows to stop the
  device, and says when to unplug it, or which drive is in use if Windows
  refuses.
- Only a USB device with a disk on it is offered. A keyboard, mouse,
  hub, camera, Bluetooth adapter or other device is shown with its
  details, not stopped from here.
- On 98 SE and ME the stop removes the device from Device Manager; until
  it is unplugged, Device Manager's Refresh can start it again. On XP and
  later Windows holds it stopped (Code 47) until it is unplugged.
- **Logging level** (View > Logging level..., or a right click on an
  `xhci98.sys` controller, a Thunderbolt or USB4 controller or a "Driver
  log" item) shows and
  sets both drivers' log levels side by side: `xhci98.sys`'s
  `XhciLogVerbosity` in each of its controllers' driver keys and
  `xhcicm.sys`'s `XhciCmLogVerbosity` in its controller's. Each
  is `0` off, `1` counters only, `2` the log (the default; no value reads
  as `2`), `3` adds port status, `4` adds memory addresses (for
  developers). A value outside `0` to `4` is shown as it is and never
  written. On 98 SE and ME, and for an administrator, the tool writes the
  value at once; on 2000 to 7 without administrator rights it offers "Set
  as administrator...", which asks for them for that one change. Each
  driver reads its level only when it starts, so the tool then offers to
  restart Windows. The levels are also in Save Report and `XHCITR
  /report`, and `XHCITR /setlevel xhci98=<n> xhcicm=<n>` sets them from
  a command line. It replaces `XHCISNAP -verbosity` and `SNAPON.BAT`,
  which this release still carries.
- **PCI configuration**: each xHCI controller `xhci98.sys` drives, and
  each Thunderbolt or USB4 controller, has a "PCI configuration" item:
  vendor, device, subsystem, revision and class, the Command and Status
  registers, the interrupt pin and line, the BARs as read, a bridge's
  buses and windows, and the capabilities (MSI and MSI-X with their state
  and vectors, the PCI Express link now and what it is capable of, power
  management). Under a Thunderbolt or USB4 controller it lists the
  controller, its tunnel ports and, on 2000 and later, the devices behind
  each port; on 98 SE and ME those devices read "not read on this
  system". The drivers read configuration space and never change it. An
  older driver's item reads "not served by this xhci98.sys" (or
  xhcicm.sys).
- **"Can run faster" (new in `3.2.0.0`, roadmap tasks 43.27 and
  43.30).** When a USB device that declares SuperSpeed or SuperSpeedPlus
  runs slower than it can on a controller `xhci98.sys` drives - on a USB
  2.0 port, behind a USB 2.0 hub, fallen back to USB 2.0 in a USB 3 port,
  or a 10 Gbit/s device at 5 Gbit/s - or a High Speed device runs at Full
  Speed behind a Full Speed hub (one not itself behind a High Speed hub),
  a balloon names it ("A USB device can run
  faster"; the tooltip on 98 SE) and, but for a 10 Gbit/s device at 5
  Gbit/s, the icon turns red (below) until every such device is
  reconnected at its speed or removed. On Vista and later a
  balloon the shell did not show (on 7, a device's first plug, behind
  Windows' own "ready to use" balloon) is sent once more, so it shows
  after Windows' own has gone (read on 7 x86 in QEMU, roadmap task
  43.66). Clicking the balloon lists each device
  with its port and hub, the speed it runs at and the speed it can, and
  what to try; from `3.2.0.0` a click acts on the balloon on the screen,
  and opens nothing when the tool cannot tell which of its balloons that
  is (the icon's menu then leads on; roadmap task 43.72); so does the first item of the icon's menu when only a
  10 Gbit/s device at 5 Gbit/s is counted (while the icon is red that
  item opens the window on the device instead). The device's details,
  Save Report and `XHCITR /report`
  carry the same lines. A hub never counts itself (the USB 2.0 half of a
  USB 3 hub always runs at High Speed); a device behind a slow hub counts,
  and its text names the hub. "Tell me when a device can run faster" in
  the View menu and, from `3.4.2.0`, on the Settings window's "xHCI98 USB
  Tree" tab (the icon's right-click menu up to `3.4.1.0`) turns it on or
  off for you; the installer's box of the same name on the Components page, or
  `INSTALL /faster` and `INSTALL /nofaster`, sets it for the computer,
  which your own choice overrides. It is on by default on every
  Windows this package runs on. **Windows' own notices do not appear
  under `xhci98.sys`**: Explorer's "Device can perform faster when
  connected to USB 3.0" on 8, 8.1 and 10 needs Windows' own USB 3 driver
  running a controller (read in Windows 8's and 8.1's files; 10 not yet
  read), and Windows 7's "This device can perform faster" balloon for a
  High Speed device behind a Full Speed hub comes from Windows' own hub
  driver, which this package replaces (measured on 7: not raised). For
  the High Speed case the tool asks such a device once for its
  device_qualifier descriptor, which only a High Speed-capable device
  has. On a Windows 8, 8.1 or 10 computer where another USB 3 controller
  runs Windows' own driver, Explorer's line may show for the stick too.
  It reads what
  `xhci98.sys` answers; from `3.2.0.0` the driver asks a USB 3 device at
  High or Full Speed for its BOS descriptor to know it is one
  (`XhciUsb2Bos` below). Read in virtual machines on 98 SE, ME, 2000,
  XP and 7 (32- and 64-bit), roadmap task 43.28: the High Speed case,
  and the USB 3 case with a QEMU built so its stick declares USB 3 at
  High Speed. Not yet read on a computer, behind a High Speed hub, for a
  faster device at 5 Gbit/s, or on 8 and 8.1. Windows 2000's shell does
  not report a click on a balloon, so there the balloon says to click the
  icon, whose menu's first item opens the window on the device (roadmap
  task 43.68). From `3.3.0.0` (roadmap task 44.80) "Can run at" is never
  faster than this computer's USB 3 ports. The tool reads each port's
  top speed from `xhci98.sys`. A 10 Gbit/s device at 5 Gbit/s is listed
  only when one of them runs at 10 Gbit/s or more. A hub on the way that
  runs at 5 Gbit/s at most holds the device to 5 Gbit/s too. The
  device's details then say so in a note. A controller whose ports the
  tool cannot read (an older `xhci98.sys`) counts as 5 Gbit/s. Under
  `3.2.0.0` a computer whose USB 3 ports run at 5 Gbit/s at most listed
  such a device for good, and "Can run at" said 10 Gbit/s for it there.
  **Windows' own ports (from `3.3.0.0`).** By default the notice watches
  only the ports `xhci98.sys` runs. A USB 3 device on a USB 2.0 port of
  the board's own EHCI, OHCI or UHCI controller gives no notice. "Watch
  Windows' own USB ports too", under "Tell me when a device can run
  faster", turns that on for you. It is off by default. The tool then
  asks a device there for its BOS descriptor through Windows' hub driver.
  It asks once per plug, and only when the device says USB 2.10 or later.
  A device that declares SuperSpeed there is listed "on a USB 2.0 port"
  with "Connect it to a USB 3 port of this computer.", and the icon turns
  red. Where Windows' hub driver does not give the descriptor, the tool
  says nothing. A USB 3 controller Windows' own driver runs is never
  watched: Windows has its own notice there. On Windows 98 SE the tool
  reads no port of Windows' own USB controllers, so there the option
  does nothing. Read in virtual machines on XP and 98 SE (roadmap task
  44.80). ME and 2000 to 7 were not read.
- **The red icon (new in `3.2.0.0`, roadmap task 43.57).** The icon turns
  red while something is wrong that you can put right: a device of the
  "can run faster" notice above, a Thunderbolt or USB4 device waiting
  for approval, or a controller that stopped answering (a Thunderbolt or
  USB4 controller lost or failed, or `xhci98.sys` stopped on a
  controller fault). An unexpected unplug, a plug, an eject or a sleep
  does not turn it red. While red its tooltip names the first such
  thing ("and N more" for the rest), and the menu's first item or a
  double click opens the window on it. Read in QEMU on 7 x86 and 98 SE
  for the two speed cases; the approval and controller cases, ME, 2000,
  XP and 7 x64 are not yet read.

### Removing a Thunderbolt or USB4 device safely

Close the files open on the device first, as for any removable disk. On
every system `INSTALL /console /devices` shows what is connected and
`INSTALL /console /eject` removes one device safely (`XHCIINST`, or `XHCIIN64` on
64-bit Windows, in `tools\xhciinst\` takes the same switches).

- **Windows 98 SE and ME:** use the xHCI98 USB Tree or `INSTALL /console /eject`;
  only these offer a device behind a Thunderbolt or USB4 port there.
  Windows 98 SE as shipped has no "Unplug or Eject Hardware" applet, and
  the one ME has (or NUSB adds to 98 SE) does not list such a device,
  though it lists USB sticks and UAS disks. The tool lists the
  devices, asks which one, and then (from `3.2.0.0`) asks the Thunderbolt
  driver, `xhcicm.sys`, to remove it; before, the tool asked Windows
  itself. The driver asks Windows to query the device's removal (a volume
  with a file open on it refuses, and the tool says so and nothing is
  changed); once Windows has agreed, for an NVMe drive bound to the
  bundled Stor98 it asks Stor98 to stop the drive cleanly (flush and shut
  down) and waits up to about 14 seconds for its answer, then removes the
  device; the tool then says whether it can be unplugged. An NVMe drive
  that the bundled Stor98 is not running is not ejected from here, since
  it cannot be asked to stop cleanly: the tool says so, nothing changes
  (exit 1), and the tool tells you to shut Windows down before you
  unplug it. A drive that
  does not confirm its stop is not removed: it stays stopped until it is
  unplugged, the tool says "Blocked: the drive did not stop; shut down
  Windows before you unplug it" (exit 2), and the USB Tree shows the port
  as blocked. So does a removal Windows keeps refusing for 30 seconds
  (the driver keeps trying until it succeeds or the device is unplugged).
  A tool that hears no answer from the driver says so (exit 2) and does
  not claim that nothing changed. Under the USB4 connection manager the
  port's tunnel is closed after a clean removal when no other device
  behind it is running; under the Thunderbolt 3 firmware connection
  manager the tunnel stays up until the unplug. Until it is unplugged,
  Device Manager's Refresh, or the Thunderbolt driver looking at the port
  again, can start the device again.
  A drive unplugged without an eject while a program still has a file
  open on it (from `3.2.0.0`): Windows refuses to remove the drive's old
  entry, and the Thunderbolt driver asks it again - after 2 seconds,
  then less and less often down to about once a minute, and at once when the
  drive is plugged back in - until the file is closed, never forcing it; meanwhile the USB Tree shows "A
  removal is in progress" on the port, and a drive plugged back in there
  appears only once the old entry has gone (before, the old entry was
  left and could take the returning drive, which then failed). A removal
  that Windows accepts and then keeps failing for 30 seconds shows the
  port as blocked, and the driver keeps trying until it succeeds.
- **Windows 2000:** "Unplug or Eject Hardware" lists the device; stop it,
  then unplug it. Unplugged without a stop, the device is removed all the
  same and Windows 2000 shows its "Unsafe Removal of Device" dialog.
- **Windows XP, Vista and 7:** "Safely Remove Hardware" lists the device;
  stop it, then unplug it. After the stop Device Manager shows it at Code
  47 until it is unplugged. Unplugged without a stop, it is removed with
  no dialog.
- **A dock** is listed under the name of its own PCI switch's upstream
  port, a generic PCI-to-PCI bridge name; stopping it stops everything in
  the dock.
- **A device Windows does not list (Windows 2000 to 7).** Until
  `3.1.0.0`, the first time a device Windows has never seen was plugged
  in behind the tunnel, Windows installed its driver and rebuilt its
  driver stack without the filter that marks it removable, so "Unplug or
  Eject Hardware" and "Safely Remove Hardware" did not offer it (read on
  2000 and XP; 7 kept the mark on the first plug), and the same happened
  after the device was disabled and enabled or its driver updated. From
  `3.1.0.0` the Thunderbolt component installs a small service,
  `xhcimark` (`tools\xhcimark\`, run by Windows, never by hand), which
  writes the filter into the device's own driver list (its `LowerFilters`
  value) within a poll of the device appearing (about a second; ten
  seconds while the driver is not answering) and takes it out again within
  a poll of the device leaving, so the first plug, a disable and
  enable and a driver update keep the device listed. When Windows rebuilt
  a first plug's drivers before the mark was written, the service
  restarts that one device once, the way Device Manager applies a
  property change; a program with a file open on it refuses that, and the
  device is then listed from its next start. With the service stopped,
  or for such a refused device, the xHCI98 USB Tree and `INSTALL /console /eject`
  still stop it when run as an administrator: they ask Windows for the same query-remove and
  remove by the routine meant for devices that are not removable, and a
  program with a file open still refuses it. The USB Tree asks for the
  rights when it lacks them ("Stop as administrator...": User Account
  Control on Vista and 7, the Run As dialog on 2000 and XP); without them
  it says "Windows does not list this device for safe removal. Run this
  as an administrator to stop it." Pulling such a device out without
  stopping it is a surprise removal: Windows 2000 shows its "Unsafe
  Removal of Device" warning, and writes Windows still held in its cache
  can be lost, so if you have no administrator, close every program
  using the device, unplug it and plug it in once more before you use
  it. After this stop Windows may start the device again the next time
  it looks for new hardware, so unplug it soon. Read in virtual machines
  on 2000, XP and 7 x86 on 2026-10-16, with a bridge chain and no volume
  behind it.

On Windows 2000 and XP the tool needs an administrator; on Vista and 7 it
asks for elevation. `INSTALL /console /eject 2` ejects device 2 of the list after
one question, and `INSTALL /console /eject id=<instance id> /yes`, with the
instance id `/devices` prints, does it with no question, for scripts;
`tools\xhciinst\README.TXT` has the exit codes.

`INSTALL /console /devices` shows each connected device's name, generation, link,
approval and connections (PCIe, USB3, DisplayPort) as `xhcicm.sys` knows
them, and the PCI functions and Windows devices behind each Thunderbolt
port with their drivers and problem codes. The driver writes what it
knows to the registry value `XhciCmStatus` of the Thunderbolt host
interface's hardware key, and the tool trusts it only after the running
driver has answered it (`XhciCmStatusAsk` in the driver key); it ejects
only devices behind a port the driver has confirmed, and never the
Thunderbolt controller's own functions. Unique ids are shown as their
last four digits; no key material is ever written. Design record 24 is
the reference.

Under a firmware connection manager the stop ends the device's drivers
and leaves its tunnel up, since the firmware has no way to drop one
tunnel alone; the tunnel goes when the device is unplugged. Under the USB4
connection manager on Windows 2000 to 7 the stop also takes the PCI
Express tunnel down (the USB3 tunnel stays) - from `3.1.0.0` only once
the stop is confirmed: on XP, Vista and 7 by the `xhcimark` service,
which sees Windows hold the device for removal, within a poll; on
2000, whose applet leaves no such sign, only by an administrator's
`INSTALL /console /eject` (or the USB Tree run as one). Without the confirmation
the tunnel stays up until the unplug, and the tools say the driver has
not confirmed the teardown. In `3.1.0.0` the USB Tree run without
administrator rights on XP, Vista and 7 said so after every stop, as it
stopped waiting before the service could confirm (read on 7 in a virtual
machine, 2026-10-22); from `3.2.0.0` it waits up to 30 seconds for the
service and, if the tunnel is still up then, says that it has not been
seen closed and closes if the service confirms the stop. A disable, a driver update or the service's
own restart of a device is never taken for a stop, so it no longer tears
the tunnel down as it did in `3.0.0.0`.

### NVMe drives behind a tunnel

An NVMe drive in a Thunderbolt or USB4 enclosure needs an NVMe driver.
From `3.2.0.0` a tunnelled drive gets this one on each system (roadmap-usb4
43.59, the owner's decision of 2026-10-23):

| Windows | The driver of a tunnelled NVMe drive | Why |
|---|---|---|
| 98 SE, ME, 2000, XP, Vista | Stor98 | no in-box NVMe driver |
| 7 | Stor98 | 7 has an NVMe driver (`stornvme`) only with Microsoft's update KB2990941, untested behind a tunnel here |
| 8 | Stor98 | 8 has no in-box NVMe driver (none in its install image) |
| 8.1 | Stor98, put on the drive by the installer or, for a drive plugged in later, by the safe-removal mark service | 8.1's own `stornvme` (6.3.9600) holds a write for good when the drive is unplugged without Safely Remove; Stor98 fails it at once. An internal NVMe drive keeps `stornvme` |
| 10 | Windows' own `stornvme` | it ends a surprise unplug cleanly (read on 10 x86 and x64); Stor98 is opt-in per drive |

**The tunnel id (new in `3.3.0.0`).** The connection manager gives every
NVMe drive behind a Thunderbolt or USB4 tunnel port an id of its own,
`PCI\XHCI98_TUNNEL&CC_010802`: first among its compatible ids on Windows
2000 to 8, added at the end on 98 SE and ME. Device Manager shows it
under the drive's "Compatible Ids"; its hardware ids are unchanged. On
Windows 2000 to 8 Stor98's INF matches that id alone, so Windows binds
Stor98 to a tunnelled drive by itself and never offers it for an internal
NVMe drive. On 8.1 and 10 no id is given and the INF keeps `3.2.0.0`'s
model, as the table above says: those systems choose a driver again
whenever a drive restarts with ids that changed, which would take Stor98
off it again. On 98 SE and ME the id is a statement
only: their own Stor98 INF matches by the NVMe class. Where a drive
arrives without the id:

- **Windows 2000 and XP, the first plug of an enclosure never plugged in
  before, when it has a switch inside** (most real enclosures): Windows
  installs the enclosure's bridges first and the drive then has no id,
  so the hardware wizard finds no driver. Once the bridges are installed
  the safe-removal mark service restarts the enclosure, the drive comes
  back with the id, and the wizard offers Stor98 (answer its signing
  question). Unplugging the enclosure and plugging it in again does the
  same. Later plugs of that enclosure have the id at once. Once Stor98 is
  on one tunnelled drive, Windows 2000 and XP may also start it on a
  newly plugged one through their Critical Device Database, with no
  wizard; Device Manager then names that drive "PCI Device".
- **Vista to 8, a drive on a tunnel port the package does not filter**:
  the mark service puts Stor98 on it within seconds.
- With `XhciCmSafeEject` 0 there is no mark service: on 2000 and XP
  unplug and plug the enclosure again; on Vista to 8 use Update Driver.

Safely Remove first is still the recommended way to unplug a drive, on
every system. Stor98, built with this package at its version, is the
driver for Windows 98 SE to 8.1 in that table, and the download carries
it with its own licence and notes (which version, and where it is: "NVMe drives behind
Thunderbolt or USB4", under "Installing"); read them before installing
it. `xhcicm.sys` brings the drive's tunnel up and down; Stor98 runs the
drive.

`xhcicm.sys` publishes the link-state channel of Stor98's interface 4
and states on it which NVMe function is behind a tunnel when the tunnel
comes up. On Windows 2000 to 7 the channel is the kernel callback object
`\Callback\Stor98LinkState`; on Windows 98 SE and ME it is a registry
locator under `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98`
beside a key that proves it belongs to the current boot,
`HKEY_DYN_DATA\Stor98LinkState`, which `xhcicm.sys` answers for as long
as Windows runs. The bundled Stor98 reads it on every target:

- On Windows 2000, XP x64, 98 SE and ME, where `xhcicm.sys` can prove
  that the drive's link went down and the drive was reset (today only in
  its emulator), it says so on the channel, Stor98 acknowledges, and the
  drive is removed at once rather than after Stor98's own deadline of
  about 5 seconds.
- Everywhere else nothing is stated: Stor98 detects a removed drive by
  itself, at that deadline.
- On Windows 98 SE and ME Stor98 finds the channel, also for a drive
  plugged in after Windows started (it looks for its drive's entry again
  after it starts). From `3.2.0.0` an eject with the install or tray tool
  goes through `xhcicm.sys`, which asks Stor98 for a clean stop and waits
  for its answer before Windows removes the drive ("Removing a Thunderbolt
  or USB4 device safely"). From `3.2.0.0` the link-down statement is made
  there too, as on 2000 and XP x64 above: each of 98 SE and ME was
  admitted on its own reading of the statement's ordering in the
  emulator.

### The connection manager's registry values

Each value is in the Thunderbolt or USB4 controller's driver (software)
key, a `DWORD` unless it says otherwise:

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\System\NNNN` |
| 2000 to 10 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E97D-E325-11CE-BFC1-08002BE10318}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` is the controller's Device
Manager name ("Which controllers"). The install writes each value below
at its default, only where it is missing, so a value you set survives an
update: from `3.4.0.0` every one, `XhciCmTunnelBridge` as `auto`, on the
systems that read it (`XhciCmNoIrqStart`, `XhciCmBridgeNumbering` and,
from `3.4.1.0`, `XhciCmAsm4242Handover` on 98 SE and ME alone, `XhciCmRingInterrupt` on 2000 to 10, `XhciCmBridgeReq`
on 2000, XP and XP x64). `docs/using/tuning.md` has every value. From `3.1.0.0` each is also read from
`HKLM\System\CurrentControlSet\Services\USB` first, where it wins when it
is there ("Registry settings", "One value for every controller").
Restart Windows, or disable and enable the controller, after a change.

| Value | Default | What it does |
|---|---|---|
| `XhciCmMode` | `1` | `0` off: the driver binds the controller, logs it and touches nothing. `1` automatic: the firmware's connection manager's client where it runs, `xhcicm.sys`'s own on a USB4 host whose firmware runs none, and nothing where neither is certain. `2`: an Intel controller the table above does not list, driven as an Alpine Ridge, at your own risk, after adding its id to a copy of `xhcicm.inf`. `3`: force `xhcicm.sys`'s own USB4 connection manager, at your own risk; refused on Falcon, Alpine and Titan Ridge and Ice Lake, and wherever the firmware's runs |
| `XhciCmApprove` | `1` | `1` approves every device (see "Thunderbolt 3: which devices get a tunnel"); `0` approves none |
| `XhciCmSafeEject` | `1` | `1` offers devices behind a tunnel for safe removal (on 98 SE and ME only through the USB Tree and `INSTALL /console /eject`: 98 SE as shipped has no "Unplug or Eject Hardware", and ME's does not list them); `0` does not |
| `XhciCmReenum` | `1` | `1` makes Windows look at the tunnel ports after each change; `0` leaves it to Refresh, for a machine where that misbehaves |
| `XhciCmUsb3Tunnel` | `1` | USB4 connection manager: `1` builds a USB3 tunnel beside the PCI Express one where the device has one; `0` builds none |
| `XhciCmLaneBonding` | `1` | USB4 connection manager: `1` bonds the link's two lanes; `0` runs on one |
| `XhciCmPreOsCleanup` | `0` | USB4 connection manager, tunnels found at its start: `0` keeps every tunnel it can verify and holds a port it cannot; `1` cleans what it cannot verify; `2` cleans everything |
| `XhciCmNoIrqStart` | `1` | Windows 98 SE and ME: `1` starts an NVMe drive behind a tunnel whose interrupt cannot be routed with no interrupt, for the bundled Stor98 (0.4.0.0 or later; this package's is 3.1.0.0, installed by its installer) to poll; `0` leaves it at its problem code. `0` to `1`. Ignored on 2000 to 7. Written by the install on 98 SE and ME from `3.3.0.0` |
| `XhciCmBridgeNumbering` | `1` | Windows 98 SE and ME: `1` sets up the PCI bridges a device brings up behind a tunnel after Windows has started (bus numbers, memory windows, interrupt lines, as the BIOS would) (a USB4 NVMe enclosure's switch, which otherwise shows Code 29 until a restart); `0` leaves them as they come up. `0` to `1`. Ignored on 2000 to 7. Written by the install on 98 SE and ME from `3.3.0.0` |
| `XhciCmAsm4242Handover` | `1` | From `3.4.1.0`: Windows 98 SE and ME only. On an ASMedia ASM4242 USB4 host whose firmware hands USB4 over only through ACPI `_OSC`, which these systems never run, `1` sends that handover when the driver starts, once a boot, if the driver knows the firmware. Without it such a host never answers and nothing behind it works. `0` never sends it. `0` to `1`. Written by the install on 98 SE and ME |
| `XhciCmBridgeReq` | `1` | From `3.4.0.0`: Windows 2000, XP and XP x64 only. `1` rewrites the resource lists of the PCI bridges a dock or an enclosure with a switch brings, below an active tunnel port, so every port behind it is placed, and has the safe-removal mark service keep the filter on those bridges; `0` neither; `2` computes and records each list without changing it. Any other value is refused and `1` stands. The installer offers it under the Thunderbolt page's "Advanced..." on those systems, and `INSTALL /set XhciCmBridgeReq=0..2` sets it there. What it did is recorded in `XhciCmBridgeReqDiag`, a binary value in the controller's hardware key |
| `XhciCmUsb4V2` | `1` | From `3.4.0.0`, USB4 connection manager: leave it at `1`. `1` runs a USB4 version 2 router (Barlow Ridge, say) as version 2, Gen 4 links included; `0` runs every router as version 1, as `3.3.0.0` did |
| `XhciCmGen4LinkRecovery` | `1` | From `3.4.0.0`, USB4 connection manager: leave it at `1`. `1` lets the Gen 4 link between the controller and the device plugged into it recover from a link error in place, the devices behind it kept; `0` takes such an error as an unplug and a fresh plug, as before |
| `XhciCmRingInterrupt` | `0` | From `3.4.0.0`, Windows 2000 to 10: `1` lets the controller's interrupt tell the driver at once that a device was plugged in or removed, the driver still looking every `XhciCmPollIdleMs` and going back to that alone if the interrupt fails; `0` looks every `XhciCmPollIdleMs` alone. 98 SE and ME look as with `0` |
| `XhciCmNet` | `0` | From `3.4.0.0`, Thunderbolt networking: `1` lets the controller answer another computer joined by a Thunderbolt or USB4 cable and carry the network adapter's traffic; `0` answers no other computer. The installer's "Thunderbolt networking" box sets it. Its buffer and polling values are in `docs/using/tuning.md` |
| `XhciCmTunnelBridge` | `auto` (`REG_SZ`) | `auto`, written by the install (or empty): the driver finds the tunnel ports itself. A comma-separated list of `bb:dd.f` PCI bridges replaces that finding, for a machine whose ports it does not find (its log says so) |
| `XhciCmAsmediaPcie7Credits` | `0` | From `3.3.0.0` (roadmap-usb4 task 44.41): a bench experiment for the ASMedia ASM4242, leave it at `0`. `1` writes 7 credits, IFC 1 and ISE 0 into the two protocol-adapter entries of a PCI Express tunnel the USB4 connection manager builds between an ASM4242 host router and an ASM2464 device router; `0` keeps them as read; any other value is refused and `0` stands. Written as `0` by the install from `3.4.0.0`; the installer does not ask for it |
| `XhciCmPollIdleMs` | `200` | How often, in ms, the driver looks at the controller with nothing happening (`50` to `5000`) |
| `XhciCmCmdTimeoutMs` | `5000` | Firmware connection manager: the wait for a command's answer, in ms (`1000` to `60000`), three tries |
| `XhciCmApproveTimeoutMs` | `10000` | Firmware connection manager: the wait for an approval's answer, in ms, the same bounds |
| `XhciCmLogVerbosity` | `2` | The driver's log, on the same `0`-`4` ladder as `xhci98.sys`'s `XhciLogVerbosity`; read by the xHCI98 USB Tree |
| `XhciCmLogDebugView` | `0` | `1` sends the driver's log to DebugView as well, in every build |

The install tool asks "Install Thunderbolt 3 / USB4 support?": from
`3.1.0.0` Enter and `/yes` answer N whether or not a Thunderbolt or USB4
controller is present (the support is experimental; up to `3.0.0.0`
Enter answered Y with a controller present), and the tool says whether
it found one (a laptop may show its controller only while a device is
plugged in); `INSTALL /thunderbolt` includes it without asking, and N
leaves a Thunderbolt driver already installed as it is.

`INSTALL.BAT` asks about `XhciCmMode`, `XhciCmApprove` and
`XhciCmSafeEject` where a Thunderbolt or USB4 controller is present. It
never asks about `XhciCmReenum`, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding`
(each `0` or `1`) and `XhciCmPreOsCleanup` (`0`, `1` or `2`).
`INSTALL /set Name=Value` sets any of these seven.

### Where `3.0.0.0` was read

Every reading below was taken in QEMU virtual machines on development
host A, on the emulator of the `usb4-emu` project: its model of the
ThinkPad P14s Gen 1's Alpine Ridge controller (from a capture of that
machine), its Titan Ridge model, its USB4 host models (Alder Lake's, a
minimal one, and a stand-in for ASMedia's ASM4242), and its model of a
real USB4 NVMe enclosure (the ORICO enclosure's ASMedia ASM2464, from a
capture). The NVMe driver was Stor98. The one real machine
is the owner's ThinkPad P14s Gen 1 under Windows 98 SE ("Untested hosts"
above); **no other real Thunderbolt or USB4 machine has run this
release**. `docs/contributing/runs/run-39.md` and `run-40.md`
have every leg.

- **Windows 98 SE:** under the firmware connection manager, the install,
  a device approved and its NVMe drive found with no Refresh and read and
  written through Stor98, unplug and re-plug, a device tunnelled at
  boot left alone, nothing attached at boot, the controller absent with
  Thunderbolt BIOS Assist on, disable and enable, `XhciCmApprove` `0` and
  `XhciCmMode` `0`, a plug and an unplug while an Add New Hardware wizard
  was open, and `INSTALL /devices` and `INSTALL /eject` (an eject refused
  while a file was open, then accepted). Under the USB4 connection
  manager, the install, hot plug, unplug and re-plug, a cold boot with
  the device attached, reads and writes on the ASM2464 enclosure model,
  the USB3 tunnel, and the host interface taken away under the driver,
  which then stopped touching it.
- **Windows ME:** the install through `INSTALL.BAT`, and under both
  connection managers a plug, unplug and re-plug with the NVMe drive
  found with no Refresh and its data intact; Stand by and resume under
  both. ME's "Unplug or Eject Hardware" does not list the drive (98 SE
  as shipped has no such applet): the USB Tree and `INSTALL /eject` remove
  it.
- **Windows 2000:** the firmware connection manager's legs read on 98 SE,
  the open wizard aside, and the Titan Ridge model,
  safe removal through "Unplug or Eject Hardware", the USB4 connection
  manager's legs (a cold device, hot plug, unplug during I/O, pre-OS
  tunnels kept and cleaned, the USB3 tunnel, disable and enable, the
  policy values, two ports, a daisy chain left alone, the driver turned
  off) on three host models, the host interface taken away, and a
  multiprocessor guest under Driver Verifier. Two readings failed on the
  emulated platform, not in the driver: a re-plug after Thunderbolt BIOS
  Assist powered the controller off (the emulator resets the controller's
  bridges without telling Windows), and a second port on the ASM4242
  stand-in (Windows 2000 left that bridge at Code 12).
- **Windows XP, XP x64, Vista (32-bit) and 7 (32-bit):** the install with
  the filter through `INSTALL.BAT`, and under both connection managers a
  plug, unplug and re-plug with the NVMe drive found with no Refresh;
  under the firmware connection manager also safe removal through
  "Safely Remove Hardware", with no dialog after a plain unplug. These
  legs ran the `release` package (XP x64's unsigned, which it loads);
  most 98 SE, ME and 2000 legs ran the `qemu` flavour of `xhcicm.sys`,
  which logs.
- **Not yet read: Windows Vista x64 and 7 x64** with a Thunderbolt or
  USB4 controller. They load only a signed driver, and these legs wait
  for the test-signed package. Suspend and resume were read on ME only.
- **`INSTALL.BAT`:** a first install and an update from `2.4.0.0` on each
  of Windows 98 SE, ME, 2000, XP, XP x64, and Vista and 7 in both
  architectures (Vista x64 and 7 x64 with an unsigned package, started
  with F8), and on several of them an uninstall and an update from
  `2.3.1.0`.

### Thunderbolt and USB4: known limitations

- **Windows 8, 8.1 and 10: hibernation and Fast Startup with the
  connection manager installed.** Before `3.2.0.0`, read in QEMU
  (roadmap-usb4 task 43.24, `runs/run-43-24.md`), every resume from
  hibernation stopped Windows with bug check 0x9F
  (DRIVER_POWER_STATE_FAILURE), on 8 x86, 8 x64, 8.1 x64 and 10 x64. So
  did the start after an ordinary shutdown on 8.1 x64 and 10 x64, where
  Fast Startup, on by default, makes a shutdown a hibernation. The
  connection manager's port filter kept a bridge's power-on back for the
  controller's resume, and on these systems Windows does not resume the
  controller until that power-on has completed. The same guests without
  this package hibernated and resumed. On 2000 to 7 the same wait ended
  after five minutes, when the connection manager gave up waiting and
  let the power-on through. The resume then completed and the drive came
  back, so a resume took about 6 to 10 minutes in the emulator (read on 7
  x64, and earlier on XP x64 and Vista). From `3.2.0.0`, on every
  Windows from 2000 to 10, the power-on is passed on at once and the port
  is enumerated again once the controller has resumed. The legs with this
  change are task 43.24's.
- **The BIOS must leave the controller on.** A controller the BIOS
  switched off is not switched on: with "Thunderbolt BIOS Assist" on and
  nothing attached at power-on, the BIOS powers it down, and the switch
  that powers it up again is an ACPI method this driver cannot call
  (Windows 98 runs these machines in APM mode). Turn Assist off in the
  BIOS. On the integrated controllers (Ice Lake and later) the driver
  takes only the controller's own power-up step; the platform's own power
  control stays the BIOS's. A driver that finds the controller already
  forced on never switches it off.
- **ThinkPad P14s Gen 1 (BIOS 1.36): "Thunderbolt BIOS Assist" on with
  pre-boot support off hides the controller**, even with a device
  attached, so no driver can use it; in that setting a USB4 enclosure is
  unusable to any operating system (a Thunderbolt 3 dock still shows the
  controller). With Assist on and pre-boot support on, after the last
  device is unplugged the firmware stops answering and the controller is
  removed about 15 seconds later, not to return on a re-plug. Turn Assist
  off, or turn pre-boot support on and keep a device attached.
- **Windows 98 SE and ME: keep "Thunderbolt BIOS Assist Mode" Off on
  ThinkPads that have it.** Read on the ThinkPad P14s Gen 1 with Assist
  on: Windows 98 SE shows no Thunderbolt controller at all, with or
  without a device attached, and a plug brings up nothing.
- **Windows 98 SE and ME: a device with a PCI Express switch of its own**
  (a USB4 NVMe enclosure built on the ASMedia ASM2464) plugged in after
  Windows has started is set up by the driver as the BIOS would have set
  it up at power-on: bus numbers, memory windows and, where provably
  routed, the interrupt line (`XhciCmBridgeNumbering`, on by default).
  Without it Windows shows the switch with Code 29 until a restart.
  Read under the Thunderbolt 3 firmware connection manager on the
  ThinkPad P14s Gen 1 and in the emulator; the USB4 connection manager
  takes the same path but has not been read with such a device.
- **Windows 98 SE and ME: an NVMe drive behind a tunnel whose interrupt
  cannot be routed** (Device Manager: Code 29, "the BIOS for the device did
  not give it an IRQ") **starts from `3.3.0.0`, with no interrupt, the
  bundled Stor98 polling it** (`XhciCmNoIrqStart`, on by default;
  roadmap-usb4 task 44.30, design record 28 section 12). Windows 98's own
  PCI driver disables such a function once, when it first finds it; the
  driver asks Windows to configure it again, and it starts on its memory
  alone. It does so only for a drive bound to a Stor98 the installer
  installed or staged; from `3.3.0.0` the installer records that whenever
  it stages the bundled Stor98 on 98 SE and ME, with a drive attached or
  not. Read in the emulator with the interrupt withheld: on 98 SE the
  drive started, Stor98 polling, a 16 MiB file written and compared, and
  again after an unplug and re-plug, and `XhciCmNoIrqStart` `0` left it
  unstarted; on ME the drive started at the re-plug after the first
  plug's Stor98 wizard. Other devices behind such a tunnel (a dock's USB
  controller or network adapter) still need the interrupt: re-plug with
  the tunnel's bridge routed, or restart with the device attached. While
  xhcicm.sys makes its Windows 98 configuration calls, a hardware wizard left open on 98 SE or ME holds
  its other work for the tunnel ports too (plug, removal and safe-eject
  marking wait), as any such call it makes does.
- **Windows 98 SE and ME: after Safely Remove of such a device, unplug it
  before using it again.** Windows' stop closes the switch's memory windows;
  Scan for hardware changes without an unplug leaves the drive at Code 29
  until it is unplugged and plugged in again, or Windows restarts.
- **Windows 98 SE and ME: close every file on a Thunderbolt or USB4 drive,
  and every window showing it, before you unplug it** - or use Safely
  Remove first, which refuses while a file is open. A drive unplugged while
  a program still has a file open on it is left to Windows to remove once
  the file is closed (above). Closing that program afterwards (an MS-DOS
  Prompt whose copy failed, an Explorer window on the drive) moves the
  mouse between Windows and the MS-DOS Prompt, and Windows 98 SE's own
  mouse driver, VMOUSE.VXD, can then, rarely, cancel an event it no longer
  owns: every timed wait in Windows stops, the Thunderbolt driver sees no
  further plug or unplug, the drive does not come back, and on Windows 98
  SE the whole desktop can freeze (the pointer still moves). Only a
  restart recovers. Read on the emulator, in a loop built to provoke it: 4
  stops in about 110 such unplugs on the two systems before the retries
  were spaced out, and 1 in 56 after (1 in 16 on ME, none in 40 on 98 SE).
  The mouse driver's cancel was caught in a debugger on 98 SE; ME's was
  not read. It was not seen on an unplug with no file open.
- **Windows 2000: an unplug without a stop shows "Unsafe Removal of
  Device"**, and the device is removed. Stop it in "Unplug or Eject
  Hardware" first.
- **Windows 2000, XP and XP x64: the first plug of a dock or an enclosure
  with a switch** (from `3.4.0.0`, tasks 45.12 and 45.55). The first time such a device is
  plugged in, its devices may stop and start again once, a few seconds
  after it is plugged in, while xHCI98 sets up its PCI Express ports: the
  safe-removal mark service restarts it once its Found New Hardware
  windows have been answered. If a drive behind it is already in use, the
  restart is refused: unplug it safely and plug it in again instead. A
  device added later behind a port that has no I/O range (a network
  adapter behind a chained dock) is placed after the dock is unplugged
  and plugged in again.
- **Windows 2000, XP and XP x64: two Found New Hardware wizards at the
  first plug of an NVMe enclosure** (task 45.55). The first finds no
  driver: finish it either way. After the safe-removal service restarts
  the enclosure's bridges, a second offers "Stor98 Storage Controller
  for NVMe SSDs": follow it to the end.
- **Stop a dock before unplugging it when a keyboard, mouse, tablet or
  similar device is on the dock's own USB controller** (task 45.35): on
  98 SE and ME, and on 2000, XP, Vista and 7 under a firmware connection
  manager (Thunderbolt 3, and the Intel USB4 controllers whose firmware
  runs one). Stop it in the xHCI98 USB Tree or with
  `INSTALL /console /eject` ("Removing a Thunderbolt or USB4 device
  safely"); Windows' own Safely Remove Hardware may not list such a dock.
  Otherwise Windows may never finish removing the dock: it is not found
  again when plugged back, and Windows does not shut down (record 17
  section 4.13's kept transfers, below "Known limitations"). On Vista and
  7, plugging such a dock in again can then stop the computer responding;
  on 7 a dock stopped first was removed, found again when plugged back,
  and Windows shut down (task 45.49). On 2000 and XP with a USB4 host
  whose tunnels this release's connection manager builds, the unplug is
  normally handled: the connection manager reads the tunnel closed before
  this driver asks for the proof, and only in the reverse order do the
  transfers stay kept (record 17 section 4.13.6).
- **A device Windows has not seen before, or one disabled and enabled or
  whose driver was installed or updated, is missing from the safe-removal
  list** (Windows 2000 to 7): Windows rebuilds its driver stack without
  the filter that marks it removable (the first plug read on 2000 and
  XP); the next plug puts it back. The xHCI98 USB Tree and
  `INSTALL /console /eject` stop it when run as
  an administrator ("Removing a Thunderbolt or USB4 device safely"); a
  pull without a stop is a surprise removal, with Windows 2000's warning
  and cached writes at risk. From `3.1.0.0` the `xhcimark` service keeps
  the mark across those rebuilds; this stays the case for a device whose
  restart a program refused, until its next start, and while the service
  is stopped.
- **A device tunnelled before the driver started** (by the BIOS's pre-boot
  support) is offered for safe removal only after a re-plug, on Windows
  2000 to 7 (one connected while the package was installed, after a
  restart or a re-plug: "What `3.1.0.0` changes"); the xHCI98 USB
  Tree and `INSTALL /console /eject` stop it before that, as an administrator.
- **Windows 98 SE and ME: once the controller has started, `xhcicm.sys`
  stays loaded until Windows restarts**: disabling, updating or removing
  it takes full effect at the next start.
- **Windows 98 SE and ME, USB4 connection manager: a removal leaves the
  tunnel up until the unplug.** After `INSTALL /console /eject` or Device Manager's
  Remove, the device is stopped but its tunnel stays, so a Refresh can
  find it again; unplug it.
- **A USB4 host that stops answering stays stopped until the controller is
  disabled and enabled, or Windows restarts.** If the platform powers the
  controller off or resets it without telling Windows, `xhcicm.sys`
  notices within a few idle periods, reports every device behind it gone
  and touches the controller no more; it does not take the fabric up again
  by itself in this release. Under a firmware connection manager it
  resumes when the controller answers again.
- **Windows 2000: the first install of the filter takes the Thunderbolt
  switch's other devices away until the restart**, the switch's upstream
  port showing Code 12 meanwhile. `INSTALL.BAT` asks for that restart; do
  it before using the controller.
- **Windows 2000 with the "Standard PC" computer type: an NVMe drive
  plugged in behind a tunnel after the start works from `3.3.0.0`, but
  runs polled** (roadmap-usb4 task 44.53, design record 21 section 6.2.1,
  [issue 19](../issues/19-w2k-standard-pc-hot-plug-interrupt.md)). There
  Windows gives such a drive no interrupt, and before `3.3.0.0` it stayed
  at Code 10; the connection manager now gives it the tunnel port's
  interrupt so that it starts, and the bundled Stor98 polls it. Not
  affected: USB drives on this package's ports, drives attached at
  power-on, internal NVMe drives, and Windows 2000 on the ACPI computer
  type. Other non-ACPI computer types (Windows 2000's MPS ones, XP's
  Standard PC) are untested. Read on the emulator, not yet on real
  hardware.
- **32-bit Windows 10 on a board that leaves the USB4 ports' windows
  above 4 GB** (roadmap-usb4 tasks 44.72 and 45.9). Some boards leave
  these ports' memory windows above 4 GB even with "Above 4G Decoding"
  off. XHCIPFW.SYS closes those windows on 32-bit Windows 2000 to 7 and,
  from `3.4.0.0`, on 8 and 8.1 as a filter of the PCI root bus. It is not
  offered on 10, where such a filter is never started; 32-bit Windows 10
  cannot use the devices behind such ports.
- **32-bit Windows 8 and 8.1: a PCI root bus that names XHCIPFW.SYS's
  filter with its file gone stops Windows at the start** (task 45.9).
  The installer takes the filter off every root bus before it removes the
  file. If the file was deleted or damaged by other means, System Restore
  may recover such a start on 8; `tools\xhciinst\readme.txt`
  gives the manual route from the recovery environment's registry editor.
- **Windows 98 SE: answer No to the restart when an enclosure is first
  plugged in** (roadmap-usb4 task 44.78). When an enclosure is first
  plugged in after Windows 98 SE has started, Windows asks to restart
  after it installs each of the enclosure's PCI bridges. Answer No. If
  you answered Yes, Windows may skip the enclosure's bridge from then on
  and the drive does not appear. To undo it, run asd from Start, Run.
  Tick "Enumerating a device => PCI standard PCI-to-PCI bridge". Click
  OK. Restart Windows.
- **Nothing is remembered.** Approval is not stored: after a re-plug, or a
  restart without pre-boot support, a device is approved again by
  `XhciCmApprove`'s rule (see "Thunderbolt 3: which devices get a
  tunnel").
- **Approval is automatic by default, and a tunnelled device can reach all
  of memory**; set `XhciCmApprove` to `0` where the port is within reach
  of people you do not trust (same section).
- **Standby and hibernation with a tunnelled device attached are
  unsupported** on every system, and so is standby with an integrated
  controller (Ice Lake and later). Stop and unplug the device first.
  From `3.3.0.0`, on a USB4 host whose tunnels the connection manager
  builds, a tunnel it built is built again after a hibernation also when
  Windows had restarted the controller's driver since (the new instance
  took the tunnel as one it found up and did not build it again, and the
  drive was missing after the resume; roadmap-usb4 task 44.26, design record 22 section 9.4). Read on
  the emulator's tunnel bridge with a standard hot-plug controller, on 7
  x86 and 10 x86; with that change, 2000, 7 x86, 8.1 x86 and 10 x86
  resumed from a hibernation with the drive attached and its files, and
  shut down in under a minute after it (task 44.27).
  Where the tunnel port is a native PCIe hot-plug port (the integrated
  USB4 root ports, and the emulator's q35 port), Windows finds the port
  empty at the resume, before the tunnel is back, and reports the drive
  missing: files written shortly before the hibernation can be lost
  (those whose directory entries were still in Windows' cache), and the
  drive comes back only when it is plugged in again (read on the
  emulator; on the integrated root ports it is expected from the same
  mechanism, not yet read). On Windows XP x64 and 7 x64, read on the
  emulator, the first shutdown after resuming from a hibernation with a
  tunnelled NVMe drive still attached can stall at "Shutting down" for
  over ten minutes (XP x64 finished after twelve; 7 x64 was stopped after
  fifteen); 8 x64 and 10 x64 do not stall. On 7 x64, without the drive,
  or without this package, it completes, and it also completes when the
  drive was stopped with Safely Remove before the hibernation (read on 7
  only). Always use
  Safely Remove before standby or hibernation.
- **Windows 98 SE and ME: a device plugged in after the start needs an
  interrupt line from the BIOS.** On the ThinkPad P14s Gen 1 the BIOS's
  routing table covers the Thunderbolt port (read from a capture); on
  another machine, a function behind the tunnel that Windows 98 leaves
  unstarted (Code 29) needs the device attached at power-on with pre-boot
  support on. From `3.3.0.0` an NVMe drive on the bundled Stor98 is the
  exception: it starts without an interrupt (above).
- **The Ice Lake to Meteor Lake controllers under their firmware's
  connection manager have no approval message**: a device works on them
  only if the firmware tunnelled it by itself, whatever `XhciCmApprove`
  says.
- **Windows ME: a UAS drive as the first USB storage device shows Code 2**
  (unchanged; "Known limitations").
- **Stor98's own limitations** are in its readme,
  `stor98-readme.txt`.

Answered by an earlier release, and no longer a limitation: **Windows ME's
first shutdown after an install no longer hangs** (from `3.2.0.0`,
roadmap-usb4 task 43.65). On 98 SE and ME a shutdown now quiesces the
connection manager on its own thread and completes without asking the
controller to power down. Read on the ME emulator: five first shutdowns
after the Add New Hardware wizard had started `xhcicm.sys`, all clean,
where three of five had hung before.

### Thunderbolt and USB4 in Device Manager

`xhcicm.sys` names a controller by its generation, then (from `3.2.0.0`)
its maker and product, the same whichever connection manager runs it
("Which controllers", above, has each id's name):

| Device Manager shows | For |
|---|---|
| `xHCI98 TB2 Controller (...)` | Intel Falcon Ridge |
| `xHCI98 TB3 Controller (...)` | Intel Alpine Ridge, Titan Ridge and Ice Lake |
| `xHCI98 USB4 Router (...)` | Intel Maple Ridge, Tiger Lake to Meteor Lake, Lunar, Panther and Wildcat Lake, Barlow Ridge; AMD Pink Sardine |
| `xHCI98 USB4 Router` | any other USB4 host |

Up to `3.1.0.0` these were `xHCI98 Thunderbolt 2 Controller`, `xHCI98
Thunderbolt 3 Controller` and `xHCI98 USB4 Host Router`.

On Windows 2000, XP, Vista and 7 the PCI bridge ports the filter attaches
to - the controller's own switch ports or, on the integrated
controllers, its PCI Express root ports - are shown with the xHCI98
bridge names in place of the name Windows gave them:
`xHCI98 Thunderbolt PCI Bridge` on a Thunderbolt 2 or 3 controller,
`xHCI98 USB4 PCI Bridge` on a USB4 one (`xHCI98 USB4 PCI Bridge
(ASMedia ASM4242)` on an ASM4242's downstream ports, from `3.3.0.0`; its
upstream port carries no filter and keeps Windows' name), and `xHCI98
PCI Bridge` on the emulator's generic bridges. They are still ordinary PCI bridges run by
Windows's own `pci.sys`; the name says this package's filter sits on
them. Every port of one switch the filter attaches to carries the same
name, upstream and downstream alike. Windows 98 SE and ME install no filter, so their
bridges keep their names. The driver's service is
`xHCI98 Thunderbolt and USB4 Connection Manager (xhcicm)`.

### Thunderbolt 3: which devices get a tunnel, and what that means

A Thunderbolt 3 device - a dock, an enclosure, an NVMe case - reaches the
machine through a PCI Express tunnel that the controller's firmware builds
only when the operating system approves the device. Windows 98, ME, 2000
and XP have no Thunderbolt control panel, and this project does not add
one, so `xhcicm.sys` decides by one registry value, `XhciCmApprove`, in
the Thunderbolt host interface's driver (software) key:

| Value | What the driver does |
|---|---|
| `1` (the default) | Approves every Thunderbolt device the firmware announces and does not mark rejected, as soon as it is announced: plug it in and it gets its tunnel. |
| `0` | Approves nothing. A device that already has its tunnel - one the BIOS tunnelled before Windows started (pre-boot support), or one approved before the driver was disabled and enabled again - keeps it and works. A newly plugged device is logged and left without a tunnel. |

**Read this before you leave it at `1`.** A device with a PCI Express
tunnel can read and write all of the machine's memory: none of these
machines has an IOMMU that this driver or Windows uses to fence a device
off. At `1`, anything plugged into the Thunderbolt port gets that access,
whatever it is and whoever plugged it in. That is the state every device
tunnelled by the BIOS's pre-boot support is already in on these systems,
and it is what the owner chose as the default; set `XhciCmApprove` to `0`
on a machine where the port is within reach of people you do not trust.

**Nothing is remembered.** The driver keeps no list of devices, so there
is no "approve only devices I have seen before". The firmware itself keeps
a device's approval and its tunnel until the device is unplugged or the
machine is powered off - across a disable and enable of the driver, and
across standby - and the controller's own memory holds the list the BIOS
uses for pre-boot tunnelling, which this driver never changes. After a
re-plug, or a restart without pre-boot support, a device is announced
unapproved again, and the value above decides again.

The Ice Lake to Meteor Lake integrated controllers have no approve
message at all: on them a device works only if the firmware tunnelled it
by itself, whatever `XhciCmApprove` says.

`XhciCmMode` `0` turns the whole driver off without uninstalling it: it
binds the Thunderbolt host interface, logs it and touches nothing.

## What `2.4.0.0` adds

- **The silent install on Windows 98 SE and ME, off by default.** A new
  value, `XhciSilentInstall`, under the controller's driver key: at `1`, a
  device whose class is in the list at
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB\SilentClasses`
  installs with no Add New Hardware wizard; at anything else, or absent, the
  driver behaves as before. The install writes it at `0`, and an update
  keeps a `1` already there. The download's
  `tools\silent\` directory carries the list, `SILENT.REG` (hubs, boot
  keyboards and mice, Bulk-Only storage; UAS is not in it), `SILENT.VBS`,
  and `silent-on.bat` and `silent-off.bat`, which turn it on and off in one
  double-click; restart Windows after either. With no wizard, 98 SE asks
  for a missing driver file from its CD (read for a mouse and a
  keyboard). Never on 2000 and later. SweetLow
  provided the information on how the silent install is decided and the
  list. Read in QEMU virtual machines: on 98 SE a mouse, keyboard, stick
  and hub, each a device of its own, installed with no wizard and worked
  (the mouse and the keyboard after the CD prompt above), and so did a USB
  audio composite device matched by an entry added by hand; with the
  switch off, or no list, a new mouse brought the wizard as before. A hub
  needs this package's INF from `2.1.0.0` or later: under an older one its
  devnode is left with no driver. ME needs none of it
  for those: it installed a mouse, keyboard, stick and hub with no wizard
  with the switch off. On ME the switch matters only for a class ME would
  ask about (the audio device). A composite keyboard, mouse or stick was
  not read on either (no virtual device offers one), nor real hardware.
- **Controllers below xHCI 1.0 start.** `2.3.1.0` refused a controller
  whose HCIVERSION is below 1.0 (Code 10, "This device cannot start"),
  which is where the first-generation NEC/Renesas uPD720200 and uPD720200A
  (`1033:0194`) and ASMedia's ASM1042 stopped: they are xHCI 0.96 parts.
  From `2.4.0.0` such a controller starts, driven as an xHCI 1.0 one but
  for the fields Linux writes differently on 0.96 parts. A new value,
  `XhciAccept096`, written by the install at `1`, turns it off at `0`
  ("XhciAccept096", under "Registry settings"). Read in QEMU virtual
  machines, whose controller is 1.0, so no 0.96 part has run it
  ("Untested ground").
- **A finer system timer while polling, on Windows 2000 and later, off
  by default.** A polled controller was looked at no more often than
  Windows' clock ticked (10 to 15 ms on Windows 2000, XP and 7 in QEMU),
  however short `XhciPollActiveMs` was. With a new value,
  `XhciPollFineTimer`, set to `1`, the driver asks Windows for a clock of
  `XhciPollActiveMs` while it polls at that period, and gives it back one
  idle period after, when the controller sleeps, and when it stops. The
  value is `0`, off, by default: in QEMU virtual machines the finer clock
  slowed the guest's own (below, and "Untested ground"). Never on
  Windows 98 SE and ME, which have no such call ("XhciPollFineTimer: a
  finer system timer while polling").
- **The 32-bit helper is now `xhcihlpr.sys`, and 2000 and XP get it too.**
  It replaces `2.3.x`'s `xhcimsi.sys`: on 32-bit Vista and 7 it still
  connects the message-signalled interrupt, and on 32-bit 2000, XP, Vista
  and 7 it carries the finer timer. On 2000 and XP the install asks for no
  restart, and the timer, when turned on, starts at the next boot; on
  Vista and 7 an update deletes `xhcimsi.sys` at once and asks for a
  restart, after which its service is gone ("Updating from 2.y.y.y"). To remove it after an uninstall, `sc delete
  xhcihlpr`, or on Windows 2000 its key ("Windows Vista and 7:
  message-signalled interrupts").
- The finer timer has been read only in QEMU virtual machines without
  hardware acceleration, polling (`XhciInterruptMode` `2`), on 7 x64,
  7 x86 and Vista x86: while it was held the guest's clock ran at a tenth
  to seven tenths of real time and USB keyboard input was lost; with it
  off, or under an interrupt, the clock kept time and nothing was lost.
  So it ships off. It has not been read on real hardware ("Untested
  ground").
- **The root hub, the hubs and the composite devices are named under
  `USB\`.** Their device ids move from `XHCI98\ROOT_HUB`, `XHCI98\HUB`,
  `XHCI98\HUB30` and `XHCI98\COMPOSITE` (each hub and composite id with its
  `&VID_`, `&PID_` and `&REV_` forms) to `USB\XHCI98_ROOT_HUB`,
  `USB\XHCI98_HUB`, `USB\XHCI98_HUB30` and `USB\XHCI98_COMPOSITE`: a device
  id's first part names the bus that found the device, and there is no
  `XHCI98` bus (SweetLow, GitHub issue 17). Their entries move from
  `Enum\XHCI98` to `Enum\USB` in the registry. The names stay private to
  this project, so no Windows or vendor INF binds them, and the root hub's
  install no longer copies `xhci98.sys` (the controller's does). **An
  update from `2.3.x` or earlier finds each root hub, hub and composite
  device once more, and asks on XP, XP x64 and 98 SE** ("Updating from
  2.y.y.y", "From 2.3.x or earlier"). The download's `tools\xhcicln\`
  carries `XHCICLN.EXE` and `XHCICL64.EXE`, an optional tool that lists the
  old entries an update leaves behind and, on Windows 2000 to 7, removes
  them; on 98 SE and ME it lists only. The downloads up to `3.2.0.0`
  carry it. It was dropped in `3.3.0.0`.
- **Linux's controller quirks, fourteen of them, each behind a value of
  its own.** Linux's xHCI driver works around faults of particular
  controllers, named by PCI vendor and device ID; this release takes
  fourteen of those workarounds, each a new value under the controller's
  driver key with `0` off, `1` Linux's controllers (the default for all but
  `XhciShutdownReset`, which is off) and, where it is generic, `2` every
  controller, and `XhciIntervalCap` now also covers the ATI Navi10's USB-C
  controller (`1002:7316`). The install writes each at its default, and an
  update adds them. See "Linux's controller quirks".
- **Endpoint rings are kept until the controller has let go of them.**
  When a driver switches an interface to another alternate setting (an
  audio device starting or stopping, for one) and the switch fails part
  way, the driver used to hand the old endpoints' transfer rings back for
  reuse while the controller could still be using them. It now keeps every
  ring until a command proves the controller is done with it, the rule
  Linux follows; a switch the controller refuses leaves the interface
  working at its old setting; and a device that refuses the switch has
  its old setting put back. A device whose interface has only one setting
  may refuse the request outright, as USB allows: that is now treated as
  success, the endpoints' toggles reset by clearing their halts, as Linux
  does. This also bounds the endpoints the driver ever asks of the
  controller to the 64 that Intel's 7-series (Panther Point) controller
  can hold. Not read on real hardware.
- **hidusbf on a composite device's own entry.** Under Windows' own USB
  stack, a composite device - a keyboard with media keys, a keyboard and
  mouse receiver - selects its configuration once, on its "USB Composite
  Device" entry, so hidusbf's "Process Parent" sets the polling rate of
  every function there at once. Under this driver that entry
  (`USB\XHCI98_COMPOSITE`) took no such request until now, so the filter
  there did nothing; only the filter on a function's own entry worked. The
  driver's composite entry now selects the configuration through its own
  entry at its start, as Windows' does, and each function takes its
  polling rate from it, unless a filter on the function's own entry sets
  another, which wins. A change of the rate reaches a function when it
  next starts: at once on 2000 and later, where restarting the composite
  entry restarts its functions; on 98 SE and ME restart the function's
  entry too, or replug the device. SweetLow found it (GitHub issue 17).
  The composite entry's own selection was read in QEMU virtual machines
  with two USB audio devices, one with a serial number and one without:
  each selection made once, the configuration unchanged, both functions
  bound, a tone played whole from each, and the composite entry's and the
  controller's disable and enable recovering, on ME, 2000, 32-bit XP,
  XP x64 and 7 x64, and on 98 SE the functions bound (USB audio does not
  play there in QEMU). hidusbf on a composite entry is read on no virtual
  machine, as QEMU offers no keyboard-and-mouse composite device; it is
  owed to a real composite device after the release ("Untested ground";
  see "SweetLow's hidusbf").

## What `2.3.1.0` changed

A fix release: every system `2.3.0.0` supports is supported, from the same
four directories. The Setup fix was read on real hardware, the rest in
QEMU virtual machines on development host A; each item says what was read.

- **Windows XP x64 Setup from a USB stick on the xHCI controller no longer
  stops with STOP 0x0000007B** (INACCESSIBLE_BOOT_DEVICE; reported on a
  ThinkPad X230, reproduced on a Dell Latitude E7250; issue 15 in
  `docs/issues`). When the firmware is still resetting the stick's port as
  Windows takes the controller over, `2.3.0.0` gave the port up at once
  instead of waiting, so text-mode Setup never found the stick it had started
  from. The driver now waits for that reset to end, up to one second, before
  its own; the log notes the wait as `enum.port.prwait`. A reset found
  running on any port is waited out the same way, on every target, not only
  in Setup. Read on the E7250 under Windows XP x64 Setup with the USB 3.0
  controller on: text mode reached the partition screen from the stick,
  and on the release build Setup ran through to the desktop, with no
  device in Device Manager marked with a problem.
- **In text-mode Setup a disk that offers both UAS and Bulk-Only runs as
  Bulk-Only**, so Setup's own `usbstor` drives it; `2.3.0.0` chose UAS,
  for which text mode has no driver, and the disk was not seen. A missing
  `XhciForceBulkOnly` now reads as `1` ("XhciForceBulkOnly: Bulk-Only
  instead of UAS"). Every install writes `0`, so an installed system keeps
  UAS as before. Read on the same E7250, whose stick offers both.
- **Windows ME needs no USB 2.0 stack.** Not a change in the driver: the
  `2.3.0.0` package was read on a stock Windows ME with none installed,
  and the documents now say so ("Requirements", and the Windows ME row of
  "Targets and their standing").
- **After the controller is powered off for shutdown, a new request no
  longer waits for a resume that never comes.** A transfer, command or
  port reset that arrives then is refused at once, and once the
  controller is proven halted, what was still queued is failed. A
  transfer the controller might still write into is kept, as at any stop
  where that is not proven ("Known limitations"). Standby and hibernate
  are unchanged. Hardening: no hang was traced to this gap. Read in QEMU
  from the release package on Windows 98 SE, ME, 2000, XP and 7 x64,
  each shutting down twice with a mouse, a stick, a UAS disk and a hub
  with audio attached.
- **An event left waiting when the interrupt is re-armed is served at
  once**, by one more bounded pass, instead of at the driver's 100 ms
  check, which still catches one left during that pass. Seen only on QEMU's
  MSI-X, which loses such an interrupt: on Windows Vista x64 and 7 x64
  there, a UAS disk ran at about 100 MB/s where `2.3.0.0` gave about
  4 MB/s. No real controller has been seen to lose one.
- **The port switchover's scripts are in one `portsw\` directory** at the
  top of the download, beside `xhciqual\` and `xhcisnap\`, instead of a
  copy in each driver directory ("Intel 7-, 8- and 9-series chipsets: the
  port switchover"). On Windows 98 SE and ME, change into `portsw\` before
  running a batch file from a command prompt.

## What `2.3.0.0` changed

Most were read in QEMU virtual machines on development host A; the
Low-Speed mouse fix rests on host tests of that mouse's descriptors alone,
since QEMU models no such device. Where the owner's bench of 2026-10-11 read
a change on real hardware, its item says so. The sections named give the
detail, and what each rests on.

- **A controller with no working interrupt is polled, not refused.** A
  controller with no interrupt pin, or whose line the driver sees raised
  and never delivered, has its events read on a timer instead (`XhciInterruptMode`, `XhciPollIdleMs`,
  `XhciPollActiveMs`; "XhciInterruptMode, XhciPollIdleMs and
  XhciPollActiveMs: how the controller's events arrive"). Read on real
  hardware: polling by choice on a ThinkPad E460 under Windows 98 SE, and
  the move to polling of a dead line on a Dell Latitude E7250 under XP
  x64.
- **Message-signalled interrupts on Windows Vista and 7**: on x64 directly,
  on 32-bit through the package's helper `xhcimsi.sys`. The 32-bit Vista
  and 7 install asks for a restart ("Windows Vista and 7: message-signalled
  interrupts"). Read on real hardware: MSI on a Dell Latitude E7250 under
  Windows 7 x64.
- **The Intel interrupt routing repair**, for firmware that routes the
  controller's interrupt to the wrong line (`XhciIntelIrqRoute`). Read on
  real hardware: on the Dell Latitude E7250 under XP x64, and under
  Windows 7 x64 with the line asked for.
- **UAS commands of 1 MiB** where `2.2.0.0` sent 64 KiB
  (`XhciMaxTransferKB`, default `1024`), and one drive model set apart with
  Microsoft's per-device `MaximumTransferLength` (`65535` to `2097152`
  bytes) ("XhciMaxTransferKB: the UAS command size").
- **Two transfer pieces at once, off by default** (`XhciWideMap`; Windows
  2000 and later only; "XhciWideMap: two transfer pieces at once").
- **The Intel port switchover is off by default** (`XhciIntelPortSwitch`
  `0`); `portsw.vbs`, or `portsw-on.bat` and `portsw-off.bat` in one
  double-click, turns it on or off, then a restart ("Intel 7-, 8- and
  9-series chipsets: the port switchover"). Read on real hardware: the
  Dell Latitude E7250 (`9CB1`) under Windows 7 x64.
- **Selective suspend, off by default** (`XhciSelectiveSuspend`; USB 2.0
  and SuperSpeed devices; "XhciSelectiveSuspend: putting idle devices to
  sleep").
- **A composite device has an entry of its own**, and every device has
  the USB device interface ("Devices and hubs in Device Manager"). Read on
  real hardware with two USB audio devices on the Dell Latitude E7250
  under Windows 7 x64.
- **USBView shows a device's strings.** The manufacturer, product, serial
  number and interface names, and any other descriptor a tool asks for, are
  read from the device ("Devices and hubs in Device Manager"). Read on real
  hardware on the Dell Latitude E7250 under Windows 7 x64.
- **A Low-Speed mouse that carries more endpoints than it declares
  works.** A mouse (`VID_073A&PID_2230`, GitHub issue 17) whose interface
  declares one endpoint and carries two was never configured under
  `2.2.0.0` and did not move. The driver now takes the endpoints the
  interface declares and ignores the rest, as Microsoft's stack does. Read
  with that mouse's descriptors in the host tests; the mouse itself has
  not yet been read on real hardware.
- **USB audio devices with a feedback endpoint are no longer refused on
  Windows 7.** Windows 7's audio driver asks for every packet of such a
  device (an asynchronous one, such as Creative's Sound Blaster Play! 2)
  at a frame it names. On a controller without the xHCI feature that lets
  every request name a frame, such as the Intel one in a Dell Latitude
  E7250, `2.2.0.0` refused every one of those requests, so the device was sent no sound and played only hiss.
  The driver now names the frame where the controller allows it (the
  first request after the endpoint falls idle) and otherwise sends the
  request at the controller's next opportunity. Devices without a
  feedback endpoint, such as C-Media's, were never affected. Read in host
  tests and in Windows 7's audio driver's own code, and on a Dell
  Latitude E7250 under Windows 7 x64, where a Sound Blaster Play! 2 played
  cleanly at 44.1 and 48 kHz on a test build of the fix, and again on this
  release. About half the reads of its feedback endpoint end in a USB
  Transaction Error; the sound is unaffected, and the cause is open.
- **A device's speed in USBView** on Windows 98 SE and ME: the newer
  connection query that tools such as SweetLow's USBView send reads
  SuperSpeed where it is; Windows' own hub pages are unchanged ("A
  device's speed on the hub pages"), and
  `XhciFastPollFsLs` `4` polls at the rate the client asks for.
- **`XHCIQUAL` passes a controller with no working interrupt as
  `QUALIFIED WITH CAVEATS`**, exit code 3 ("Before you install: check the
  machine"). Its `--gsi` check was read on real hardware: on the Dell
  Latitude E7250 under MS-DOS 6.22 it reported the routing mismatch the
  repair fixes.
- **`XHCISNAP`'s and `XHCIQUAL`'s reports name the system** they were
  taken on, with no serial numbers or names; `XHCISNAP -nosysinfo` and
  `XHCIQUAL --no-sysinfo` skip it ("The log, and how to send one", and
  "XHCIQUAL: checking a machine's controller (optional)").
- **Driver Verifier no longer stops the machine at boot.** On 32-bit
  Windows 7 under `verifier /standard /driver xhci98.sys`, `2.2.0.0`
  stopped Windows with bugcheck C9 (0x220) as the root hub started,
  because the root hub answered a WMI request itself instead of passing it
  on; with no debugger it looked like a hang at "Starting Windows". The
  hubs now pass it on. Read on 32-bit Windows 7 under Driver Verifier on
  both drivers; 32-bit Vista and 7 x64 are still to be read.
- **Low- and Full-Speed devices behind the VIA VL805's built-in hub**
  (`1106:3483`, GitHub issue 18). The card's USB 2.0 port lists only High
  Speed in its speed table, which the xHCI specification allows for a
  controller whose built-in hub serves the slower speeds, and `2.2.0.0`
  refused every mouse, keyboard or other Low- or Full-Speed device behind
  that hub. Such a device now gets the standard speed ID. The fix was built
  without the hardware: it rests on the specification, the reporter's logs
  and what Linux, FreeBSD and Haiku do. On a VL805 it is reported working
  on `2.3.0.0` by the owner, not yet read in detail. It
  is on by default, on any controller, because without it every Low- and
  Full-Speed device on such a controller fails; set
  `XhciUsb2HubSpeedFallback` to `0` to turn it off ("XhciUsb2HubSpeedFallback:
  Low- and Full-Speed IDs behind a hub").
- **A lost interrupt can no longer stop Windows 98 SE.** In `2.2.0.0`, an
  interrupt the controller raised and Windows never delivered was picked
  up by the driver's own thread. Windows 98 SE could leave that thread
  waiting while a copy to a USB drive in MS-DOS
  compatibility mode waited for the very transfer the thread would pick
  up. Such a drive is one the computer's BIOS presented at start, the "E:"
  of a stick plugged in at power-on. Both waited for ever, and the
  computer stopped. One lost interrupt in the middle of such a copy was
  enough, in the default `XhciInterruptMode` `0`. On a controller whose
  interrupt never arrives at all, with `XhciInterruptMode` `1`, it
  happened on the first copy. The pickup now runs from the driver's
  100 ms timer instead, which kept running when the thread did not. Read
  in QEMU on Windows 98 SE, with lost interrupts made on purpose ("Controller
  faults: what the driver does about them").
- **Every registry value the driver reads is in its key after an
  install.** The install writes all nineteen, now including `XhciWideMap`
  `0`, `XhciSelectiveSuspend` `0` and `XhciUsb2HubSpeedFallback` `1`, each
  the value a missing one reads as, so nothing behaves differently; an
  update keeps a value you set ("Registry settings").
- **A UAS drive keeps working after a transfer error on its data.** A
  USB Transaction Error on a UAS drive's data or status stream is now
  retried in place, as on any other bulk endpoint ("Controller faults:
  what the driver does about them"); before, the read or write failed and
  the drive was reset. And after a reset of the drive, the driver starts
  each of its streams afresh. Read in QEMU on development host A with the
  error made on purpose; a real drive's error has not yet been read on
  real hardware.
- **UAS in QEMU: a workaround for an emulator defect.** QEMU's xHCI (read
  at QEMU 11.1.0, `hw/usb/hcd-xhci.c`: `xhci_find_stream` calls
  `xhci_ring_init`, which forces the consumer cycle to 1) ignores the cycle
  bit a UAS stream's context carries whenever an endpoint is stopped, reset
  or reconfigured, so a stream whose ring had wrapped an odd number of
  times stopped moving data: a UAS disk in a QEMU guest could then hang
  after any transfer error, cancel or reset until it was removed and added
  again. On QEMU's xHCI alone (PCI ID `1B36:000D`) the driver now gives
  each stream its cycle back after such a command. Real controllers read
  that bit and never take this path.

## Requirements

| | |
|---|---|
| Operating system | Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows ME (4.90.3000), 32-bit Windows XP (SP3), Windows XP x64 (SP2), and Windows Vista (SP2) and Windows 7 (SP1) in both architectures; and Windows 8, 8.1 and 10 (builds below 22000) in both architectures, where the installer installs the Thunderbolt 3 / USB4 connection manager by default and the USB drivers only when ticked, with Secure Boot off. 64-bit Vista to 10 load the drivers in test-signing mode or while driver signature enforcement is disabled (F8 on Vista and 7). Windows 11 is refused |
| USB stack | None. The driver replaces the port and hub drivers on every target. Windows 98 SE needs no USB 2.0 stack for the controller, hubs, HID or audio; **storage needs NUSB's mass-storage component** (next section). Windows ME needs no USB 2.0 stack either, and SweetLow's, if installed, is not used. Do not install NUSB on Windows ME, 2000, XP, Vista or 7 |
| Controller | An xHCI controller presenting PCI class code `0C0330`, with a BAR0 mapped below 4 GB. Since `2.3.0.0` an interrupt pin is no longer required: a controller reporting `Interrupt Pin = 0` is polled, as is one whose line the driver sees raised and never delivered (another kind of dead interrupt needs `XhciInterruptMode` `2`), and on Windows Vista and 7 a controller may use a message-signalled interrupt instead of its pin ("XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs"). Up to `2.2.0.0` such a controller could not be driven at all. A controller with no USB 2.0 protocol port at all is accepted |
| Install media | Windows 98 SE on an xHCI-only machine: the Windows 98 SE installation CD at hand, or the Windows CABs on the hard disk. Windows ME: the same, from the Windows ME CD or the CABs its Setup leaves on the disk. The NT targets take what they need from their own driver cache and ask for nothing |

Run the qualifier before installing anything; it answers the controller
conditions in a single read-only pass.

## Windows 98 SE and ME: the stock install, and storage

**Windows 98 SE needs no USB 2.0 stack.** On a stock Windows 98 SE install,
with neither NUSB nor SweetLow's stack, the driver installs, starts the
controller and the root hub, and drives hubs, HID devices and composite audio
devices through Windows 98 SE's own class drivers. The install asks for the
Windows 98 SE CD for Windows' own `usbd.sys`, and the first HID device asks
for it again for `hidclass.sys` (see "Installing"). If NUSB or SweetLow's
stack is installed, its port and hub drivers are simply not used.

**Windows ME needs no USB 2.0 stack either.** On a stock Windows ME install,
with no USB 2.0 stack, the driver installs with no file asked for and a
restart, and then drives hubs, HID devices, composite audio devices and
storage through Windows ME's own class drivers. SweetLow's stack, if
installed, is likewise simply not used.

**Storage on Windows 98 SE needs NUSB's mass-storage component**, for
Bulk-Only and UAS devices alike. Windows 98 SE has no USB mass-storage
driver, and a drive letter there comes from a mapping layer that this
package does not replace. The component is seven files, which NUSB 3.3 and
3.6 install:

- `USBSTOR.INF` and `USBSTOR.SYS`, the USB storage driver;
- `USBAUTH.SYS` and `USBU2A.SYS`, two filters `USBSTOR.INF` copies with it
  and installs for a few card readers and older devices;
- `USBNTMAP.INF` and `USBNTMAP.SYS`, which map a USB disk to a drive letter;
- `USBMPHLP.PDR`, the mapping port driver.

They are Microsoft's, apart from `USBU2A.SYS`, a third party's. SweetLow's
stack alone has no storage part.

**For the "Unplug or Eject Hardware" icon on Windows 98 SE, install NUSB
3.3.** Windows 98 SE has no such icon of its own (its `SYSTRAY.EXE` stops
PC Cards only). The applet is Microsoft's hotfix KB 242975, which NUSB 3.3's
own setup installs; the seven files above do not include it, and neither
does SweetLow's stack. With NUSB 3.3 installed, the icon lists USB sticks
and UAS disks under `xhci98.sys`, as Windows ME's own does: Stop gives "Safe
To Remove Hardware", and a disk with a file open is refused until the file
is closed. Neither lists an NVMe drive behind a Thunderbolt or USB4 tunnel:
remove that with the xHCI98 USB Tree or `INSTALL /console /eject`. The USB Tree's
Safely Remove also stops USB storage on both systems.

**From `3.2.0.0` the download carries them, and the installer offers
them on Windows 98 SE** (roadmap-usb4 tasks 43.3 and 43.4). They are NUSB
3.3's copies, unmodified, in a folder of their own, `USBDRV\NUSB33\` (the
files in `USBDRV\NUSB33\W98SE\`; `USBDRV\` also holds the other USB
drivers' sets, `FREE\` and `NONFREE\`), with a `NOTICE.md` saying whose
they are; they are not this project's work, and no licence to
redistribute them has been established. The installer's storage row is
on Windows 98 SE only; whether it starts ticked depends on what it finds
of the seven and of NUSB, and on an earlier run's choice
(`tools\xhciinst\readme.txt`), and `/storage` or `/nostorage` decides
it. UAS disks under `xhciuas.sys` need the files as well as
Bulk-Only ones. A download built without them says so in its
`readme.txt`. The routes below work without the installer.

- **Without them**, a Bulk-Only stick has no driver at all: "Unknown Device",
  Code 28. A UAS device installs "xHCI98 USB Attached SCSI Storage" and then
  stays at Code 2 ("The NTKERN.VXD device loader(s) for this device could not
  load the device driver"), because `xhciuas.inf` names `USBNTMAP.SYS` as a
  filter on the device; a restart does not change that.
- **With NUSB 3.3 or 3.6 installed**, storage works.
- **The seven files can also be installed on their own**, without NUSB's USB
  2.0 stack: point the Add New Hardware Wizard, or Update Driver on the
  device, at a folder holding them. **First copy `USBNTMAP.INF` into
  `C:\WINDOWS\INF`, before the first USB disk is plugged in.** The wizard
  takes `USBSTOR.INF` from the folder but not `USBNTMAP.INF`, and on a stock
  Windows 98 SE the disk is then taken by Windows' own `NTMAP.INF`, which
  copies `NTMAPHLP.PDR` from the Windows CD; Windows stops with "Invalid VxD
  dynamic link call from NTMAPHLP" and hangs - even with all seven in the
  folder. With `USBNTMAP.INF` in `C:\WINDOWS\INF` first, the disk installs
  as "USB Disk" with a drive letter and no restart. The installer's storage
  row places and removes the two together, and says so, with a step, if it
  finds `USBSTOR.INF` left in the INF folder without `USBNTMAP.INF`. After
  that Bulk-Only and UAS storage devices work with no extra step, and a UAS
  device that sat at Code 2 starts at the next boot. On a Windows 98 SE that
  still held NUSB's other files (`IOS.VXD`, `DISKTSD.VXD`, `NTMAP.SYS`), a
  disk installed before `USBNTMAP.INF` was in place goes to Windows' generic
  "Disk drive" with no drive letter instead; Update Driver on it, pointed at
  the same folder, installs "USB Disk", then unplug it and plug it back in.
  A stock Windows 98 SE needs none of NUSB's other files: the seven alone
  carry USB sticks and UAS disks.

**Windows ME ships its own mass-storage files.** Windows ME carries its own
`USBSTOR` and `USBNTMAP` INFs, and the files are copied from Windows ME's
CABs when the first stick installs. Bulk-Only storage works on them, and so
does UAS once they are there. A UAS drive plugged in before any ordinary
stick shows Code 2 until they are: plug in any ordinary USB stick once, then
unplug the UAS drive and plug it back in ("Known limitations").

**The UAS driver on Windows 98 SE installs with no restart.** Its INF
copies no file in use, so the first UAS device installs and works at once.

## XHCIQUAL: checking a machine's controller (optional)

`XHCIQUAL.EXE` is a DOS tool that reads the machine's xHCI controller and says
whether the driver can work on it. Run it with no arguments for a read-only
quick scan. It writes no PCI configuration register and prints one of four
verdicts:

| Verdict | Means |
|---|---|
| `LOOKS QUALIFIED` | nothing a read-only pass can see disqualifies this machine; the active tests still decide |
| `LOOKS QUALIFIED WITH CAVEATS` | the same, but the controller has no line interrupt (`Interrupt Pin = 0`): the driver polls it, or an MSI-capable OS takes a message interrupt instead; the line says whether it offers MSI |
| `DISQUALIFIED` | something a read-only pass genuinely sees: no `CC_0C0330` function, BAR0 unusable or above 4 GB, or no USB 2.0 protocol ports |
| `CANNOT SAY` | a state this pass may not change is in the way: the controller is not in D0, or Memory Space Enable is clear |

The qualifier is the same program the `1.x` releases carried, and its last
criterion, no USB 2.0 protocol ports, is the miniport's; see the Controller
row of "Requirements".

The full active run (`xhciqual/hardware-testing.md`) ends on `QUALIFIED`,
`NOT QUALIFIED`, or, since `2.3.0.0`, **`QUALIFIED WITH CAVEATS`**: a
controller with no working interrupt - no interrupt pin, or an interrupt that
never arrives while the controller's events reach memory - that passes the
rest when polled. It is not refused, because the driver polls such a
controller: with no pin under `XhciInterruptMode` 0, automatic; with an
interrupt that never arrives under `0` only where the driver sees the
controller raise it, otherwise under `2`, which the verdict names; at the cost of more CPU
wake-ups and up to the poll period of event latency; or an MSI-capable OS
takes a message interrupt, where the controller offers one (Windows Vista and
7: x64 directly, 32-bit with the `xhcihlpr.sys` helper, `xhcimsi.sys`
before `2.4.0.0`). The verdict prints
which applies. Its exit code is 3 (0 qualified, 1 not qualified, 2 a usage
error or no controller).

A passing interrupt test under DOS proves the line in the routing DOS
uses, which is not always the routing Windows uses. On the Dell Latitude
E7250 the test passed on IRQ 11, while under Windows XP x64 and 7 the same
line never reached the interrupt Windows assigned. `--gsi N`, beside
`--quick` or `--probe-only`, compares the Intel chipset's routing with N,
the interrupt Windows reports for the controller (Device Manager,
Resources tab), and on the E7250 it reported the mismatch ("XhciIntelIrqRoute: the Intel interrupt routing
repair").

Since `2.3.0.0` the report's header also names the machine it ran on: the
make, model and BIOS from the firmware's SMBIOS table, the processor, the
memory and the DOS version. It never prints a serial number, a UUID, an
asset tag, a SKU or a part number, and withholds any other string that repeats one, so a
report can be pasted into a public issue as it is. The quick scan shows
two summary lines on the screen and the whole block in its log. If reading
these details causes trouble on a machine, add `--no-sysinfo`: the header
then says they were skipped, and the verdict and every test are the same.

It is in the download's `tools\xhciqual\` directory (before `2.4.0.0`,
`xhciqual\` at the top of the download). **It must be run from real
DOS**, not a DOS box inside Windows, booted without EMM386 or any other V86 or
paging memory manager. `HIMEM.SYS` is allowed, and on some machines it is
needed: if the tool will not run at all on a boot that loads nothing, add
`DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` to `CONFIG.SYS` (adjusting the path)
and try again. `xhciqual/hardware-testing.md` has the staged active tests,
the safety notes, and how to read each result.

From `2.4.0.0` the same directory carries `QUALREP.BAT`, the one command a
bug report asks for: it runs `XHCIQUAL --probe-only --no-page --log-stamp`,
the read-only probe, and saves it in the current directory to a new log
named from the DOS date and time, `mDDhhmmS.LOG` (the month as one
character, `1` to `9` then `A`, `B`, `C`; the day, hour and minute; then the
first letter `A` to `Z` not yet taken), so a second run never overwrites the
first. Attach the `.LOG` it names. `XHCIQUAL` refuses `--log-stamp` before
reading anything, with exit code `4`, if the DOS clock does not read as a
date, all 26 names of that minute are taken, or the log cannot be created.

## Installing

From `3.4.0.0` the drivers are in one folder per Windows family: `98-me\`
(98 SE, ME), `nt-x86\` (32-bit 2000 to 10) and `nt-x64\` (64-bit XP x64 to
10), each holding only what its Windows installs. At its root: `xhci98.inf`
and `xhci98.sys`, the host controller driver, and `xhciuas.inf` and
`xhciuas.sys`, the UAS driver; in `nt-x86\` also `xhcihlpr.sys`, the helper
the INF installs on 32-bit Windows 2000, XP, Vista and 7; in `nt-x86\` and
`nt-x64\` `txtsetup.oem`, which only Windows 2000 and XP Setup read (below),
so that folder's root files are the F6 floppy. `cm\` holds `xhcicm.inf` and
`xhcicm.sys`, the Thunderbolt and USB4 driver, with XHCIPFW where it is
offered and, on NT, the tunnel-port filter's INF as `xhcicmf.inx`, which
`SETUP.EXE` installs and integration tools skip; `stor98\` holds Stor98.
`nt-x64\` also holds `xhci98-test.cer`, the public half of the certificate
the x64 drivers are test-signed with, and a catalog beside each INF.
`INTEGRATE.TXT`, at the download's root, says which folder to give nLite,
NTLite or DISM. Up to `3.3.0.0` the download had a flavour directory each
for release and debug builds of each architecture (`release-x86\` and the
others), the filter's INF in their `nt\` subdirectory. The Intel port
switchover's script, `portsw.vbs`, and `portsw-on.bat` and `portsw-off.bat`,
which turn the switchover on or off through it in one double-click, are not
part of the install. From `2.4.0.0` they are in the download's
`tools\portsw\` directory, one copy for the whole download; in `2.3.1.0`, in
`portsw\` at the top of the download; in `2.3.0.0`, at the root of each
flavour directory. No Microsoft file is in the package; the Windows 98 SE
storage files that the download carries from `3.2.0.0` are beside it, not in
it ("Windows 98 SE and ME: the stock install, and storage").

1. Install the prerequisite, where there is one: on Windows 98 SE, NUSB 3.3
   or 3.6 if you want USB storage (above). On a fresh Windows 98 SE install
   the order is NUSB, then this driver, then the first storage device.
2. In Device Manager, find the unrecognised xHCI controller, usually under
   "Other devices", and update its driver from the family folder
   (`98-me\`, `nt-x86\` or `nt-x64\`):
   - Windows 98 SE and ME: Properties, Driver, Update Driver, Specify a
     location. If Windows finds the controller first, the Add New Hardware
     Wizard asks the same question; give it the same directory.
   - Windows 2000 and XP: Properties, Driver, Update Driver, Have Disk. XP
     shows its unsigned-driver warning; choose Continue Anyway.
   - Windows Vista and 7: Update Driver Software, Browse my computer for
     driver software. If Windows warns that it cannot verify the publisher,
     install the driver anyway. Use Device Manager rather than right-clicking
     the INF.
3. It installs as "xHCI98 USB 3 Controller" ("xHCI98 USB 3.x eXtensible Host
   Controller" up to `3.1.0.0`) with "xHCI98 USB 3 Root Hub" ("xHCI98 USB
   3.x Root Hub" up to `3.2.0.0`) beneath it; the installer adds the maker
   and product, a hand install does not. Restart if asked: Windows 98 SE
   asks once, after the controller's install, and 32-bit Vista and 7 ask,
   for the helper, holding the controller (Code 28 on 7, Code 1 on Vista)
   until the restart. From `2.4.0.0` 32-bit Windows 2000 and XP install the
   helper too, with no restart asked for: the controller runs at once, and
   the helper's finer timer ("XhciPollFineTimer: a finer system timer while
   polling"), off by default, can start from the next boot. A reinstall on
   32-bit Vista and 7 (the controller removed in Device Manager and found
   again) needs the restart too, whether or not Windows asks: 32-bit Vista
   showed only an "installed successfully" balloon and held the controller
   at Code 1 until a restart. After any restart asked for, neither entry
   should carry a warning mark.
4. The first UAS device asks for the UAS driver; point it at the same
   directory. On Windows 98 SE, ME, 2000, XP and Vista a New Hardware wizard
   asks; Vista's names the device by its own product string, and it becomes
   "xHCI98 USB Attached SCSI Storage" once installed. Windows 7 shows no
   wizard: it searches Windows Update, then reports that the device was not
   installed. Install it from Device Manager on the device's entry: Update
   Driver Software, Browse my computer for driver software, the same
   directory.
5. An external hub installs as "xHCI98 USB Hub" from the driver already
   installed. On Windows 98 SE and 2000 it installs with nothing to answer
   (Windows 98 SE shows its "New Hardware Found" box briefly). On Windows XP
   every newly plugged hub brings the Found New Hardware wizard and the
   unsigned-driver warning, as the controller's install did; choose Continue
   Anyway.

**The files Windows supplies.** Two files the install needs are Windows' own
and are not in the package: `usbd.sys`, whose helper routines the class
drivers above this driver call, and `usbui.dll`, which draws the USB
property pages. On an xHCI-only machine Windows never placed them, so the
INF asks Windows to copy each from its own installation source, only if it
is absent. On Windows 98 SE that is an "Insert Disk" prompt naming the
Windows 98 Second Edition CD-ROM, unless the Windows CABs are on the hard
disk; insert the CD and click OK, and if it then asks where to copy from,
give it the CD's `WIN98` folder. The first HID device can raise the same
prompt for `hidclass.sys`, Windows' own HID class driver. The NT targets
take them from their driver cache with no prompt, but for one: on Windows XP
x64 and Server 2003 x64 the install fetches `usbd.sys` alone (from
`3.4.0.0`), so there the controller's Advanced tab and the root hub's Power
tab appear only where `usbui.dll` is already present, placed by another USB
driver, or, from `3.4.2.0`, by `SETUP.EXE` (or `INSTALL.BAT`), which copies
it from Windows' own installation files when it is missing and a
controller runs on this driver; an install from the INF alone, such as
Update Driver or the F6 floppy, still does not. Every Windows Vista to 10
install already has both. The driver needs no `usbport.sys` and no
`usbhub.sys` on any target, and the install copies neither.

Windows 2000 note: the first Update Driver after a cancelled Found New
Hardware Wizard was once seen to leave the controller with no driver; a
second Update Driver filled it.

### Installing, updating and uninstalling with INSTALL.BAT (new in `3.0.0.0`)

From `3.1.0.0` a normal install, update or uninstall is the installer's
window: double-click `SETUP.EXE` at the top of the package to install or
update, and use Remove in Add/Remove Programs (Programs and Features on
Vista and 7) to uninstall; or install by hand through Device Manager
("Installing", above, and the update steps below). What follows describes
the console tool `INSTALL.BAT` runs, which `3.0.0.0` introduced and which
stays for command-line and scripted installs; `tools\xhciinst\readme.txt`
is its reference.

INSTALL.BAT was beta in `3.0.0.0`. If it fails, or you prefer, install
through the window or through Device Manager ("Installing", above, and the
update steps below).

From `3.0.0.0` the download carries `INSTALL.BAT` at its top level and the
program it runs in `tools\xhciinst\`: `XHCIINST.EXE` for 32-bit Windows and
`XHCIIN64.EXE` for 64-bit Windows, which `INSTALL /console` chooses, with
the family folder of the running Windows (`98-me\`, `nt-x86\` or `nt-x64\`).
Unzip the whole download first: the program refuses to run from anywhere but
its own place in an unzipped package, and refuses a driver directory whose
files are not all this release's, this architecture's and this build's. Then
run `INSTALL /console` from the top directory, at a command prompt: on
Windows 98 SE and ME change to that directory first; on Windows 2000 and XP
run it as an administrator; on Vista and later from a Command Prompt run as
administrator. Run with no argument, or double-clicked, `INSTALL.BAT` opens
the installer's window instead (from `3.1.0.0`).

It shows the devices it found and what it will do with each, asks about a
few of the driver's options (below), each choice on its own line and
"Enter for [x]" saying what Enter keeps, asks for the install directory of
its tools (`C:\XHCI98` unless another is chosen, or `INSTALL /console /to
<path>`), asks once (`Y`, or Enter), and then:

- **Windows 2000, XP, XP x64, Vista and 7**: it installs or updates every
  driver of the package on the devices present, with the routine Windows
  documents for a driver package's own installer
  (`UpdateDriverForPlugAndPlayDevices`) - the controller first, then its
  root hub, hubs and composite entries, then UAS disks, then a Thunderbolt
  or USB4 controller and its tunnel ports - and stages the package's INFs
  (all but one whose driver is a newer release, which it leaves) so a device
  plugged in later finds its driver. It copies the package to a folder of
  its own under `XHCI98` in the Windows directory first and installs from
  there, so the files a later device needs are on the hard disk. On Windows
  2000, XP and XP x64, if an earlier install of the same release was made by
  hand from a CD or a stick, Windows may still look there for a later
  device; keep that medium at hand. It leaves alone any controller or disk
  another vendor's driver runs, never replaces a newer release unless told
  to (`/force`), and does what the update steps below otherwise ask of you:
  Windows 2000's Have Disk, the root hub's own update, and on Vista and 7 a
  build with the same `DriverVer` (it says so, and `/restage` replaces the
  stored one). Windows' own unsigned-driver questions still appear; answer
  them. It says when to restart, and when to run it again after the restart
  (32-bit Vista and 7 after an update from `2.3.x`, for the new root hub).
  On XP and XP x64 after an update from `2.3.x` or earlier, it stops while
  Windows' Found New Hardware windows wait for an answer; answer them with a
  keyboard and mouse that do not go through this driver, then run it again.
  On Vista x64 and 7 x64 it says whether it could confirm test-signing mode,
  and never changes it. The Thunderbolt and USB4 tunnel-port filter is
  `cm\xhcicmf.inx` in `nt-x86\` and `nt-x64\`, beside `xhcicm.inf` and
  `xhcicm.sys`, and the tool installs it from there; `98-me\` does not carry
  it.
- **Windows 98 SE and ME**: it copies the package's drivers into the install
  directory itself (`C:\XHCI98` by default; on these systems a path of short
  names with no spaces), adds that folder to `DevicePath`, the list of
  places Windows' hardware wizards search, and from `3.1.0.0` installs each
  device's driver through Windows' own device installer with a small helper,
  `XHCIDI.EXE` (`3.0.0.0` printed the Device Manager steps instead). A
  Windows dialog may open (the Windows CD for `usbd.sys`); answer it. A
  device the helper cannot install gets its Device Manager steps printed as
  a short numbered list with the folder to type. Run it again after the
  restart to see the result and set the options. A USB 3 controller that
  appears after the install (a card added later, say) brings Windows'
  hardware wizard, which finds the driver in the install directory and asks
  once for the "xhci98 xHCI Host Controller Disk": type the install
  directory (`C:\XHCI98` by default) and it finishes. That one prompt is how
  the install works on these systems, not a fault. The first plug of a
  Thunderbolt or USB4 NVMe drive asks for no disk: the installer puts
  Stor98's files in place beforehand.
- **Windows 8, 8.1 and 10** (10 from `3.2.0.0`, every build below 22000;
  Windows 11 is refused, having a USB4 connection manager of its own): the
  connection manager through the same route as on Vista and 7, with the USB
  drivers unticked unless you tick them (`/usb`), then with a warning, and
  the bundled Stor98 ticked with the Thunderbolt support on 8 and 8.1
  (`/nostor98` leaves it out) and unticked on 10, where Windows' own
  `stornvme` stays (`/stor98`; roadmap-usb4 task 43.59). Windows Server
  2016, 2019 and 2022 report a Windows 10 version and are treated as 10. On
  the x64 editions the test-signed drivers load in test-signing mode, which
  these systems let you turn on only with Secure Boot off in the computer's
  firmware setup, or on a start with driver signature enforcement disabled;
  the tool never changes either. It installs the connection manager there
  only in test-signing mode, and a start with enforcement disabled is not
  enough: 64-bit 8 to 10 make it a boot driver when they install its filter
  (roadmap-usb4 task 44.32), and under `3.2.0.0` a start without
  test-signing mode then stopped 64-bit 8 in Automatic Repair (task 43.22);
  from `3.3.0.0` the tool sets it back to start on demand at the end of its
  run ("What this is"). The x64 package carries a catalog for each driver
  (from `3.2.0.0`), signed with the same test certificate: these systems
  refuse a 64-bit driver package without one in test-signing mode (error
  `0xE000022F`; design record 23 section 5.12), so keep the `.cat` files
  beside the INFs. From `3.3.0.0` (roadmap-usb4 task 44.25), where it reads
  Secure Boot as on, the tool refuses before any change a run with the USB
  drivers (Windows would load none of them, and every USB 3 controller would
  stop working) and the connection manager on 32-bit 8, 8.1 and 10 (Windows
  would not load it, and it is a boot driver while Windows installs its
  filter), as it already refused the connection manager on the x64 editions;
  on 64-bit 10 it reads whether memory integrity (Core isolation) is turned
  on and, if it is, says that Windows refuses a driver it finds incompatible
  with it. Where Intel's Thunderbolt driver runs the Thunderbolt controller
  (task 44.28), the Thunderbolt support is unticked and the console asks, N
  the default; ticked (`/thunderbolt`), it replaces Intel's driver on that
  controller after a warning, the uninstall giving the controller back to
  Intel's driver (Windows Update may also put Intel's back).

At the end, if an update from `2.3.x` or earlier left old `XHCI98\`
entries, it says so in one line. They do no harm, and nothing needs
them removed ("From 2.3.x or earlier").

**The options it asks about** are a few of the "Registry settings" below,
each only where the hardware or the Windows it affects is present:
`XhciIntelPortSwitch` (only with an Intel 7-, 8- or 9-series controller),
`XhciSilentInstall` (Windows 98
SE and ME only, off by default; on does exactly what
`tools\silent\silent-on.bat` does, off what `silent-off.bat` does, and the
question says to restart after), and
`XhciCmMode`, `XhciCmApprove` and `XhciCmSafeEject` (only with a
Thunderbolt or USB4 controller). From `3.2.0.0` it no longer asks
`XhciInterruptMode` or `XhciForceBulkOnly`: set them with `/set
Name=Value`, or in the window's Advanced settings. Each shows the value now, the default
and the values allowed, with a warning where the setting has one; Enter
keeps the value, and only an answer changes it. An answer applies from
the driver's next start: on a new Thunderbolt install the first start runs
with the defaults, so devices connected before the restart are approved.
`/defaults` asks nothing, and `/set Name=Value` sets a value without
asking. `XhciPollFineTimer` and `XhciSelectiveSuspend` are never asked;
`/set` still sets them. **At the end** it says "Changes were made. Please
restart Windows now." whenever it changed anything, on every Windows, and
"No changes were made." when it did not.

**Switches** (each after `/console`): `/list` shows what it would do and
changes nothing (no log either); `/yes` skips the confirmation; `/uninstall`
removes the drivers (below); `/force`, `/restage`, `/nopause` and `/log
path` are in `tools\xhciinst\readme.txt` (`/debug` is gone from `3.4.0.0`,
with the debug builds). **Exit codes**: 0 done; 1 refused or cancelled,
nothing changed; 2 something failed (the screen and the log say what was
done); 3 done, restart Windows; 4 done as far as it can, with the steps left
printed. **The log**: every run but `/list` adds to `XHCIINST.LOG` at the
root of the system drive; attach it to a report about an install.

**Uninstall**: Remove in Add/Remove Programs (Programs and Features on Vista
and 7) warns first that on a machine whose only USB controller is xHCI the
USB keyboard and mouse stop working until another driver is installed, and
goes on only when you tick that you have another keyboard and mouse or
accept it (the console, `INSTALL /console /uninstall`, asks for the word
`REMOVE`). On Windows 2000 to 10 it removes, in order and stopping at the
first step that fails or needs a restart (run it again after the restart):
this package's filter from a Thunderbolt port (the port goes back to a
suitable Windows or vendor driver), every device entry of the package, its
INFs, its services and its driver files, and its copies of the package. It
never removes Windows' own `usbd.sys` and `usbui.dll`, which Windows
supplied and other USB drivers use. On Windows 98 SE and ME it removes every
device entry through the helper, then the package copy and its search path,
the INF copies Windows kept and the driver files. On Windows ME, wait a
minute after you pull out a USB stick before you uninstall the drivers.

### Installing Windows 2000 or XP, or using the Recovery Console: the F6 floppy

From `2.1.0.0` the package carries `txtsetup.oem` (from `3.4.0.0` at the
root of `nt-x86\` and `nt-x64\`), for a machine whose keyboard or install
medium is on an xHCI controller. Copy the files at the root of `nt-x86\`
(`nt-x64\` for Windows XP x64), without its subfolders, to the root of a
floppy, press F6 when text-mode Setup offers it, press S, and pick "xHCI98
USB 3 Controller (32-bit Windows 2000/XP)" (on XP x64, the x64 entry;
"xHCI98 USB 3.x Host Controller ..." on a floppy before `3.3.0.0`). Setup
then drives a USB keyboard, mouse and USB stick through its own drivers
above this one. **Installing Windows 2000: also copy three files from your
Windows 2000 CD to the floppy and pick the Windows 2000 entry** (below);
without them the install stops at its first start.

**Installing Windows 2000 from the F6 floppy** (new in `2.4.0.0`):

1. Copy the files at the root of `nt-x86\`, without its subfolders, to
   the root of a floppy.
2. From the `I386` folder of the Windows 2000 CD you install from, copy
   `USBD.SY_`, `HIDCLASS.SY_` and `HIDPARSE.SY_` to the root of the same
   floppy, as they are: do not expand or rename them. This package does
   not carry them; they are Windows 2000's own files.
3. Start Setup from that CD and press F6 when it offers it; press S,
   insert the floppy and press Enter. In the list, press the Down arrow
   to "xHCI98 USB 3 Controller (32-bit Windows 2000, files from CD)" and
   press Enter, then Enter again to continue.

Text mode copies the three files to the new system with the driver, so a USB
keyboard, mouse or stick used during Setup works when the new system first
starts. If they are not on the floppy, Setup stops while copying files with
"Insert the disk labeled: USBD.SY_ HIDCLASS.SY_ HIDPARSE.SY_ from the 32-bit
Windows 2000 CD into drive A:"; put a floppy holding them in drive A: and
press Enter, or press F3 to quit Setup. The entry is for Windows 2000 only:
on XP pick the other one, which never asks for them
(`docs/issues/16-w2k-f6-first-gui-boot-usbd.md` has the whole story).
Windows 2000 keeps three inert service entries named `usbd`, `hidclass` and
`hidparse` (set to start on demand; nothing starts them) from this step.

The same floppy serves everything that starts from the Windows CD's text
mode:

- **Installing Windows 2000 or XP.** GUI-mode Setup then installs the driver
  from `xhci98.inf` as usual, from the copies text mode made of the floppy's
  files. On the 32-bit floppy those include the helper `xhcihlpr.sys`, which
  text mode copies and does not load; GUI-mode Setup installs it with the
  controller, and it starts from the next boot (see "XhciPollFineTimer: a
  finer system timer while polling"). A pre-release build of `2.4.0.0` left
  it off `txtsetup.oem`'s file list, and GUI-mode Setup then installed no
  driver on the controller; this release's `txtsetup.oem` names it.
  **Windows 2000: use the Windows 2000 entry above; see the limit below.**
- **The Recovery Console.** Press F6 as for an install, then R at Setup's
  Welcome screen (on Windows 2000, then C for the console). The USB
  keyboard logs in and types commands; the console runs in text mode
  throughout, so Windows XP's GUI-mode restriction below does not touch it.

Limits:

- Pressing F6 needs the firmware's own USB keyboard support.
- The floppy must be drive A: as the firmware sees it.
- **A USB floppy drive works as the F6 drive only if it is one of the
  three text-mode Setup knows.** Windows 2000 SP4's and XP SP3's
  `I386\TXTSETUP.SIF` name the same three USB floppy drives: the Mitsumi
  (`USB\VID_03EE&PID_6901`), the Y-E Data (`USB\VID_057B&PID_0000`) and
  the TEAC (`USB\VID_0644&PID_0000`). XP's `usbstor.inf` names more
  floppy drives, but text mode reads only `TXTSETUP.SIF`, and no INF,
  this package's included, can add a drive to it. With any other USB
  floppy drive the driver cannot be loaded from it: use one of those
  three, or a floppy drive on the machine's own floppy controller.
- A disk that offers only UAS is not usable until GUI-mode Setup. Since
  `2.3.1.0` a disk that offers Bulk-Only as well as UAS, as most USB 3
  enclosures, SSDs and sticks do, runs as Bulk-Only in text mode and is
  usable there (`XhciForceBulkOnly`); Bulk-Only flash sticks always are.
- Installing Windows onto a USB disk is not supported.
- A USB drive present at the partition screen takes the letter C:. Unplug
  the USB drives you do not need, or Windows installs to the next letter.
- **Installing Windows XP and XP x64: have a PS/2 keyboard, or a laptop's
  built-in keyboard, to answer GUI-mode Setup's prompts.** Text-mode Setup
  drives the USB keyboard, mouse and stick, but it copies Windows' own
  `hidclass.sys`, `hidparse.sys` and `usbd.sys` to the new system only
  together with Microsoft's own USB controller drivers, which an xHCI-only
  machine does not use. So in GUI-mode Setup the USB keyboard and mouse do
  nothing until its "Installing Devices" step installs those files from
  the CD, and that step first shows Windows XP's unsigned-driver prompt for
  the xHCI98 controller and root hub, whose default button is No. A machine
  whose only keyboard is USB cannot answer it, and Setup stops there. A
  laptop's built-in keyboard (connected inside as PS/2, as on the ThinkPad
  E460 and P14s) or any PS/2 keyboard answers it, and after that step the
  USB keyboard and mouse work. Windows 2000 shows no such prompt. The
  package cannot carry those Microsoft files, `txtsetup.oem` can copy only
  from its own floppy, and only a signed driver or a remastered Windows CD
  would silence the prompt, so this is a limitation of this route.
- On Windows 2000, plug the USB keyboard and the USB stick in before Setup
  starts. Windows 2000's text-mode Setup uses only the USB devices present
  when the driver first reports them, as it does on its own USB stack; a
  device plugged in during text mode stays unused there. The driver waits,
  up to 5 seconds, for the devices plugged in at start before that first
  report (`XhciFirstEnumWaitMs`, under "Registry settings"; text-mode Setup
  always gets the defaults).
- **Installing Windows 2000 with the first entry, "(32-bit Windows
  2000/XP)", and a USB keyboard, mouse or storage device on the xHCI
  controller stops at the first start of GUI-mode Setup** (the Windows 2000
  entry with the three files from the CD, above, avoids it) with `STOP
  c000026c` ("Unable to Load Device Driver", error status `0xc0000034`),
  naming `\SystemRoot\System32\Drivers\hidusb.SYS` for a keyboard or mouse,
  or `usbstor.SYS` for a USB stick or disk. Windows 2000's text-mode Setup
  copies to the new system the class driver of each USB keyboard, mouse and
  storage device it drove (`hidusb.sys`, `kbdhid.sys`, `mouhid.sys`,
  `usbstor.sys`), but `usbd.sys`, which `hidusb.sys` and `usbstor.sys` both
  need, and `hidclass.sys` and `hidparse.sys` only together with Microsoft's
  own USB controller drivers, which an xHCI-only machine does not use; the
  new system then cannot load that class driver at its first start. What
  works on Windows 2000 from the floppy: text mode itself, and the Recovery
  Console; the stop comes at the first start of the installed system. An
  install with no USB keyboard, mouse or storage device on the controller
  during text mode (a hub and an audio device only) goes on into GUI-mode
  Setup with no stop. 32-bit XP with a USB keyboard and mouse is not
  affected: its text mode leaves the same files out, and its first start
  goes on without them (the XP limit above). The package does not carry
  those Microsoft files, and `txtsetup.oem` can copy only from the floppy in
  drive A: (text mode asks for any other disk there, with no way past it but
  F3), so the Windows 2000 entry copies them from the floppy once you have
  put them there.
- GUI-mode Setup may ask for the floppy or the Windows CD.

**Slipstreaming into a Windows 2000 CD.** If you add `xhci98.sys` to a copy
of your Windows 2000 CD as a driver text-mode Setup loads by itself (entries
in `I386\TXTSETUP.SIF`) instead of using the F6 floppy, the same stop
applies: Windows 2000's `TXTSETUP.SIF` copies `usbd.sys`, `hidclass.sys` and
`hidparse.sys` (all three already in the CD's `I386`) only in the file
sections of its own USB controller drivers, `[files.openhci]` and
`[files.uhcd]`, so the slipstreamed driver's own file section must list them
too, as those sections do, or a USB keyboard, mouse or stick used in text
mode stops the new system at its first start (`STOP c000026c`). This package
does not ship a slipstreamed CD or a recipe for one.

### Updating from 2.y.y.y

From `3.0.0.0` the installer (`SETUP.EXE` from `3.1.0.0`) does what
follows on Windows 2000 to 7 and prepares it on 98 SE and ME ("Installing, updating and uninstalling with
INSTALL.BAT"); this section is the route by hand, and what each system
does either way.

**From `2.3.x` or earlier to `2.4.0.0` or later, read "From 2.3.x or
earlier" (below) first**: that update finds each root hub, hub and
composite device again, so update the controller only, and on Windows XP,
XP x64 and 98 SE have a keyboard and mouse that do not go through this
driver before starting. Otherwise:

Install a newer `2.y.y.y` over an older one with Update Driver on "xHCI98
USB 3.x eXtensible Host Controller", then on "xHCI98 USB 3.x Root Hub", both
pointed at the `release-` directory. The steps are the same from every
earlier `2.y.y.y`. The registry values already there are kept and the
missing ones added ("Registry settings"); an update over `2.1.1.0` adds the
three values new in `2.2.0.0` the same way. What follows is what an update
of `2.1.0.0` over `2.0.0.0` does. Neither release uses NUSB's or SweetLow's
port driver, so nothing needs renaming first. On each system:

- **Windows 98 SE (under NUSB, under SweetLow's stack, or stock) and ME:
  restart after the update, although Windows does not ask.** Update Driver
  offers `2.1.0.0` as a better driver, and Windows 98 SE may ask for its CD
  for `usbd.sys`. The new `xhci98.sys` cannot replace the running one, so
  Windows sets it to replace it at the next start; until then `2.0.0.0`
  keeps running. Under SweetLow's stack the controller showed a problem and
  the mouse stopped for a minute or two after Finish, then both recovered
  by themselves; on Windows ME the controller shows a problem until the
  restart, while the devices keep working.
- **Windows 2000: use Have Disk.** Update Driver letting Windows search
  answers that a suitable driver is already installed and keeps `2.0.0.0`.
  Use "Display a list of the known drivers", Have Disk, the `release-x86\`
  directory, "xHCI98 USB 3.x eXtensible Host Controller", as in the steps
  above. It runs at once, with no restart.
- **Windows XP, XP x64, Vista and 7**: XP, XP x64 and 32-bit Vista show
  their unsigned-driver warning once; on every one the update takes effect
  at once, and no restart is asked for.
- **Windows Vista and 7: use Have Disk.** Update Driver Software, Browse my
  computer for driver software with the folder typed in answers that the
  best driver software is already installed and keeps the earlier release.
  Choose "Let me pick from a list of device drivers on my computer", Have
  Disk, and the `release-` directory instead.
- **32-bit Vista and 7: restart when asked.** Through Have Disk, both asked
  for a restart, for `xhcimsi.sys`, and until it the earlier release kept
  running with the controller at no problem code ("You need to restart
  your computer before the changes you made to this device will take
  effect"); after it, `2.3.1.0` and its helper ran.

**The root hub's Driver tab keeps showing `2.0.0.0`** (on Windows 98 SE and
ME, its date) after the update, although the root hub runs the same new
`xhci98.sys` as the controller. To have it show `2.1.0.0`, run Update Driver
on "xHCI98 USB 3.x Root Hub" too, pointed at the same directory. On 32-bit
Vista and on Windows 7 x64 letting Windows search found it; on 32-bit
Windows 7 the search answered that the best driver was already installed,
and "Let me pick from a list of device drivers on my computer" with the
`2.1.0.0` entry did it; on Vista x64 a command-line update of the root hub
did it. On the other systems use Have Disk.

When updating from `2.0.0.0`, each device is found once more as new hardware, exactly
once, and Windows installs it again from the same drivers: a stick under its
serial number, which Windows now knows it by instead of its port, and the
other devices under a new id. On the NT systems this happened with nothing
to answer; on Windows 98 SE the Add New Hardware wizard ran for each and
asked for the Windows 98 SE CD again for `hidclass.sys`, and Windows ME
found each once as well. Nothing was
found again after that. A setting kept on such a device's own entry, such as
hidusbf's polling rate, has to be applied again on the new entry.

**On Vista and 7, Have Disk does not take a build whose INF is identical to
the installed one** (the same `DriverVer`): Windows reuses the package
already in its driver store and keeps the old `xhci98.sys`. Replace the
`.sys` by hand, or use a build with a newer `DriverVer`.

**An update keeps the Intel port switchover as it was.** Since `2.3.0.0` a
new install writes `XhciIntelPortSwitch` at `0`, off; `2.1.1.0` and
`2.2.0.0` wrote `1`, and an update never overwrites a value that is already
there, so a machine updated from either keeps the switchover on. To take the
new default, run `portsw-off.bat` (or `portsw.vbs 0`) from the download's
`tools\portsw\` directory (in `2.3.1.0`, `portsw\` at the top of the
download; in `2.3.0.0`, the flavour directory) and restart Windows
("Intel 7-, 8- and 9-series chipsets: the port switchover").

**An update from `2.2.0.0` adds the eight values new in `2.3.0.0`** at
their defaults and keeps the rest ("Registry settings"). On 32-bit Vista
and 7 it also installs `xhcimsi.sys` and asks for a restart ("Windows
Vista and 7: message-signalled interrupts").

**An update to `2.4.0.0` adds `XhciPollFineTimer`** at `0` (off) and keeps
the rest. On 32-bit Vista and 7 it installs `xhcihlpr.sys`, marks the
`xhcimsi` service of `2.3.0.0` or `2.3.1.0` for deletion, deletes the
`xhcimsi.sys` file at once, during the update, and asks for a restart; the
controller's update itself waits for that restart, and after it the
`xhcimsi` service is gone, no error is logged for it, and `xhcihlpr.sys`
connects the message. On 32-bit Windows 2000 and XP it installs
`xhcihlpr.sys` and asks for no restart of its own; the helper runs from the
next boot, and the finer timer ("XhciPollFineTimer: a finer system timer
while polling"), when set to `1`, starts then.

**Deleting the driver keeps the older packages.** On Vista and 7,
uninstalling the controller in Device Manager with "Delete the driver
software for this device" ticked removes only the package in use. Every
older `xhci98` package installed on that machine stays in Windows' driver
store, and the next time Windows finds the controller it may install one of
them. To remove them all, list the store from an administrator's prompt with
`pnputil -e`, and remove each `oemNN.inf` it lists with the provider "Yeo
Kheng Meng" and the class "Universal Serial Bus controllers" with `pnputil
-d oemNN.inf`. The same applies before going back to an older version
("Windows Vista and 7: message-signalled interrupts").

#### From 2.3.x or earlier

From `2.4.0.0` the root hub, every hub and every composite device are named
under `USB\` instead of `XHCI98\` ("What `2.4.0.0` adds"). Windows keeps an
entry per device id, so this update costs something once, on every system.
What follows is what an update from `2.3.1.0` does on Windows 98 SE (under
NUSB and under SweetLow's stack), ME, 2000, 32-bit XP, XP x64, and Vista and
7 in both architectures (design record 13 section 10.13 is the design):

- **Each root hub, hub and composite device is found again as new
  hardware, once**, and installed from this package under its new name. Its
  old entry stays behind, not present: nothing loads for it, it costs
  nothing at run time, and Device Manager hides it (on Windows 2000 to 7
  it shows, greyed, after `set DEVMGR_SHOW_NONPRESENT_DEVICES=1` at a
  prompt, `devmgmt.msc` from that prompt and View, Show hidden devices; the
  old and the new carry the same names). Update the controller only: the
  root hub is a new one and installs by itself from the new package, so
  the root hub's Driver tab paragraph above does not apply.
- **The devices beneath follow.** A device or a composite device's function
  with a usable serial number (most sticks and disks) keeps its entry, its
  driver and its settings. One without (most mice and keyboards, at a root
  port or behind a hub) is found again too, because Windows names it after
  its parent, and on Windows 98 SE the Add New Hardware wizard runs for each
  and may ask for the Windows 98 SE CD. What was kept on the old entry is
  not carried over: a setting a tool wrote there (hidusbf's polling rate), a
  friendly name, and for a USB serial adapter possibly its COM number (the
  old entry may keep the number it had, and the new one take the next free
  one). Apply them again afterwards.
- **Before updating on Windows XP, XP x64 or 98 SE, have a keyboard and
  mouse that do not go through this driver**: PS/2, a laptop's built-in
  ones, or a USB controller another driver runs. XP and XP x64 ask Found New
  Hardware and the unsigned-driver question for the new root hub, each hub
  and each composite device; until those are answered, the USB keyboard and
  mouse behind the new root hub do nothing, so they cannot answer them. A
  second controller running this driver does not help: its root hub is new
  too. On 98 SE the USB keyboard and mouse do nothing while the update
  wizard's last page ("Windows has finished installing an updated driver")
  is up, so its Finish needs another keyboard or mouse; at Finish they work
  again, on the earlier release, until the restart. After the restart, 98
  SE's Add New Hardware wizard for a mouse without a serial number, found
  again, cannot be answered with that mouse. The update itself asked only
  for the Windows 98 SE CD (`usbd.sys`), and no restart.
- **Windows ME** asked for nothing: the controller shows a problem until
  the restart (as above), and after it one wizard came, for the USB audio
  device without a serial number.
- **Windows XP** shows "will not work until you restart" at the end of the
  controller's update, and then a System Settings Change box offering a
  restart, although `2.4.0.0` already runs; on XP `XHCICLN` already found
  every check passed before that restart. **64-bit Vista and 7** show the
  same "will not work until you restart", and `2.4.0.0` runs at once there
  too. **32-bit Vista and 7** keep the earlier release running until the
  restart they ask for, as above.
- **Windows 2000: reinstalling after the update.** An uninstall of the
  controller and a scan for hardware changes after the update installed
  `2.3.1.0` again from the earlier package Windows 2000 still held
  (`oem0.inf`): it ignores the dates of unsigned drivers. Use Have Disk
  with this release's `release-x86\` directory again, as for the update.
- **Two controllers** (32-bit XP): one controller's update made both run
  `2.4.0.0` at once, as both run the one `xhci98.sys`; the second
  controller's driver key kept `DriverVersion` `2.3.1.0`, the version its
  Driver tab shows, until it was uninstalled and found again.
- **Windows 98 SE and ME: connect no new hub or composite device between
  the update and the restart.** The earlier release runs until then and
  would give it an old name, which the new package no longer binds.
- **The old entries stay, and nothing needs them removed.** After the
  restart they are not present and do no harm. The cleanup tool that
  could remove them,
  `XHCICLN`, was dropped in `3.3.0.0`. The downloads from `2.4.0.0` to
  `3.2.0.0` still carry it in `tools\xhcicln\`, with its `readme.txt`.
  The installer's uninstall takes them with the rest of the driver (on
  98 SE and ME through the steps it prints).
- **Going back to `2.3.x`** brings each old entry back with its driver,
  and the new ones go non-present.

### Updating xhciuas.sys over an older one

When the package's `xhciuas.sys` carries the same `DriverVer` as the one
installed, Windows 98 SE's "Search for a better driver" keeps the copy it
already has. Instead, Update Driver on "xHCI98 USB Attached SCSI Storage",
choose "Display a list of all the drivers in a specific location", then Have
Disk, and point it at the package directory.

### NVMe drives behind Thunderbolt or USB4 (new in `3.0.0.0`)

This download bundles Stor98 `3.4.2.0`.

<!-- The cut replaces the line above with "This download bundles Stor98
`<version>`." or "This download bundles no Stor98."
(scripts\package\make-release.ps1 holds it to what the cut bundles). -->

An NVMe drive in a Thunderbolt or USB4 enclosure needs an NVMe driver.
Windows 98 SE, ME, 2000, XP, Vista and 8 have none of their own; 7 has
one only with Microsoft's update KB2990941, and 8.1 and 10 have
`stornvme`, which 10 keeps for a tunnelled drive. Stor98, the version named in
the line above, is that driver. `xhcicm.sys` brings the drive's tunnel up
and down, and Stor98 runs the drive.

Each family folder carries it, in its `stor98\`:

- built from this project's source with the USB drivers, at the same
  version and date (`3.0.0.0` bundled Stor98's own release `0.4.0.0`),
  under its own licence (BSD-3-Clause);
- `stor98.inf` and the files beside it whose names start with `stor98`;
- its `LICENSE`, `NOTICE` and readme beside it, as
  `stor98-LICENSE.txt`, `stor98-NOTICE.txt` and `stor98-readme.txt`.

Its readme is its install guide and lists its known limits; read it before
installing. `SETUP.EXE` installs it as part of the Thunderbolt 3 / USB4
support (its box under the Thunderbolt 3 / USB4 one on the Components page,
ticked with that one and needing it ticked; untick it to leave Stor98 out),
and only on NVMe drives behind a Thunderbolt or USB4 tunnel ("What `3.1.0.0`
changes"), and updates an older Stor98 on such a drive (on Windows 98 SE and
ME as a file, at the next start, after a shut down). Updating from `3.2.0.0`
on a computer with an ASMedia ASM4242 USB4 controller, the first run leaves
Stor98 on a drive it already runs and says so: `3.2.0.0`'s Thunderbolt
driver does not list that controller's tunnel ports, so where the drive sits
is read only after the restart, by the new one. Run the installer again
after the restart: it updates Stor98 there, unless another Stor98 binding
keeps the shared driver file at the older release (an internal drive on it,
or one not plugged in that the installer did not record), which the run then
says. On 98 SE and ME, from `3.3.0.0`, the installer also puts `stor98.mpd`
in place before the first tunnel drive is plugged, so Windows no longer asks
for the "Stor98 Storage Controller Disk" at its install. Stor98 is not for
internal NVMe drives: do not install it on one. From `3.3.0.0` its INF for
Windows 2000 to 8 matches only the id the connection manager gives a drive
behind a tunnel ("NVMe drives behind a tunnel"), so there Windows never
offers it for an internal NVMe drive. On 8.1 and 10 it keeps `3.2.0.0`'s
model, which Windows' own `stornvme` outranks on an internal drive. On 98 SE
and ME, which have no NVMe driver of their own, Stor98's 98 SE and ME INF
(`98-me\stor98\stor98.inf`) matches every NVMe controller, so Windows may
still offer it for an internal NVMe drive that has no driver there: decline
it. Up to `3.2.0.0` the one INF matched every NVMe controller on 2000 to 8
as well, and Windows 8 (and Vista x64 and 7 x64 with this package's test
certificate trusted) put it on an internal drive by itself; an internal
drive that runs Stor98 from then keeps it, and the uninstall leaves Stor98
in place while a drive uses it.

From `3.3.0.0` every Stor98 setting is named alike on every system, each
name starting with Stor: on Windows 2000 to 10 the `DriverParameter` keys
`maxtransferkb=`, `writethrough=`, `pollmode=`, `pollbusyus=`,
`pollidlems=`, `pollspinus=`, `gone=` and `idle=` are renamed
`stormaxtransferkb=`, `storwritethrough=`, `storpollmode=`,
`storpollbusyus=`, `storpollidlems=`, `storpollspinus=`, `storgonems=` and
`storwriteidlems=`, and Windows 98 SE and ME gain `StorGoneMs` and
`StorWriteIdleMs`, with the same meanings and ranges. A key of an old name
is ignored after the update, and the setting takes its default: rename it
in `DriverParameter`, keeping its value (Stor98's readme, "Settings").
Windows 98 SE and ME also gain `StorHold9xMs`, how long a request waits
for room in Stor98 before it is answered busy: 4000 ms by default,
1000 to 10000 ms, a value outside that refused and the default kept. It
has no `DriverParameter` key, since on Windows 2000 to 10 Stor98 holds a
request by its own timeout; `SETUP.EXE` and the wizard list it with the
other Stor98 settings (under "Advanced...", greyed on 2000 to 10).

On Windows 98 SE and ME, `98-me\stor98\` also carries `S98VSD.VXD` (new in
`3.2.0.0`, roadmap-usb4 task 43.46, design record 30), this package's own
disk-layer driver for Stor98's drives. Without it Windows cuts a read or
write over 512 KiB, or one whose buffer is in more than 17 physical pieces,
into 63 KiB pieces before Stor98 sees it; with it, reads and writes of up to
2 MiB reach Stor98 whole. Copies through the file cache are unchanged.
`SETUP.EXE` places it in `SYSTEM\IOSUBSYS\` with Stor98: its box, "Large
transfers for Stor98 (S98VSD.VXD), up to 2 MiB", sits under Stor98's and is
ticked whenever Stor98's is. `/nos98vsd` leaves it out and `/s98vsd` asks
for it (it needs Stor98 in the run); on every other system the box is greyed
and both switches are refused. Unticked, Stor98 works as before. An install
also removes a `STOR98.VXD` that a test build of this release placed (that
name stopped Stor98 from starting).

When a tunnel drive's NVMe controller (`PCI\CC_010802`) shows in Device
Manager without a driver, Update Driver, Have Disk, and point it at the
directory below (on 98 SE and ME pick `stor98.inf`, `stor98w9.inf` up to
`3.3.0.0`; on 2000 to 8 a drive that shows `PCI\XHCI98_TUNNEL&CC_010802`
among its compatible ids is found at once, and one without it only when the
compatible-hardware filter is off):

- `98-me\stor98\` on Windows 98 SE and ME (its INF `stor98.inf`, up to
  `3.3.0.0` `stor98w9.inf`)
- `nt-x86\stor98\` on 32-bit Windows 2000 to 10
- `nt-x64\stor98\` on 64-bit Windows XP x64 to 10

The x64 `stor98.sys` is test-signed with the same certificate as the
other x64 drivers, in the same step: `xhci98-test.cer` in `nt-x64\`
is its public half. Test-signing mode, which the x64 drivers already need
on Vista x64 and 7 x64, loads it too. Importing the `.cer` is not
needed.

## Devices and hubs in Device Manager

New in `2.1.0.0`.

- **A device keeps its entry when it moves.** A device with a usable serial
  number is known to Windows by it, as under Microsoft's own hub driver:
  moved to another port, or behind a hub, it keeps its Device Manager entry
  and is not found again as new hardware. Two identical devices plugged into
  the same port in turn no longer share one entry. A device without a serial
  number, or whose serial number is not usable as a name, is still known by
  its port, as under Microsoft's hub driver, so moved to another port it is
  found again as new hardware; so is the second of two devices that report
  the same serial number while both are plugged in. The snapshot counts that
  case as `serial.duplicate`, a counter the driver writes only when the
  controller stops, so a report taken while it runs does not show it.
- **External hubs have entries of their own.** A USB 2.0 or 1.1 hub appears
  as "xHCI98 USB Hub" beneath the port it is plugged into, with the devices
  behind it beneath it, as on Microsoft's own USB stacks. A USB 3 hub
  appears twice, once as "xHCI98 USB Hub" for its USB 2.0 half and once as
  "xHCI98 USB 3 Hub" ("xHCI98 USB 3.x Hub" up to `3.2.0.0`) for its
  SuperSpeed half, each with the devices on that half beneath it, as
  Microsoft's own stack also shows two entries for one USB 3 hub. Each hub
  entry has a Power tab, and Device Manager's view by connection and USBView
  walk into it. The driver still runs every hub itself; Windows' own hub
  driver is not used. A hub installs from the driver already installed: on
  Windows 98 SE and 2000 with nothing to answer, and on Windows XP with the
  Found New Hardware wizard and the unsigned-driver warning for each newly
  plugged hub (see "Installing"). Disabling and enabling an external hub in
  Device Manager brings back the devices behind it. While a drive behind the
  hub is open in Explorer, Windows refuses the disable, asks for a restart
  and leaves the hub running; close the window first.
- **Devices are named by their product name where Windows has no name for
  them.** Where one of Windows' own class INFs names a device (a mouse, a
  keyboard, a USB stick on most systems), Device Manager and the Add New
  Hardware wizard show the INF's name, as they do over Microsoft's own
  stack. The name the device reports for itself (for a part of a composite
  device, the name of that part where the device gives one) shows for a
  device no INF names, in Windows 98's "New Hardware Found" box, and as
  Windows 7's "Bus reported device description" on the Details tab. Under
  `2.0.0.0` each of those showed "USB Device". A device that reports no name
  is still "USB Device". On Windows 98 SE and ME every character outside
  plain ASCII is shown as `?`; the NT systems show the name as the device
  sends it.
- **Every device has the USB device interface** (new in `2.3.0.0`; GitHub
  issue 17). Each device registers `GUID_DEVINTERFACE_USB_DEVICE`, as under
  Microsoft's own hub driver, so a program that finds USB devices through
  it (`SetupDiGetClassDevs` with that interface, or
  `RegisterDeviceNotification` for its arrivals and removals) finds them.
  The root hub has none, as under Microsoft's stack. From `2.4.0.0` an
  external hub has none either, only the hub interface, as under
  Microsoft's stack on Windows 2000, XP and 7 (roadmap task 38.7; GitHub
  issue 17, SweetLow); `2.3.0.0` and `2.3.1.0` gave an external hub both.
  A composite device has it on its own entry (below), not on its parts, as
  under Microsoft's stack.
- **A composite device has an entry of its own** (new in `2.3.0.0`; GitHub
  issue 17). A device the driver splits into parts - a headset's audio and
  its buttons, a keyboard with media keys, a mouse with a second interface -
  appears as "xHCI98 USB Composite Device" beneath the port it is plugged
  into, with its parts beneath it, so each port of a hub has one entry, as
  on Microsoft's own stacks (there the entry is "USB Composite Device").
  USBView and the hub pages name that entry for the port. The driver still
  splits, configures and drives the device itself; Windows' own composite
  driver is not used. The entry installs from the driver already installed:
  on Windows 98 SE, 2000 and 32-bit Vista with nothing to answer, and on
  Windows XP with the Found New Hardware wizard and the unsigned-driver
  warning, the parts then installing silently. Disabling it in Device
  Manager stops every part, and enabling it brings them back without the
  device being plugged again; disabling one part leaves the others working.
  **After an update from an earlier `2.y.y.y` each composite device is found
  once more as new hardware**, exactly once: its own entry is new, and on
  the NT systems a part that the device has no usable serial number for is
  found again too, under its new parent; Windows installs each from the
  drivers it used before. A setting made on a part's own entry (such as a
  sound device's default) may need to be made again.
- **USBView shows a device's strings** (new in `2.3.0.0`; GitHub issue 17).
  Up to `2.2.0.0` a tool that asked the hub for a descriptor the driver had
  not kept - a string such as the manufacturer, product or serial number,
  another language, another configuration, the BOS - got nothing. The driver
  now reads it from the device, with the same length rules as Microsoft's
  hub driver, so SweetLow's USBView shows each device's and hub's strings.
  The WDK 7.1 USBView showed only the string numbers on 32-bit Vista, and
  does not run on 2000. A device that is suspended (`XhciSelectiveSuspend`
  `1`) is not asked: the request fails until it wakes.

## SweetLow's hidusbf: a mouse's polling rate

hidusbf, SweetLow's filter driver, now maintained by LordOfMice, sets the
polling rate Windows asks of a mouse or another HID device. It does so by
rewriting the polling interval in the configuration Windows selects for the
device, and this driver programs the controller from that configuration, so
the filter's setting reaches the controller.

A mouse set to 1000, 500 and 250 Hz is polled every 1, 2 and 4 ms, and at
its own rate with the setting off. hidusbf works behind a hub on XP too: its
Setup reads the mouse's speed there, and 1000, 500 and 250 Hz programmed
Interval 3, 4 and 5.

- **A composite device: either entry** (new in `2.4.0.0`). hidusbf's
  "Process Parent" puts the filter on the composite device's own entry,
  `xHCI98 USB Composite Device` (`USB\XHCI98_COMPOSITE`), and sets every
  function's rate there; the filter on a function's own entry sets that
  function's alone, and wins over the composite's. A new rate on the
  composite entry reaches a function when the function next starts: on
  2000 and later restarting the composite entry does it, on 98 SE and ME
  restart the function's entry too, or replug the device. If hidusbf's
  Setup does not offer the composite entry, set `LowerFilters` and
  `bInterval` on it by hand, as for a function on Windows 98 SE.
- **1000 Hz is the ceiling** for a Low- or Full-Speed device, as the xHCI
  specification sets it, unless `XhciFastPollFsLs` is set (under "Registry
  settings").
- **On Windows 98 SE and ME, set it on both keys.** The filter took effect
  only with its `LowerFilters` entry and `bInterval` value on both the
  device's hardware key and its `Class\HID` driver key; hidusbf's Setup
  writes them. It works on a stock Windows 98 SE as well as under NUSB:
  Windows 98 SE's own `usbd.sys` has the routine `hidusbf.sys` needs.
- **Apply the setting again after an update from `2.0.0.0`**: each device
  is found again once as a new Device Manager entry, and the setting is
  kept on the entry (see "Updating from 2.y.y.y").

## SuperSpeed and UAS

- **Which driver a storage device gets.** A UAS-capable device gets UAS
  through `xhciuas.sys`, and anything else gets Windows' own `usbstor.sys`
  (Bulk-Only), at whatever speed it connects. A device that offers both can
  be forced to Bulk-Only (`XhciForceBulkOnly`, below).
- **A UAS drive that runs Bulk-Only** (from `3.3.0.0` the xHCI98 USB Tree
  says so). Windows keeps the driver a device's entry was installed with,
  and a drive with a serial number is found again under the same entry. So a
  drive whose entry was installed while this driver offered it as Bulk-Only
  (by a release before `3.4.2.0`, or with
  `XhciBulkOnlyOwnId` set to `0`) is expected to keep
  `usbstor.sys` after the bus offers it as UAS. The tree's details of a
  drive show the transport the bus chose and why ("Transport: UAS (the
  device offers Bulk-Only and UAS; UAS chosen)"), and, where the entry is
  bound to `usbstor`, a note saying so. **To give it back to UAS**:
  uninstall the drive's entry in Device Manager (with the drive plugged in),
  then unplug the drive and plug it in again; Windows installs it afresh and
  the UAS driver is expected to take it. A drive whose vendor and product id
  a `usbstor.inf` names by hand - Windows' own on 2000 and later, or NUSB's
  on 98 SE - can take `usbstor.sys` again that way.
- **A drive that offers UAS and Bulk-Only, used as Bulk-Only** (new in
  `3.4.2.0`). At SuperSpeed on a controller without streams
  (`XhciStreamsOff`; by default the controllers Linux lists, such as the
  ASMedia ASM1042), and wherever `XhciForceBulkOnly` is set, such a drive
  is used as Bulk-Only,
  and it now has a Device Manager entry of its own for that, apart from
  its UAS entry: Windows installs its own Bulk-Only driver on it the first
  time, and the drive keeps UAS where it is used as UAS. Until `3.4.1.0`
  it had one entry on every controller, so a drive installed with the UAS
  driver on a controller that streams stopped at Code 10 on one that does
  not. A drive installed as Bulk-Only by an earlier release is found once
  more as a new entry, and may get another drive letter.
  `XhciBulkOnlyOwnId` set to `0` keeps one entry everywhere, as before.
- **SuperSpeed UAS needs streams.** A UAS-only device at SuperSpeed on a
  controller that cannot stream is sent back to its USB 2.0 port and runs
  UAS at High Speed, or is refused if it has no USB 2.0 port; behind a
  SuperSpeed hub it is refused in place.
- **Device Manager does not show SuperSpeed.** Device Manager and the other
  Windows tools on these systems show a SuperSpeed device as High Speed at
  most: the interface they read predates SuperSpeed. That display says
  nothing about the real link. The snapshot shows the speed the driver
  decoded from the port and the speed it gave the device's slot: in the
  report's slot table, `PSIV` is the Speed field of the controller's own Slot
  Context for the device and `speed` is that value decoded, for example
  `SuperSpeed, 5 Gbit/s, Gen 1x1`.
- **Intel Sunrise Point-LP (`8086:9D2F`): SuperSpeed since `2.2.0.0`.**
  Every release up to `2.1.1.0` never enumerated a SuperSpeed device on this
  controller, the USB controller of Intel's 100-series mobile chipset, found
  in the ThinkPad E460 and the HP EliteBook 850 G5 among others: a USB 3
  stick was not seen at all, and a USB 3 hub showed only its USB 2.0 half.
  The link trained; the driver could not name its speed. The controller
  publishes a table of speed IDs for its USB 3 ports that lists only three
  chip-to-chip rates and not the ID 4 it reports for a 5 Gbit/s device, and
  the driver read a published table as replacing the standard IDs. Since
  `2.2.0.0` an ID 4 to 7 that a USB 3 table does not list takes its standard
  meaning (4 is SuperSpeed at 5 Gbit/s), on every controller, and an ID the
  table does list still wins. On a controller whose table lists ID 4,
  nothing changes. `docs/issues/11-sunrise-point-ssic-psi-table.md` is the
  record. With `XhciLogVerbosity` at `2` or above, the snapshot says how
  each root port's speed was read ("The log, and how to send one").
- **Throughput, for scale.** On a ThinkPad P14s Gen 1 under Windows 98 SE, an
  MSSU10 flash drive under the UAS driver at SuperSpeed read about 198 to
  224 MB/s and wrote about 205 to 217 MB/s from 512 KB transfers up (ATTO
  Disk Benchmark, Direct I/O, queue depth 1), an informal run. Windows 98
  sends one command at a time, so a drive reads slower there than on a
  modern Windows at higher queue depth.

## Intel 7-, 8- and 9-series chipsets: the port switchover

New in `2.1.1.0`. On a machine with an Intel 7-, 8- or 9-series chipset
(Ivy Bridge to Broadwell) or a C610/X99 one, each switchable connector,
usually a blue one, is wired to two USB controllers, the USB 2.0 (EHCI) one
and the xHCI one, and the chipset decides which of them has it. Firmware set
to "Auto" or "Smart Auto", or offering no setting at all, typically leaves
those connectors on the USB 2.0 controller for the operating system to move:
on Windows 7 Intel's own USB 3.0 drivers do it, and Linux does it as it
starts. Up to `2.1.0.0` this driver did not, so on such a machine it started
with no error and saw nothing on those connectors, and a SuperSpeed drive
there ran at High Speed under the USB 2.0 controller, where a driver for
that was loaded.

Since `2.1.1.0` the driver can move them itself, and since `2.3.0.0` it does
so only when `XhciIntelPortSwitch` is `1` (or `2`, below): **it is off by
default.** A connector left on the USB 2.0 controller is not lost - it works
there, at USB 2.0 speed, wherever a driver for that controller is loaded
(NUSB's or SweetLow's on Windows 98 SE and ME, Windows' own on 2000 and
later) - so the driver no longer takes the connectors from it unasked
(owner, 2026-10-09; `2.1.1.0` and `2.2.0.0` installed it at `1`). Turned on,
it works in the order Linux does: each
time the controller starts, and again each time it resumes from standby, it
turns on the SuperSpeed side of every connector firmware lets it switch,
then moves the USB 2.0 side of each to the xHCI controller. When the
controller stops - disabled or removed in Device Manager, a start that
fails, or the machine shutting down - it hands them back to the USB 2.0
controller. A standby or hibernate keeps them where they are.

- **Which machines.** Only an Intel xHCI controller with PCI device id
  `1E31` (7-series), `8C31` or `9C31` (8-series), `8CB1` or `9CB1`
  (9-series), or `8D31` (C610/X99). On every other controller the driver
  writes nothing for this, unless `XhciIntelPortSwitch` is set to `2` on an
  Intel one (below). One board is left alone among those: a Sony VAIO
  with subsystem id `104D:90A8`, which Linux records as unable to switch;
  and so is any board whose subsystem id cannot be read. A connector that
  firmware does not let the system switch stays with the USB 2.0 controller
  whatever the driver does.
- **A device on the USB 2.0 controller is disconnected when the driver
  starts.** Where a USB 2.0 driver is running (NUSB's or SweetLow's on
  Windows 98 SE and ME, Windows' own on 2000 and later) and has a device on
  a switchable connector when this driver's controller starts, that device
  is disconnected there and found again under this driver. A USB drive in
  use there loses its drive letter mid-session, and writes in flight are at
  risk, so do not copy files on a blue connector while installing this
  driver or enabling its controller. The reverse happens when the
  controller stops: the devices on those connectors go back to the USB 2.0
  controller, and work there if a driver for it is loaded.
- **Turning it on, or off again: `portsw.vbs`.** The download's
  `tools\portsw\` directory carries `portsw.vbs` (in `2.3.1.0`, `portsw\`
  beside `release-x86\` and the others; in `2.3.0.0`, each flavour directory
  carries it instead), a Windows Script Host script that runs with nothing
  to install on every Windows this driver supports. Double-click it: it
  shows `XhciIntelPortSwitch` on every xhci98 controller on the machine and
  asks for `1` (on) or `0` (off). From a command prompt in that directory,
  `cscript //nologo portsw.vbs show`, `... portsw.vbs 1` or `... portsw.vbs
  0` does the same. Setting a value needs an administrator (on Vista and 7
  the script asks Windows for that itself). **Restart Windows afterwards**:
  the value is read when the controller starts. Turn it on for a machine
  whose blue connectors should run at SuperSpeed under this driver; leave it
  off where the USB 2.0 controller should keep them. The script never sets
  `2` (below); that is set by hand ("Registry settings").
- **In one double-click: `portsw-on.bat` and `portsw-off.bat`.** Beside
  `portsw.vbs`, each shows the value, sets `1` or `0` through the script and
  says to restart Windows. On Windows 98 SE and ME double-click it (at a
  prompt, change to its directory first); on 2000 and XP run it as an
  administrator; on Vista and 7 right-click it and choose Run as
  administrator. **Then restart Windows.** Neither sets `2`.
- **A USB 3 stick at USB 2.0 speed.** On one of these chipsets, a USB 3
  stick or drive on a switchable connector that copies only at USB 2.0 speed
  is most likely still on the USB 2.0 controller, because the switchover is
  off by default. Run `portsw-on.bat` and restart Windows. (A USB 2.0 hub or
  cable in between gives the same speed, and the switchover does not change
  that.)
- **What it logs.** With `XhciLogVerbosity` at `2` or above, the snapshot
  carries `psw.` records: `psw.gate` (`1` on a listed controller),
  `psw.value` with `psw.value.found` (`psw.value.found` 0 means the value
  is absent or not a `DWORD`, which is off since `2.3.0.0`), the two masks firmware allows and what each register read
  back after the write, and `psw.route.step`, `0` when every step was
  accepted. Since `2.2.0.0` an Intel controller's report also carries
  `psw.mode`, the setting in effect (`0` off, `1` on a listed controller,
  `2` on an unlisted Intel one by value `2`), and `psw.route.written` and
  `psw.release.written`, which of the two routing registers a route wrote
  and a release then gives back (bit 0 `USB3_PSSEN`, bit 1 `XUSB2PR`). Send
  them with any report from one of these machines.
- **A stop gives back only what was written.** Since `2.2.0.0` the hand-back
  at a stop or shutdown writes only the registers this driver's own routing
  wrote since the controller started; a routing that was refused part way
  gets back only the part it wrote. Up to `2.1.1.0` it wrote both registers
  whenever the switchover had been decided on.

**Value 2: any Intel controller, at your own risk.** The list above will miss
chipsets that have the same switchable connectors. Since `2.2.0.0`, setting
`XhciIntelPortSwitch` to `2` makes the driver do the switchover on any
Intel xHCI controller (PCI vendor `8086`), listed or not; it still does
nothing on another vendor's controller, on the Sony board, or where the
identity or subsystem id cannot be read. The driver does not check that the
registers are there:

> **Setting `XhciIntelPortSwitch` to 2 makes the driver read four Intel
> chipset registers and write two of them on any Intel USB 3 controller,
> not only on the 7-, 8- and 9-series and C610/X99 chipsets it was written
> for. On a controller that does not have those registers - every Intel
> chipset from the 100-series (Skylake) on, and any other whose layout has
> not been read - it writes registers of unknown meaning, at every start,
> resume, stop and shutdown, and the result is unknown. Use 2 only for an
> Intel chipset that has both an EHCI and an xHCI controller and that this
> driver does not list, and at your own risk. Everyone else should use 0
> or 1.**

On a listed controller `2` does the same as `1`.

## Controller faults: what the driver does about them

From `3.2.0.0` (roadmap-usb4 task 43.64), a cancelled transfer that sits
behind another on the same endpoint and crosses the end of its ring no
longer leaves the ring's Link TRB chained into the cancelled transfer's
place, which the xHCI specification forbids.

Also from `3.2.0.0` (roadmap-usb4 task 43.67), a device whose configuration
has more than four endpoints is no longer refused (`USBD_STATUS_NO_MEMORY`,
as the wired Xbox 360 pad's four interfaces and seven endpoints were): a
device may now hold up to 16 of the controller's 32 transfer rings.

New in `2.2.0.0`. A tester on an AMD AM5 board reported that a USB mouse
stops working at random: "basically unusable" under the `1.x.x.x` releases,
less often under `2.1.x`, and for longer after being moved to another port.
Which controller sits behind those ports, and what the driver saw, have not
been sent. Looking for a cause, the project found a set of transfer, port
and controller faults that used to leave a device dead until it was
unplugged and plugged in again, with nothing in the driver noticing. Since
`2.2.0.0` the driver answers each of them itself; but for the lost
interrupt, whose pickup keeps delivering, it does so a few times at most,
and then stops in a stated state rather than try for ever:

- **A lost interrupt.** An event the controller has written and no
  interrupt has delivered is picked up by the driver's own 100 ms timer
  once it has waited 100 ms, which in practice is about a third of a
  second after it was first seen.
- **A transfer error on a bulk or interrupt endpoint** (a USB Transaction
  Error: the device did not answer cleanly) is retried in place, up to three
  times for the same transfer, before the transfer fails as it did up to
  `2.1.1.0`. Since `2.3.0.0` that includes a UAS drive's data and status
  streams. Not on the control endpoint or isochronous (audio)
  endpoints, not for a Low- or Full-Speed device behind a High-Speed hub's
  transaction translator (a mouse or a keyboard plugged into a USB 2.0 hub
  running at High Speed; one behind a Full-Speed-only hub plugged into the
  computer's own connector is retried), and
  not on the four controllers Linux excludes from the same retry (AMD
  `1022:43B9` and `1022:43BB`, Etron `1B6F:7023` and `1B6F:7052`).
- **A report the driver cannot pin on a transfer** - a completion code the
  specification gives no transfer, or an endpoint the controller halted
  with no transfer to blame, on any endpoint but an isochronous (audio)
  one - makes the driver re-enumerate the device, as
  if it had been unplugged and plugged in again. To Windows that is a
  departure and an arrival: transfers in flight end as on an unplug, a
  drive's open files with them, and the device is found again where it was.
- **A USB 2.0 root port the controller switched off** while the device is
  still connected is re-enumerated the same way; **an over-current** on a
  root port is waited out, the port powered again and its device found
  again.
- **Each port has a budget**, a root port and a port of an external hub
  alike: three re-enumerations and, on a root port, three repowers. Once it
  is spent, the next fault there removes the device and holds the port:
  nothing is enumerated there, and the report says so. A held port that is
  still powered is released by unplugging its device; one held unpowered after over-currents only by restarting the
  controller - restarting Windows, or disabling and enabling the controller
  in Device Manager. A port gets its budget back when its device then works
  with no fault for at least a minute of transfers completing (about two
  and a quarter minutes at the driver's timer's nominal rate), or when it
  is left empty and powered for at least a second (about 2.4 seconds) and a
  device is plugged in again. The driver's timer is counted short on
  purpose, so these intervals can run long but never short: a quick unplug
  and replug may not release a held port, so leave it unplugged a few
  seconds.
- **A controller that halts by itself** is recovered in place, as one that
  reports an error already was: every device on it is re-enumerated, so a
  file copy running across the recovery fails. At most three recoveries in
  a window of at least ten minutes (about 22 minutes at the nominal rate);
  after that its devices are removed and it stays stopped until it is
  restarted - disabling and enabling it in Device Manager, or restarting
  Windows.
- **A controller that stops answering at all** (its status register reading
  as all ones for a second or more) is closed off: new requests wait rather than
  fail, and once the driver has proved the controller can no longer write to
  memory, its devices are removed; without that proof everything the
  controller might still write into is kept, and only restarting the
  machine ends it: disabling the controller then hangs ("Known
  limitations").
- **A device the controller reports incompatible** (the specification's
  Incompatible Device Error) is removed and re-enumerated, as the
  specification asks, and counts against its port's budget like any
  re-enumeration, so a device that keeps getting that report ends with its
  port held. Up to `2.1.1.0` the report on a transfer was handled as an
  ordinary transfer error and the device stayed listed but unusable. This
  applies whatever `XhciTolerance` says.
- **A recovery that cannot stop the controller** now needs the same proof
  before anything the controller might still be writing into is handed
  back. Without it the controller is closed off as above, stays stopped
  until the machine is restarted, and the snapshot reads "CONTAINED: halt and
  reset did not complete and Bus Master Enable would not clear; DMA not
  proven stopped, common buffer pinned". This too applies whatever
  `XhciTolerance` says.

Three rules on a controller that is latched failed - its recoveries
refused at the window's limit, or failed three times in a row - also
apply whatever `XhciTolerance` says, since they keep memory and requests
safe rather than answer a fault. When the controller shows it has halted,
the transfers it still holds are completed and its devices removed, so a
read of a drive fails at once and the controller can be disabled; a
resume from standby no longer brings such a controller back, only
disabling and enabling it or restarting Windows does (at `XhciTolerance`
`0` a change from `2.1.1.0`, where a resume could); and its state is not
saved or restored for standby while it is failed.

None of this acts while no fault is reported: the only cost then is one
memory read and one read of the controller's status register each time
the driver's thread looks, every 100 ms. A fault need not mean the
controller is unhealthy: a healthy controller reports a device's missing
handshake, a CRC failure or a timeout as a transaction error. What a class driver sees is either the same failed transfer it saw up to `2.1.1.0` or a device
leaving and arriving, never a new kind of error.

Two settings concern AMD controllers in particular (both under "Registry
settings"). **`XhciIntervalCap`**: Linux polls an interrupt endpoint that
asks for more than 32 ms every 32 ms on some AMD controllers; this driver
does the same on every AMD controller (PCI vendor `1022`), the AM5 and X570
ones included, which Linux does not list, and from `2.4.0.0` on the ATI
Navi10's USB-C controller (`1002:7316`), which Linux lists too. **`XhciAvgTrbEsit`** is a
comparison switch, off by default, that changes one figure the controller is
told for each interrupt endpoint, for a tester whose machine misbehaves to
try. And **`XhciTolerance`** set to `0` turns everything in the list above
off at once, but for the last two entries and the three rules after the
list, for a machine where it does more harm than good.

The snapshot counts what happened, with `XhciLogVerbosity` at `1`
or above: the transfer errors, unknown codes and unmatched events, summed
over the controller, a count of every completion code, the retries, each
re-enumeration and why, the lost interrupts the driver picked up, each
port's budget and hold, and the controller's recoveries and any
closing-off. At `XhciLogVerbosity` `2` the log also names the device and
endpoint of the first few of each error code. At `XhciTolerance` `0` the
errors, codes and unmatched events, and the root ports' disables and
over-currents, are still counted, so a report taken with the behaviours
off shows the faults they would have answered; the retries,
re-enumerations, holds, recoveries and the rest count only what the
behaviours did; those of the behaviours switched off stay at `0`, while
what applies at every value still counts (a held port after repeated
incompatible-device reports, a closing-off without proof). **If a USB device stops on your
machine**, send the controller's id (`XHCIQUAL`'s probe, or Device Manager)
and two reports at `XhciLogVerbosity` `2`, one while it works and one after
it stops (the xHCI98 USB Tree's File > Save Report..., once each, under two
names; "The log, and how to send one" below); and if it still stops, the
same with `XhciAvgTrbEsit` at
`1`.

## Windows Vista and 7: message-signalled interrupts (new in `2.3.0.0`)

On Windows Vista and 7 the controller may now use a message-signalled
interrupt (MSI or MSI-X) instead of its interrupt line, the way Windows'
own drivers for newer controllers do. A message does not travel through
the chipset's interrupt routing, so a BIOS routing fault such as the Dell
E7250's cannot lose it. Windows 98, ME, 2000 and XP (32- and 64-bit) have
no message-signalled interrupts and are unchanged.

- **64-bit Vista and 7**: the driver asks Windows for one message in the
  controller's hardware key, and connects it when Windows grants it.
- **32-bit Vista and 7**: the one 32-bit `xhci98.sys` also runs on Windows
  98, so it cannot itself use the Vista routine that connects a message. The
  32-bit package therefore carries a small second driver, `xhcihlpr.sys` (in
  `2.3.0.0` and `2.3.1.0`, `xhcimsi.sys`, for these two systems alone),
  which the INF installs as a service that starts with Windows and which
  hands `xhci98.sys` that routine. From `2.4.0.0` the same helper also
  carries the finer timer, and is installed on 32-bit Windows 2000 and XP
  too, with no restart asked for there ("XhciPollFineTimer: a finer system
  timer while polling"). **The install and every update on 32-bit Vista and
  7 ask you to restart**, because that service starts only at boot. After a
  first install the controller does not start until you restart (Device
  Manager shows it waiting for the restart: code 28 on 7, code 1 on Vista),
  so its USB 3 ports work from the next start of Windows on, with the
  message. A reinstall needs the restart too, whether or not Windows asks:
  after a remove and a scan for hardware changes, 32-bit Vista asked for
  none and held the controller at code 1 until a restart. If the helper is
  missing or does not load, the controller keeps its interrupt line under
  `XhciInterruptMode` `0`.
- **Asking for the interrupt line** on Vista and 7: set `XhciInterruptMode`
  to `1` and disable and enable the controller, or set `MSISupported` to
  `0` under `Interrupt Management\MessageSignaledInterruptProperties` in
  the controller's hardware key (Device Parameters) and restart. Under `1`
  the driver asks Windows for the line; if Windows grants a message anyway
  (a controller that offers no line), the controller fails to start
  (Device Manager shows Code 10), on both architectures. Set `0` again.
- **Asking for a message-signalled interrupt** on Vista and 7: set
  `XhciInterruptMode` to `3` and disable and enable the controller. The
  driver asks Windows for the message alone and uses it; if Windows
  grants none (for instance with `MSISupported` `0`), or the message
  cannot be connected (on 32-bit Vista and 7, when `xhcihlpr.sys` is not
  running), the controller fails to start (Code 10), with no fallback to
  the line or to polling. On Windows 98 SE, ME, 2000 and XP, which have no
  message-signalled interrupts, `3` always fails the start.
- **Going back to an earlier version** on Vista or 7: the request for a
  message stays in the hardware key, and releases before `2.3.0.0` do not
  know what to do with a message. Before installing an older package,
  uninstall the controller in Device Manager with "Delete the driver
  software for this device" ticked, or set `MSISupported` to `0` first. The
  tick removes only the package in use; older ones stay in the driver store
  ("Updating from 2.y.y.y", "Deleting the driver keeps the older packages").
- **Removing the 32-bit helper** after uninstalling the driver: it stays
  installed as a service and does nothing on its own. On XP, Vista and 7,
  `sc delete xhcihlpr` from an administrator's prompt, then a restart,
  removes it; Windows 2000 has no `sc.exe`, so delete the key
  `HKLM\SYSTEM\CurrentControlSet\Services\xhcihlpr` with `regedit` and
  restart. The `2.3.0.0` and `2.3.1.0` helper is removed the same way with
  `sc delete xhcimsi`; an update to `2.4.0.0` removes it by itself.
- **A controller that never raises its message** (from `2.4.0.0`): on the
  Fresco Logic controllers Linux names (`XhciMsiAvoid`, "Linux's
  controller quirks"), `XhciInterruptMode` `0` asks Windows for the line
  and never uses a message: the line where Windows grants one, polling
  where it grants only a message. The snapshot gives the reason as "a
  message granted, refused by XhciMsiAvoid".

The snapshot shows which delivery the controller uses.

## Registry settings

From `3.0.0.0`, `INSTALL.BAT` asks about four of these values -
`XhciIntelPortSwitch`, `XhciInterruptMode`, `XhciForceBulkOnly` and
`XhciSilentInstall`, seven in all with the three `XhciCm*` values it asks
about ("The connection manager's registry values") - where the
hardware or the Windows they affect is present, and sets them on request
("Installing, updating and uninstalling with INSTALL.BAT"); everything
below can still be set by hand.

**One value for every controller (new in `3.1.0.0`).** Every value below,
and every `XhciCm*` value ("The connection manager's registry values"),
is read first from `HKLM\System\CurrentControlSet\Services\USB` on every
Windows, and a value there **wins** over the same value in a controller's
own key, for every controller; only when it is not there does each
controller read its own key, as before. A value of the wrong type there
still wins, and the driver's default stands. The INFs write nothing
there, so nothing changes until a value is set in it; `portsw.vbs`,
`silent.vbs` and their batch files set their value there and show both.
The installer's Settings dialog, its console questions and `INSTALL /set`
write the machine-wide value too, never a controller's own, and show
both; its uninstall deletes the values it names there and leaves the key.
To give one controller a different value, delete the machine-wide value
and set that controller's own. `docs/using/tuning.md` has the details.

Each value is a `DWORD` in the controller's driver (software) key. From
`3.4.0.0` the install writes every value the driver reads in this key at
its default, only where it is missing, so a value you set survives an
update (up to `3.3.0.0` it wrote all but `XhciUsb2Bos`):

| Windows | Key |
|---|---|
| 98 SE, ME | `HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` |
| 2000 to 10 (x86/x64) | `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN` |

`NNNN` is the subkey whose `DriverDesc` names the controller: "xHCI98 USB 3
Controller" (from `3.2.0.0`; "xHCI98 USB 3.x eXtensible Host Controller"
before it), from `3.3.0.0` on 98 SE, ME, 2000 and 32-bit XP with the
controller's product after it, as `docs/using/tuning.md` says. The number
varies from machine to machine, and a machine whose controller was ever
enumerated at another PCI slot can have more than one such key: the device's
own `Driver` value, under `Enum\PCI`, names the one in use. The xHCI98 USB
Tree shows it as the controller's "Driver key", and its View > Logging
level... finds it for you. On Vista and 7 Device Manager also shows it: the
controller's Properties, Details tab, the "Driver key" property, whose value
ends in that `NNNN`. A change takes effect when the driver next starts:
restart Windows (or, for the values that say so, disable and enable the
controller in Device Manager). `XhciForceBulkOnly` and, from `3.4.2.0`,
`XhciBulkOnlyOwnId` are the exceptions: they are read as each device is
enumerated ("XhciForceBulkOnly", "XhciBulkOnlyOwnId").

The install writes every one: each at the driver's own default, except
`XhciImodInterval250ns` at `160` and `XhciForceBulkOnly` at `0`, which
the driver reads as `1` when it is absent (text-mode Setup;
"XhciForceBulkOnly"). Each is written only where it is missing,
so a value you changed survives an install or update; to go back to the
default, set it by hand, or delete the value and update the driver.
`XhciTolerance`, `XhciIntervalCap` and `XhciAvgTrbEsit` are new in
`2.2.0.0`, and an update over `2.1.1.0` adds them; `XhciIntelIrqRoute`,
`XhciMaxTransferKB`, `XhciInterruptMode`, `XhciPollIdleMs` and
`XhciPollActiveMs` are new in `2.3.0.0`, and an update over `2.2.0.0` adds
them. `XhciWideMap` (`0`), `XhciSelectiveSuspend` (`0`) and
`XhciUsb2HubSpeedFallback` (`1`) are read by `2.3.0.0` and written by its
install too, at the value that reads as a missing one; an update keeps any
of them you had already added. `XhciPollFineTimer` (`0`),
`XhciAccept096` (`1`) and the fourteen values of Linux's controller
quirks ("Linux's controller quirks", below) are new in `2.4.0.0`, each
written at its default, and an update over `2.3.0.0` or `2.3.1.0` adds
them. `XhciSilentInstall` (`0`; "What `2.4.0.0` adds") is new in
`2.4.0.0` too and written the same way: only Windows 98 SE and ME read it,
but the install writes it on every system. Since `2.3.0.0` the
install writes `XhciIntelPortSwitch` at `0`, where `2.1.1.0` and `2.2.0.0`
wrote `1`; an update keeps the `1` they wrote, and `portsw.vbs` changes it
("XhciIntelPortSwitch", below). The install has written
every value since `2.1.1.0`, which added `XhciIntelPortSwitch`. Of the
seven before it, the
`2.1.0.0` install wrote three, the two log values and
`XhciImodInterval250ns`, and wrote them every time, over a value you had
changed.

### XhciInterruptMode, XhciPollIdleMs and XhciPollActiveMs: how the controller's events arrive

New in `2.3.0.0`; the install writes `0`, `50` and `1`. Read at each start
of the controller: disable and enable it in Device Manager, or restart
Windows. Up to `2.2.0.0` the driver took only the controller's interrupt
line and refused to start a controller without one. Since `2.3.0.0` it
can also read the controller's events on a timer of its own (polling),
and on Windows Vista and 7 take a message-signalled interrupt ("Windows
Vista and 7: message-signalled interrupts").

| `XhciInterruptMode` | Effect |
|---|---|
| `0`, absent, not a `DWORD`, or `4` and above | Automatic, the default. The interrupt line where it works. A controller with no interrupt pin, or given no interrupt by Windows, is polled. A line the driver sees the controller raise and that never reaches it is detected after the start, and the controller moved to polling by the driver itself; an interrupt that fails in a way the driver cannot see (no sign of it in the controller's registers) is not, and needs `2`. On Vista and 7, a message-signalled interrupt where Windows grants one |
| `1` | The interrupt line only, as `2.2.0.0`, on every system. On Vista and 7 the driver asks Windows for the line rather than a message. A controller given no line - no interrupt, no interrupt pin, or on Vista and 7 a message Windows grants anyway - fails to start (Device Manager: Code 10) |
| `2` | Polling always, even where the line works. For a controller whose interrupt misbehaves; it costs more processor wake-ups |
| `3` | A message-signalled interrupt (MSI or MSI-X) only. On Vista and 7 the driver asks Windows for the message alone; if none is granted, or it cannot be connected (on 32-bit Vista and 7, when `xhcihlpr.sys` is not running), the controller fails to start (Code 10), with no fallback to the line or to polling. On Windows 98 SE, ME, 2000 and XP (32- and 64-bit), which have no message-signalled interrupts, `3` fails the start |

| Value | Default | Effect |
|---|---|---|
| `XhciPollIdleMs` | `50` | While polling, how often (in ms) the driver looks when nothing is outstanding. `0` runs as `50`; above `5000` is held to `5000`. A device plugged in waits at most this long to be seen |
| `XhciPollActiveMs` | `1` | While polling, how often (in ms) it looks while transfers are outstanding. `0` runs as `1`; a value above the idle period is held to it. When the only transfers outstanding are periodic ones (a mouse's or keyboard's interrupt endpoint), the driver looks at that endpoint's interval instead, held between the active and the idle period. Windows runs the timer no faster than its clock tick, so `1` means "as often as Windows allows" |
| `XhciPollFineTimer` | `0` | New in `2.4.0.0`. While polling at the active period, ask Windows 2000 and later for a finer system clock, of `XhciPollActiveMs` ("XhciPollFineTimer: a finer system timer while polling", below). `0`, absent, or not a `DWORD` is off; any other value runs as `1`. Windows 98 SE and ME read it and do nothing |

A start that `1` or `3` refuses records why: with `XhciLogDebugView` set,
DebugView shows the counter dump's `intr.reason` line (14: a message
granted under `1`; 16: no message granted under `3`; 17: the message not
connected; 18: a system with no message-signalled interrupts) beside the
Code 10, and with `XhciLogVerbosity` `2` or above also the start's notes
and a `start.refused.delivery` line.

Neither period matters while the controller has a working interrupt line or
a message. Polling adds up to one period of delay to each completed
transfer, and Windows runs the poll no more often than its timer allows,
which on some machines is every 15 to 62 ms rather than every 1 ms. The
snapshot says which delivery the controller uses, and why. `XHCIQUAL` passes
a controller with no working interrupt as `QUALIFIED WITH CAVEATS` ("Before
you install: check the machine").

### XhciPollFineTimer: a finer system timer while polling

New in `2.4.0.0`, **off by default**: the install writes `0` on every path
of both INFs, and an update keeps a value already there; set it to `1` to
turn it on. Read at each start of the controller: disable and enable it, or
restart Windows. A polled controller is looked at no more often than
Windows' clock ticks: at best every 10 to 15 ms with `XhciPollActiveMs` `1`,
and every 15 to 62 ms on some machines ("XhciInterruptMode, XhciPollIdleMs
and XhciPollActiveMs"). With this value on, while the driver polls at the
active period it asks Windows for a clock of `XhciPollActiveMs`
(`ExSetTimerResolution`), and gives the request back when it stops polling
at that period - one idle period (`XhciPollIdleMs`) after the last transfer
outstanding, or with only a mouse's or keyboard's periodic transfers
outstanding - and when the controller leaves full power (standby, hibernate,
shutdown, or its own power-down), recovers, or stops. Each controller holds
at most one request, and every request is given back. The call is made from
a thread of the controller's own, which no power request waits on.

- **Windows 2000, XP, Vista and 7, 32-bit**: through the helper
  `xhcihlpr.sys`, which the INF installs on all four. On 2000 and XP the
  install asks for no restart, so the controller runs at once with the
  clock tick, and the finer timer starts at the next boot, when the helper
  is loaded. Without the helper the controller polls at the clock tick, as
  `2.3.1.0` did.
- **XP x64, Vista x64 and 7 x64**: directly, with no helper.
- **Windows 98 SE and ME**: never; they have no such call, and the value
  is read and does nothing.
- **Only while polling**: nothing changes under an interrupt line or a
  message-signalled interrupt.

The clock is the whole system's: a finer one lets every timer on the
machine fire sooner, at some cost in processor wake-ups while it is
held, and other software can hold a finer clock of its own, which this
driver does not undo. The snapshot has a "finer system timer"
section: whether the value is on (after the value's name, the install's
`0` reads "0; in effect off", and "not set; in effect off (the default)"
is printed only where the value is absent), where the call came from, whether a
request is held, and how many were made and given back. Text-mode Setup
(F6) has no helper and keeps the clock tick until the installed system's
own install brings it; the 32-bit floppy carries the file for that
install, and text mode copies it without loading it.

**Why it ships off.** While the finer clock was held under polling
(`XhciInterruptMode` `2`), the system clock has been seen to run at a tenth
to seven tenths of real time on Windows 7 x64, 7 x86 and Vista x86, and keys
typed on a USB keyboard were lost; with the value `0`, or under an
interrupt, the clock kept real time and no key was lost. Leave it off unless
you need it.

### XhciImodInterval250ns: the interrupt moderation interval

In **units of 250 ns**: how long the controller waits after one interrupt
before raising the next. A shorter interval makes USB storage faster at the
cost of more interrupts.

| | Value | Interval | Interrupts per second, at most |
|---|---|---|---|
| Written by the install, every path, both INFs | `160` | 40 us | 25,000 |
| Used when the value is missing, unreadable, or outside `10`-`4000` | `4000` | 1 ms | 1,000 |
| Lowest accepted | `10` | 2.5 us | 400,000 |

`160` is written by the INFs only; the driver's own default stays `4000`, the
controller's power-on value, which is what it runs at if the value is absent.
A value outside `10`-`4000` is replaced by `4000`, not rounded, so a mistyped
`0` cannot turn moderation off. The driver reads the value when the
controller starts, so a change takes effect after a restart, and the snapshot shows the value read, the
interval in force and what the controller took.

`160` is the value Linux's xHCI driver uses. On a ThinkPad P14s Gen 1 under
Windows 98 SE with NUSB, an MSSU10 drive under the UAS driver at SuperSpeed
(ATTO Disk Benchmark, Direct I/O, queue depth 1) read and wrote 181 MB/s at
8 MB transfers at `500`, the value `1.2.0.0` shipped, against 211 MB/s write
and 221 MB/s read at `160`; `40` added only 1 to 3% more for up to four
times the interrupt rate. If audio stutters under storage load on your
machine, raise the value towards `4000` or delete it.

### XhciForceBulkOnly: Bulk-Only instead of UAS

`1` makes every storage device on that controller that offers both
transports use Bulk-Only (Windows' own `usbstor.sys`) instead of UAS. `0`,
which the install writes (since `2.1.1.0`), means UAS wherever the device
offers it. Absent reads as `1` since roadmap task 37.2 (`0` before): that is
how text-mode Setup sees it, before any INF has run, and there Setup's own
`usbstor` is the only storage driver, so a stick or disk that offers both
is usable there (issue 15).

It is read each time a device enumerates, so unplug and replug the device
after changing it. From `3.4.2.0` a disk used as Bulk-Only has a Device
Manager entry of its own (`XhciBulkOnlyOwnId`), so the change applies at
the replug; with `XhciBulkOnlyOwnId` at `0`, a device already installed
keeps its driver until it is uninstalled in Device Manager and replugged. A
UAS-only device stays on UAS whatever the value says. A device whose vendor
id Windows' own `usbstor.inf` lists by hand can still get `usbstor.sys` on
the NT targets.

### XhciBulkOnlyOwnId: an entry of its own for Bulk-Only

New in `3.4.2.0`. `1`, which the install writes, gives a drive that offers
both UAS and Bulk-Only a Device Manager entry of its own wherever it is
used as Bulk-Only (at SuperSpeed on a controller that cannot stream, or
with `XhciForceBulkOnly` set), so Windows installs its own Bulk-Only driver
there and the drive's UAS entry keeps the UAS driver. `0` gives the drive
one entry everywhere, as before `3.4.2.0`. Absent reads as `1`. It is read
each time a device enumerates, so unplug and replug the drive after
changing it.

### XhciFastPollFsLs: Low- and Full-Speed polling above 1000 Hz

New in `2.1.0.0` and off by default; since `2.1.1.0` the install writes `0`. It lets a Low-
or Full-Speed mouse on a root port be polled faster than 1000 Hz, at the
rates hidusbf offers for that under Windows 8 and later, using hidusbf's own
numbers: the device is set in hidusbf to "31 Hz" or "62 Hz", and this value
turns those two settings into fast ones.

| Value | hidusbf "31 Hz" | hidusbf "62 Hz" |
|---|---|---|
| absent, `0`, `1` or anything else | 31 Hz | 62 Hz |
| `2` | 2000 Hz | 4000 Hz |
| `3` | 4000 Hz | 8000 Hz |
| `4` | 31 Hz | 62 Hz (the codes below instead) |

**Changed in `3.1.0.0`: value `4`'s codes.** In `2.3.0.0` to `3.0.0.0`
value `4` took the codes 128, 129 and 130 for 2000, 4000 and 8000 Hz. From
`3.1.0.0` it takes 128 to 143, and the order is reversed: 128 is now
8000 Hz and 130 is 2000 Hz. There is no setting for the old codes. A filter
set to a code for an earlier release must be set again.

With `2` or `3`:

- **Root ports only.** A device behind a hub keeps its ordinary rate.
- **Outside the xHCI specification.** The specification's shortest interval
  for a Low- or Full-Speed device is 1 ms. A controller that refuses the
  shorter one is caught: the device is set up again at its ordinary rate,
  and the refusal is counted in the snapshot as
  `fastpoll.fallbacks`. A controller that accepts it and then misbehaves -
  polls at its own rate, or starves other devices - cannot be caught. If
  anything misbehaves, delete the value and restart.
- **It applies to every device that asks for 16 to 63 ms.** While it is set,
  any Low- or Full-Speed device on a root port whose own polling interval is
  16 to 63 ms is polled faster too, hidusbf or not.

**`4`: the rate the client chooses** (new in `2.3.0.0`, GitHub issue 17;
its codes changed in `3.1.0.0`, from SweetLow's proposal). An interrupt
endpoint whose polling interval in the configuration Windows selects is a
code from 128 to 143 is polled every 125 us x 2 to the power (code - 128):

| Code | Polled every | Rate |
|---|---|---|
| 128 | 125 us | 8000 Hz |
| 129 | 250 us | 4000 Hz |
| 130 | 500 us | 2000 Hz |
| 131 | 1 ms | 1000 Hz |
| 132 to 143 | 2 ms to 4.096 s, doubling each step | 500 Hz down |

Every other interval is polled as the specification reads it, so value `4`
does not do `2`'s and `3`'s speeding up of 16 to 63 ms, and `2` and `3` do
not honour the codes. It applies at every speed, on a root port or behind a
hub. The procedure is the value first, then the client: set
`XhciFastPollFsLs` to `4` and restart, then set hidusbf (or another filter)
to write the code. Two cautions:

- 128 to 255 ms are legal polling intervals for a Low- or Full-Speed
  device, so a device that itself asks for 128 to 143 ms is polled at its
  code's rate while the value is `4`.
- A filter that writes a code while the value is not `4` gets what the
  number means without it: a Low- or Full-Speed device is polled every
  128 ms, and a High-Speed or SuperSpeed one, for which the number is out
  of range, every 4.096 s; on a controller where `XhciIntervalCap` applies,
  either is polled every 32 ms instead.

A controller that refuses a code is caught as for `2` and `3` and counted in
`fastpoll.fallbacks`, and the device is then polled every 1 ms (1000 Hz),
whatever the code, not at the code's ordinary reading. A controller that
accepts it and misbehaves cannot be caught; delete the value and restart. At
High Speed and SuperSpeed every code's polling interval is inside the
specification's range; at Low and Full Speed codes 131 to 138 (1 to 128 ms)
are inside it, and the others are outside it, as `2`'s and `3`'s are. A Low-
or Full-Speed device behind a hub takes the code too (`2.3.0.0` to `3.0.0.0`
left such a device alone); below 1 ms or above 128 ms the specification does
not describe it. On an AMD controller the code is applied before
`XhciIntervalCap`, which leaves a code of 136 or faster alone and polls a
slower one every 32 ms. A part of a composite device keeps the code only
until its class driver issues a later select of its interface
(`SELECT_INTERFACE`), even of the same alternate setting, as with `2` and
`3`; HID class drivers do not.

The value is read when the controller starts, so a change takes effect
after a restart.

### XhciFirstEnumWaitMs and XhciFirstEnumPortMs: the first report's wait

New in `2.1.0.0`. When the root hub, or an external hub, first reports its
devices to Windows after it starts, the driver first waits for the devices
already plugged in to be ready, so that they are in that first report.
Windows 2000's text-mode Setup uses only the devices in it. The wait ends as
soon as the devices are ready. Since `2.1.1.0` the install writes both at
their defaults.

| Value | Default | Meaning |
|---|---|---|
| `XhciFirstEnumWaitMs` | `5000` | The longest the first report waits, in milliseconds. `0` turns the wait off; a value above `30000` is held to `30000` |
| `XhciFirstEnumPortMs` | `2000` | The longest one port may hold it, in milliseconds; a slower device is reported later instead. Held to the total; `0` sets no limit per port |

### XhciIntelPortSwitch: the Intel port switchover

New in `2.1.1.0`; since `2.3.0.0` the install writes `0` (`2.1.1.0` and
`2.2.0.0` wrote `1`, and an update keeps a value already there). It matters
only on the Intel chipsets listed in "Intel 7-, 8- and 9-series chipsets:
the port switchover", unless it is `2`. Since `2.2.0.0` it is read on every
Intel controller, and still on no other vendor's. `portsw.vbs`, in the
download's `tools\portsw\` directory (in `2.3.1.0`, `portsw\` at the top of
the download; in `2.3.0.0`, each flavour directory), shows it and sets it to `0` or `1` on every xhci98
controller ("Turning it on, or off again" in that section).

| Value | Effect |
|---|---|
| `0`, absent, or not a `DWORD` | Off, the default since `2.3.0.0`: the driver leaves the connectors where they are, as every release up to `2.1.0.0` did. Up to `2.2.0.0` absent or not a `DWORD` was on |
| `1`, or any other number but `2` | On, on a listed controller: the driver moves the connectors to itself at each start and hands them back at each stop |
| `2` (since `2.2.0.0`) | On, on every Intel controller, listed or not. **At your own risk**: on an Intel controller without the switch registers - every Intel chipset from the 100-series (Skylake) on, and any whose layout has not been read - it writes registers of unknown meaning at every start, resume, stop and shutdown, and the result is unknown. Use it only for an Intel chipset with both an EHCI and an xHCI controller that the list misses; everyone else should use `0` or `1`. See "Value 2" in that section |

Only a `DWORD` that is not `0` turns it on, and only an exact `2` goes past
the list. It is read when the controller starts, so a change takes effect at
the next restart or at a disable and enable of the controller in Device
Manager. Setting `0` does not give the connectors back by itself: after a
restart they are where firmware puts them, and after a disable and enable
they stay where the disable left them, with the USB 2.0 controller.

### XhciIntelIrqRoute: the Intel interrupt routing repair

New in `2.3.0.0`; the install writes `1`. On the Intel 7-, 8- and 9-series
and C610/X99 chipsets, two chipset registers decide which interrupt line the
xHCI controller's interrupt reaches. Some firmware leaves them pointing at a
different line from the one it tells Windows: the Dell Latitude E7250's
sends the interrupt to IRQ 16 while Windows listens on IRQ 21, so the
driver never hears from the controller and every device on it is slow or
stops (docs/issues/12). At each start and each return from standby the
driver reads those registers and, where the value allows it, points the
controller's interrupt at the line Windows assigned. It changes nothing else.

| Value | Effect |
|---|---|
| `0` | Off: the driver reads and reports, and writes nothing |
| `1`, absent, any other number, or not a `DWORD` | On the Dell Latitude E7250 only (controller `8086:9CB1`, subsystem `1028:062D`, chipset `9CC3`); elsewhere it reads and reports |
| `2` | On any of the six reviewed controller and chipset pairs: controller `1E31`, `8C31`, `9C31`, `8CB1`, `9CB1` or `8D31` with one of its own chipset ids (docs/issues/12 section 5.1) |

**It does nothing on every other machine**, at any value: another vendor's
controller, an Intel controller not in that list (every chipset from the
100-series on included), a chipset id that does not match its controller,
or a check of an id, a register or the interrupt Windows gave that
fails. **It also does nothing
when the controller's interrupt is a message (MSI) rather than a line**, as
it can be on Windows Vista and 7, **and nothing when the driver is polling
the controller instead of taking its interrupt**: neither goes through the
line it would repair. On Windows 2000, XP, Vista and 7 it also writes
nothing when Windows gave the controller an interrupt below 16, since
which route an I/O APIC system then uses is not proven; it reports what it
would have done. It decides again at each start and return from standby,
so a change takes effect at the next restart or at a disable and enable of
the controller.

What it logs. The snapshot has a section "interrupt routing" with
the value in effect, how many times it ran and wrote, and the last run's
decision, what it read and, after a write, what the register read back;
with `XhciLogVerbosity` at `2` or above the report also carries `pirq.`
records. Send them with any report from one of these machines.

### XhciTolerance: the controller-fault handling, all together

New in `2.2.0.0`; the install writes `1`. It switches everything in
"Controller faults: what the driver does about them" except the two AMD
settings below and the last two entries of that list (a device reported
incompatible, a recovery that cannot stop the controller) and the three
rules on a latched-failed controller after it (its held transfers
completed, no resume bringing it back, no saved state), which apply at
every value.

| Value | Effect |
|---|---|
| `1`, any other number but `0`, absent, or not a `DWORD` | On: the lost-interrupt pickup, the transfer retry, the re-enumeration of a device after a report nothing owns, the root ports' disable and over-current handling and every port's budget, the recovery of a halted controller and its limit of three in a window of at least ten minutes, and the closing-off of a controller that stops answering |
| `0` | Off: every one of those paths is handled as `2.1.1.0` handled it, but for the rules above that apply at every value; one of them is a change from `2.1.1.0` at `0`: a resume no longer brings back a controller whose recoveries failed three times in a row |

Only an explicit `0` turns it off. It does not touch `XhciIntervalCap` or
`XhciAvgTrbEsit`, and at `0` the snapshot still counts the faults
it sees, though not what the behaviours would have done about them (see
"Controller faults: what the driver does about them"). It is
read when the controller starts, so a change takes effect at the next
restart or at a disable and enable of the controller, and the new start
leaves nothing of the old setting's work behind. The budgets and intervals
themselves are fixed in the driver, not settings.

### XhciIntervalCap: the AMD interrupt-interval cap

New in `2.2.0.0`; the install writes `1`. An interrupt endpoint - a mouse,
a keyboard, a hub's status - that asks to be polled less often than every
32 ms is polled every 32 ms. Linux does the same on some AMD controllers;
this driver does it on every AMD one, since the AM5 and X570 controllers
are not on Linux's list and the report above is from an AM5 board.
Isochronous (audio) endpoints are never capped. Polling every 32 ms is
within USB 2.0 and costs a few polls a second for such a device.

| Value | Effect |
|---|---|
| `1`, any other number, absent, or not a `DWORD` | The cap on AMD controllers (PCI vendor `1022`) and, from `2.4.0.0`, on the ATI Navi10's USB-C controller (`1002:7316`, Linux's `XHCI_LIMIT_ENDPOINT_INTERVAL_9`) only |
| `0` | Off on every controller |
| `2` | The cap on every controller |

It is applied after a polling rate a setting such as hidusbf's asked for
and does not change which mouse `XhciFastPollFsLs` speeds up; under
`XhciFastPollFsLs` `4` a code is applied first, and the cap leaves a code
of 32 ms or faster alone. It is read when the controller starts.

### XhciAvgTrbEsit: a comparison switch

New in `2.2.0.0`; the install writes `0`. `1` tells the controller, for each
interrupt endpoint, an average transfer size equal to the endpoint's largest
transfer per service interval (held to 1 to 65535 bytes), as Linux does,
instead of the 1024 bytes the xHCI specification suggests and this driver
has always used. Isochronous and other endpoints are never changed. Any
value but `1` reads as `0`. That figure enters the controller's own
bandwidth reckoning, so it is not changed by default; the switch exists so
that a machine whose devices misbehave can be compared with it set, and
nothing ties it to the AMD report. Leave it at `0` unless you are
comparing. It is read when the controller starts.

### XhciMaxTransferKB: the UAS command size

New in `2.3.0.0`; the install writes `1024`. In **KiB**: the most one UAS
command asks the drive to move, which the UAS driver tells Windows' storage
class driver as the largest request it takes. `2.2.0.0` and earlier always
said 64 KiB. Fewer, larger commands mean fewer command and status
exchanges per megabyte; the controller still moves each command in pieces
of at most 128 KiB, one after another.

| Value | Effect |
|---|---|
| `1024`, absent, not a `DWORD`, outside `64`-`2048`, or not a multiple of `4` | 1 MiB commands (the snapshot notes why a value was not used) |
| `64` | 64 KiB commands, as `2.2.0.0` and earlier |
| `2048` | 2 MiB commands, the largest |
| any multiple of `4` from `64` to `2048` | that size |

It applies to every UAS device on that controller; Bulk-Only devices keep
Windows' own `usbstor.sys` limits whatever it says. Windows reads the size
when the storage device starts and keeps it, so **a change takes effect at
the next restart of Windows**. The snapshot shows the value read and
the command size in effect, and each UAS device's hardware key records the
size it took (`XhciUasCommandBytes`, in bytes).

**Trying `2048` by hand.** Set `XhciMaxTransferKB` to `2048` (decimal; `800`
hex) in the driver key above, with `regedit` or a `.reg` file, and restart
Windows. To go back, set `1024`, or `64` for a drive that misbehaves at
large commands, and restart again. Linux's UAS driver sends commands of up
to 512 KiB, and its Bulk-Only driver 120 KiB, or 1 MiB at SuperSpeed, with
64 KiB or 120 KiB for drives known to misbehave; Windows 7's Bulk-Only
driver sends 64 KiB, raised per device up to 2 MiB by Microsoft's KB 2581464.
So `1024` is within what drives already see elsewhere, and `2048` beyond
most of it. A drive that fails at a larger size usually fails at its first
large read or write: if copies fail or the drive drops off after raising
the value, set `64`.

**A later release with another default.** The install writes the value
only where it is missing, like every value here, so an update keeps the
number already in the key - the old default included. To take a new
release's default, delete `XhciMaxTransferKB` before updating the driver,
or set the new number by hand, and restart.

**One model only: Microsoft's per-device value.** On Windows 2000 and
later the UAS driver also honours the value Windows' own Bulk-Only driver
uses for one kind of device, so a drive model can be set apart from the
rest:

| | |
|---|---|
| Key | `HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP`, where `VVVV` and `PPPP` are the drive's vendor and product IDs as four uppercase hex digits each (`VID_0BDA&PID_9210` is `0BDA9210`) |
| Value | `MaximumTransferLength`, a `DWORD`, in **bytes** |
| Accepted | `65535` to `2097152`: the range Microsoft's KB 2581464 gives, `65535` to `2097120`, with `2097152` - what tools write for 2 MiB - taken as 2 MiB as well |

A value in range replaces `XhciMaxTransferKB`, larger or smaller, for
every drive with those vendor and product IDs - the key is per model, for
the whole machine, not per physical drive - and every other drive keeps
the controller's value. It is rounded to the nearest 4 KiB (at most 2048
bytes either way), so `65535` is 64 KiB, `2097120` and `2097152` are both 2 MiB, and `131072`
(`20000` hex) is 128 KiB exactly. A value that is missing, not a `DWORD` or
out of range is ignored. Restart Windows after setting it. It is the same
key Windows' `usbstor.sys` reads for a Bulk-Only drive, so tools and
guides written for that - Uwe Sieber's pages on `uwe-sieber.de` cover it -
set it for a UAS drive too, 2 MiB included. The UAS device's hardware key (under
`Enum\USB`, on Windows 2000 and later in its `Device Parameters`) shows
what was taken: `XhciUasCommandBytes` the size in bytes,
`XhciUasCommandSource` `1` for the controller's value and `5` for the
drive's own, `XhciUasDeviceLimit` the value read and
`XhciUasDeviceLimitWhy` `0` absent, `1` used, `2` not a `DWORD`, `3` out of
range. Behind a controller driver that does not answer the transfer-size
query (`2.2.0.0` and earlier) the UAS driver keeps 64 KiB and the drive's
value is not used (`XhciUasDeviceLimitWhy` `4`).

**Windows 98 SE and ME.** Both values are read, and neither changes what
reaches the drive: Windows' mapping layer, `USBNTMAP.SYS`, sends at most 64
KiB at a time whatever the UAS driver offers.

### XhciWideMap: two transfer pieces at once

On Windows 2000 and later the controller driver gives a large bulk
transfer to the controller two pieces at a time, the next one queued
before the current one ends, where `2.2.0.0` and earlier gave one piece and waited -
when this value turns it on. It is **off unless you set it to `1`**. It applies to
large bulk writes on any device, and to large bulk reads only on a UAS
drive's streams. It does so only where the machine's DMA needs no
translation - the check is made at every start, and again for each
transfer:

| | |
|---|---|
| Key | the controller's driver key, where the install writes the other `Xhci` values ("Registry settings", above): on 2000, XP, Vista and 7 `HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN`, the subkey whose `DriverDesc` is "xHCI98 USB 3 Controller" ("xHCI98 USB 3.x eXtensible Host Controller" up to `3.1.0.0`) (on Vista and 7 Device Manager's Details tab gives it as the "Driver key") |
| Value | `XhciWideMap`, a `DWORD`; change it with `regedit` (Modify) or a `.reg` file |
| Default | off: the install writes `0`, and a missing value means off too |
| On | `1`, then **restart Windows**; `0` and a restart turn it off again |

The decision rests on a reading of every Windows 2000, XP, Vista and 7 HAL
of the service packs these notes name, x86 and x64; the checks made at each
start and each transfer stand for later HAL hotfixes. Under Driver
Verifier's DMA checks, and on a machine whose DMA does need translation
(Windows 2000 running its PAE kernel; 32-bit Vista and 7, and every x64
system, where memory may lie above 4 GB), nothing changes: every transfer
goes one piece at a time, as in `2.2.0.0`. Windows 98 SE and ME never send a
transfer large enough for it to matter. On a UAS drive at SuperSpeed the
same switch also lets one command's data go to the controller as a single
piece of up to 2 MiB, on rings the driver makes larger for that drive's
streams (about 1.5 MiB of memory per drive while it is attached; if that
memory cannot be had the drive works as before). It is off by default
because a regression, though unlikely, would show as silently wrong data
rather than an error. If you turn it on and a large copy misbehaves, set
`0`, restart and report whether that changed it.

### XhciSelectiveSuspend: putting idle devices to sleep

New in `2.3.0.0`, and **off unless you set it to `1`**. Up to `2.2.0.0` the
driver never suspended a device: it answered Windows' power requests for
a device by recording them, so an idle device kept drawing power. With
this value at `1` the driver suspends a device when Windows asks it to -
when a device's driver puts it in a low-power state while the machine
runs, or, on XP and later, when a driver uses Windows' idle notification -
and wakes it when Windows asks again or the device signals a remote wake.
It never suspends a device on a timer of its own; whether and when a
device is put to sleep is the choice of that device's driver and of
Windows.

| | |
|---|---|
| Key | the controller's driver key ("Registry settings", above) |
| Value | `XhciSelectiveSuspend`, a `DWORD`; the install writes `0` |
| `0`, absent, any other number or not a `DWORD` | Off, the default: the driver behaves as `2.2.0.0` did, bit for bit |
| `1` | On, for USB 2.0 devices (High, Full and Low Speed, at a root port or behind a hub) and SuperSpeed devices alike |

Read at each start of the controller: restart Windows after changing it.
Hubs, the root hub, a composite device's own entry and the controller are
never suspended, so the hubs above a sleeping device stay awake; a
composite device's parts are suspended together, when all of them are
idle (their own entry stays in D0 in Device Manager meanwhile). A device
that cannot signal a remote wake is offered to Windows as able to sleep
but not to wake.

It is off by default; leave it off unless you need it. If a device
misbehaves with it on, set `0`, restart and report whether that changed it.

### XhciUsb2HubSpeedFallback: Low- and Full-Speed IDs behind a hub

New in `2.3.0.0`, and **on unless you set it to `0`**. A device behind a hub
has no port of the controller's own to report its speed, so the driver
writes a speed ID for it, looked up in the controller's speed table for the
USB 2.0 port the hub hangs from. Some controllers - the VIA VL805
(`1106:3483`) is one - list only High Speed there, the link to their own
built-in hub, and `2.2.0.0` refused every Low- or Full-Speed device behind
that hub. With this value on, such a device gets the standard ID (1 for
Full Speed, 2 for Low Speed) where the table names neither that speed nor
that ID; a table that names them is followed as before, and devices on the
controller's own ports are not affected.

| | |
|---|---|
| Key | the controller's driver key ("Registry settings", above) |
| Value | `XhciUsb2HubSpeedFallback`, a `DWORD`; the install writes `1` |
| Absent, `1`, any other number or not a `DWORD` | On, the default |
| `0` | Off: such a device is refused, as in `2.2.0.0` |

Read at each start of the controller: restart Windows after changing it. The
snapshot says whether it is on, how many devices it gave an ID and which
slots hold one, and on the VL805 the card's firmware version
(`pci.vl805.fw`). If a Low- or Full-Speed device behind such a hub
misbehaves with it on, set `0`, restart and report whether that changed it.

### XhciUsb2Bos: a USB 3 device's capabilities at High Speed (new in `3.2.0.0`)

A device whose device descriptor declares USB 2.1 or later (`bcdUSB`
0x0210, which every USB 3 device declares when it runs at High or Full
Speed) is asked for its BOS descriptor when it is enumerated at High or
Full Speed, as every device at SuperSpeed already is. The answer tells
the hub request `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2` that
the device can run at SuperSpeed, which the xHCI98 USB Tree's "can run
faster" notice reads. A device that refuses the request, answers it
badly or does not answer at all within 5 seconds is enumerated without
it; one whose enumeration then fails anyway is tried once more without
the request. Set `XhciUsb2Bos` to `0` (a `DWORD`, in the controller's
key or machine-wide) to stop the request for every device; absent or
any other value, the request is made. The install writes it as `1` from
`3.4.0.0`; the driver reads it at each start. The driver's log notes each such request
(`enum.bos.usb2`) and the switch (`enum.bos.usb2.on`).

### XhciResumeKeep: devices kept across such a resume (new in `3.4.0.0`)

Where a resume from standby or hibernation starts the controller afresh
(`XhciResumeReinit` in "Linux's controller quirks" below, every VIA
controller among them, and every controller that cannot save its state), a
device on one of the controller's root ports is restored in place on a new
slot, with its Device Manager entry, its drive letter and the files open on
it, instead of being removed and found again (roadmap-usb4 task 45.23,
design record 20 sections 4.3.1 to 4.3.3). A UAS drive is kept while in use
as well: at the sleep `xhciuas.sys` holds the requests that arrive, waits
out the commands in flight and asks each unit to write out its cache, and
after the wake it checks each unit's identity and medium before it starts
the held requests; a drive found changed (another disk behind the same USB
device) is reset and found again as a new one, never written as the old.
Still found again: a hub and every device behind it, a Bulk-Only drive, a
drive with no serial number or one that could not finish its requests before
the sleep, a drive whose medium cannot be checked after the wake (a
removable-medium drive with a medium in it, or one whose units report
neither identification page, record 20 section 4.3.3), a device with an
isochronous stream open (audio or video in use), and a device selective
suspend held. `XhciResumeKeep` (a `DWORD`, in the controller's key or
machine-wide) `0` turns it off; absent or any other value, it is on. The
install writes it as `1`.

### XhciAccept096: controllers below xHCI 1.0

New in `2.4.0.0`, and **on unless you set it to `0`**. A controller says
which revision of the xHCI specification it was built to in its HCIVERSION
register. `2.3.1.0` refused anything below 1.0; the first-generation
NEC/Renesas uPD720200 and uPD720200A (`1033:0194`) and ASMedia's ASM1042
are 0.96 parts. With this value on, a controller from 0.96 up starts. It is
driven as a 1.0 controller but for the fields Linux writes differently on
0.96 parts: the TD Size field in 1 KiB units rather than packets, the
control transfer's Transfer Type written 0, the isochronous burst counts
written 0, every isochronous transfer scheduled at the next opportunity
rather than at a named frame, and the capability register at offset 1Ch
ignored, so a resume from standby starts the controller afresh: up to
`3.3.0.0` its devices were found again, and from `3.4.0.0` a device on a
root port is kept where "XhciResumeKeep" allows.
On a 1.0 or later controller the value changes nothing.

| | |
|---|---|
| Key | the controller's driver key ("Registry settings", above) |
| Value | `XhciAccept096`, a `DWORD`; the install writes `1` |
| Absent, `1`, any other number or not a `DWORD` | On, the default: from 0.96 up starts |
| `0` | Off: below 1.0 is refused (Code 10), as in `2.3.1.0` |

Anything below 0.96 is refused at every value. Read at each start of the
controller: restart Windows after changing it. If a 0.96 controller
misbehaves with it on, set `0`, restart and report what it did, with `XHCIQUAL`'s report, which names
the controller's version, and the snapshot, which says what the driver
decided ("controller version"). With `XhciLogDebugView` at `1`, a start the
controller's initialization refuses (a Code 10 at any of its steps, the
version check among them) now writes its log and counters to DebugView,
the version among them; a start refused before that, for want of
resources or a register window, does not.
### Linux's controller quirks

New in `2.4.0.0`; the install writes each value at its default. Linux's
xHCI driver works around faults of particular controllers, each named by
its PCI vendor and device ID. This driver takes fourteen of those
workarounds, each behind a value named for what it does, and adds one
controller to `XhciIntervalCap` (above). The faults and the controller
lists are Linux's, read in its `drivers/usb/host/xhci-pci.c`; the code is
this driver's own, written from design record 20, which also says which of
Linux's quirks were not taken and why.

Every value reads the same way:

| Value | Effect |
|---|---|
| `0` | Off on every controller |
| `1` | On the controllers listed for it below: the default, except for `XhciShutdownReset`, whose default is `0` |
| `2` | On every controller, except `XhciIntelShutdownToEhci`, `XhciAsmediaFlowControl` and `XhciAmdSuspendBitClear`, which are vendor-specific (a `2` there is read as `1`) |
| absent, not a `DWORD`, or any other number | The default |

A controller is matched by its vendor and device ID alone; its revision is
read and logged, never matched. Each value is read when the controller
starts, so a change takes effect at the next restart, or at a disable and
enable of the controller. The snapshot has a "Linux quirks" block
naming each value with its Linux flag: the value as read (and whether it
was absent, of another type, an unknown number, or a `2` read as `1`),
whether this controller is on its list, whether the quirk applies, and what
it last did (done, skipped and why, or failed), then counts of what each
did.

- **`XhciMsiAvoid`** (Linux's `XHCI_BROKEN_MSI`; Fresco Logic `1B73:1000`
  and `1B73:1400`). These controllers offer a message-signalled interrupt
  and never raise it. Only Vista and 7 use one. Under `XhciInterruptMode`
  `0` the driver asks Windows for the interrupt line instead, and a message
  Windows grants anyway is not used: the controller is polled ("Windows
  Vista and 7: message-signalled interrupts"). `XhciInterruptMode` `1` and
  `2` are unchanged, and `3`, a message by name, is honoured, the report
  saying the quirk was overridden. `2` applies it on every controller.
- **`XhciStreamsOff`** (`XHCI_BROKEN_STREAMS`; Fresco Logic `1B73:1000`,
  `1B73:1009`; Etron `1B6F:7023`, `1B6F:7052`; VIA `1106:3432`; ASMedia
  `1B21:1042`; and Cadence `17CD:0200`, whose separate stream-context
  quirk in Linux is answered here by giving it no streams). The controller
  is treated as having no streams: a drive that offers Bulk-Only as well
  uses Bulk-Only, and a UAS-only drive at SuperSpeed is sent back to its
  USB 2.0 port and runs UAS there ("SuperSpeed and UAS"). `2` on every
  controller.
- **`XhciResumeReinit`** (`XHCI_RESET_ON_RESUME`; AMD `1022:15E5`; Etron
  `1B6F:7023`, `1B6F:7052`; Renesas `1912:0015`; every VIA (`1106`)
  controller; Phytium `1DB7:DC27`; ASMedia `1B21:3042`; Zhaoxin
  `1D17:9202`). These controllers do not keep their state across standby
  or hibernation, so the driver saves nothing and starts the controller
  afresh at the resume: up to `3.3.0.0` every device on it was found
  again, as on a controller without Force Save Context; from `3.4.0.0`
  a device on a root port is kept ("XhciResumeKeep"). `2` on every
  controller.
- **`XhciIntelShutdownToEhci`** (`XHCI_SPURIOUS_REBOOT`; Intel `8086:1E31`,
  `8086:9C31`, `8086:9CB1`). On some machines with these chipsets a
  computer shut down with its switchable connectors on the xHCI controller
  powers itself back on. At shutdown the driver hands every switchable
  connector back to the USB 2.0 controller, writing both routing registers
  `0`, whatever `XhciIntelPortSwitch` says: also at its default `0`, which
  otherwise writes nothing ("Intel 7-, 8- and 9-series chipsets: the port
  switchover"). A `2` is read as `1`.
- **`XhciShutdownReset`** (`XHCI_SPURIOUS_WAKEUP`; Intel `8086:9C31`,
  `8086:9CB1`). **Off by default.** Linux resets these controllers at
  shutdown because of spurious wakes. At `1` this driver resets the
  controller at shutdown, and only once it has proved the controller
  halted. Nothing read here shows the fault, and the E7250 (`9CB1`) shuts
  down without it; set it on a machine that wakes by itself after shutting
  down. `2` on every controller.
- **`XhciSuspendHaltDelay`** (`XHCI_SUSPEND_DELAY`; AMD `1022:145C`,
  `1022:15E0`, `1022:15E1`, `1022:43BB`). A 1.5 ms pause before the
  controller is halted for standby, hibernation or shutdown. `2` on every
  controller.
- **`XhciSuspendHaltTimeout`** (`XHCI_SLOW_SUSPEND`; Fresco Logic
  `1B73:1000`, every revision, where Linux names revision 04). The halt for
  standby, hibernation or shutdown may take up to 640 ms instead of 32 ms;
  the wait ends as soon as the controller halts, so a healthy one loses
  nothing. Every other halt keeps 32 ms. `2` on every controller.
- **`XhciAsmediaFlowControl`** (`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`; the
  ASMedia ASM1042A, `1B21:1142`). A vendor flow-control setting written
  through the controller's configuration space after each start, each
  recovery and each resume. One that cannot be made is logged and the
  controller goes on. A `2` is read as `1`.
- **`XhciTiIntervalCap`** (`XHCI_LIMIT_ENDPOINT_INTERVAL_7`; TI's
  TUSB73x0, `104C:8241`). An interrupt endpoint of a High-Speed or faster
  device that asks to be polled less often than every 8 ms is polled every
  8 ms. Low- and Full-Speed devices and isochronous (audio) endpoints are
  never capped. It is applied after `XhciIntervalCap`. `2` on every
  controller.
- **`XhciStuckLinkReset`** (`XHCI_MISSING_CAS`; Intel `8086:22B5`,
  `8086:9D2F`, `8086:A12F`, `8086:5AA8`, `8086:19D0`). After a resume that
  restored the controller's state, a USB 3 port found stuck in its link's
  Polling state, with nothing attached reported, is warm-reset once, so a
  device there can be found. A port stuck in Compliance was already reset
  on every controller. `2` on every controller.
- **`XhciStreamRingPad`** (`XHCI_TRB_OVERFETCH`; the VIA VL805,
  `1106:3483`; Zhaoxin `1D17:9202`, `1D17:9203`). These controllers read
  past the end of a ring, so each block of UAS stream rings is given one
  ring's worth of spare memory after it. `2` on every controller.
- **`XhciControlLinkNoOp`** (part of `XHCI_ETRON_HOST`; Etron's EJ168,
  `1B6F:7023`, and EJ188, `1B6F:7052`). A SuperSpeed device's control
  transfer that would begin at the last place before its ring wraps is
  moved past the wrap with a No Op, so its stages are not split. `2` on
  every controller, still for SuperSpeed devices only.
- **`XhciIsochSpuriousSuccess`** (part of `XHCI_ETRON_HOST`; the same two).
  On an isochronous (audio) endpoint, a success the controller reports for
  a packet it has already answered with an error is dropped and counted.
  `2` on every controller.
- **`XhciAmdSuspendBitClear`** (`XHCI_DISABLE_SPARSE`; AMD `1022:15E5`).
  Bit 17 of the controller's register at offset `C12Ch` is cleared before
  each standby, hibernation and shutdown, as Linux does; neither Linux's
  source nor this driver says what the bit does. Only where that register
  lies inside the controller's memory window. A `2` is read as `1`.

**What happens at shutdown is seen only in a capture of that shutdown.**
`XhciIntelShutdownToEhci` and `XhciShutdownReset` act as the machine shuts
down, and log what they did, but the log lives in memory and the next start
cannot read it, so the snapshot never shows it. The driver sends those lines
out before the shutdown goes on, waiting for its thread for at most half a
second, and they reach you only with `XhciLogVerbosity` at `2` or above
(below it the lines are not recorded at all) and `XhciLogDebugView` set to
`1`, with a debug-output viewer or a kernel debugger attached to that
shutdown ("The log, and how to send one").

### XhciLogVerbosity and XhciLogDebugView: the log

See "The log, and how to send one". `XhciLogVerbosity` defaults to `2`
since `3.0.0.0` (`0` before; an update keeps the value there);
`XhciLogDebugView` defaults to `0`.

### The 1.2.0.0 virtual-hub values have no effect

`XhciVirtualHSHub`, `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, which a
`1.2.0.0` install wrote, are not read by any `2.x` release and its INFs
write none of them. A value left behind by a `1.2.0.0` install has no effect. The driver
reports every device at its true speed with no virtual hub in the way, so
there is nothing for them to switch. Delete them if you want them gone.

## The controller's Advanced tab and the root hub's Power tab

The controller's properties carry an **Advanced** tab and the root hub's a
**Power** tab, on every target, as Windows' own USB controllers do, where
`usbui.dll` is present: on Windows XP x64 and Server 2003 x64 the INF
does not fetch it (from `3.4.0.0`; "Installing"), so there the two tabs
appear only where another USB driver placed it or, from `3.4.2.0`,
`SETUP.EXE` copied it from Windows' own files. The
pages are Windows' own (`sysclass.dll` on Windows 98 SE and ME, `usbui.dll`
on the NT targets, with `usbui.dll` drawing the Windows 98 dialogs), and the
driver answers the requests they send. The Power tab reports the power
budget the driver itself keeps, since external hubs are the driver's own.
Since `2.1.0.0` each external hub's entry has a Power tab of its own, for
the devices behind it.

Up to `2.0.0.0` the Power tab showed every device's power as unknown, on
every target: the driver refused the descriptor request the page sends,
whose request code Windows leaves at zero. `2.1.0.0` answers it: the root
hub's and each hub's Power tab list each device's power in mA, as the device
declares it. Two things the pages show rest on Windows' own arithmetic:

- **Bandwidth counts isochronous pipes in use, and nothing else.** On
  Windows 98 SE to XP the Advanced tab adds up only the isochronous pipes a
  device has open, as it does over Microsoft's own stack, so a mouse, a
  keyboard or a drive adds nothing, and an audio device adds its share only
  while it plays or records. On Vista and 7 the figure comes from a WMI
  query the driver does not answer, and stays at zero.
- **A SuperSpeed device's power: 510 mA means 510 mA or more.** The page
  doubles the configuration descriptor's `bMaxPower`, which is in 2 mA units
  at USB 2.0 and in 8 mA units at SuperSpeed, so under `2.1.0.0`, the first
  release to answer the page, a SuperSpeed device read a quarter of its
  draw. Since `2.1.1.0` the driver hands the page, and only the page, a
  SuperSpeed device's value converted to 2 mA units. That byte cannot count
  past 510 mA, so a device declaring up to 510 mA reads its own figure and
  one declaring more reads 510 mA. Every other reader of the descriptor gets
  the device's own bytes. A bus-powered hub is not counted this way: the
  page charges it (ports + 1) x 100 mA, at most 500 mA, whatever it
  declares. The exact figure is in the snapshot, with `XhciLogVerbosity` at
  `2` or above ("The log, and how to send one"): `dev.ss.maxpower.ma`, the
  declared current in hexadecimal mA, after the device's `dev.ss.vidpid`: a
  SuperSpeed drive declaring 896 mA reads 510 mA on the page, and the report
  shows `dev.ss.maxpower.ma=00000380` (896 mA).

### A device's speed on the hub pages (new in `2.3.0.0`)

On Windows 98 SE and ME the newer form of the connection information
(`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`, which tools such as
SweetLow's USBView ask for) reads 3 for a SuperSpeed device and 4 for a
SuperSpeedPlus one, as Intel's USB 3 drivers for those systems report it;
up to `2.2.0.0` both read High Speed. Windows' own pages on those systems
send the older form, which says only whether a device is Low Speed, and
are unchanged. On the NT systems the newer form still reads High Speed,
because Vista's and 7's hub page would list any other value among the
Full-Speed devices. Every system also answers the request Windows 8
introduced (`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`), which tools
use to tell SuperSpeed and SuperSpeedPlus apart: it gives the port's
protocols and whether the device runs at, and can run at, SuperSpeed or
SuperSpeedPlus. A USB 3 device running at High Speed is not reported as
SuperSpeed-capable, since the driver does not read that part of its
descriptors at High Speed.

## The log, and how to send one

The driver keeps a small log of what happened on the bus, inside itself, and
the xHCI98 USB Tree (`XHCITR.EXE`, `XHCITR64.EXE` on 64-bit Windows, in the
download's `tools\xhcitr\` directory and installed with the drivers) reads
it off the running machine. Up to `3.0.0.0` that reader was `XHCISNAP.EXE`
with its `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT`; from `3.1.0.0` they
are gone and the tool does what they did ("What `3.1.0.0` changes"). From
`3.0.0.0` the driver records by default: a fresh install sets
`XhciLogVerbosity` to `2` (the log of what happened, with no internal
addresses); an update keeps the value an earlier install wrote, which was
`0`, off, until `2.4.0.0`. Recording costs a little speed: on one machine
(a ThinkPad P14s Gen 1 under 98 SE, a USB disk) about 1 to 2% at large
transfers and up to about 5% at small ones. For the last few percent set
the level to `0` in the xHCI98 USB Tree's View > Logging level... (for
either driver), and restart.

To send a report:

```
  1.  View > Logging level...: check xhci98 reads 2 (set 2 and restart if not)
  2.  make the problem happen again
  3.  File > Save Report...: the tree, both drivers' logs and the snapshot
  4.  attach the .TXT it wrote to the bug report
```

The tree's last item, "PCI and bridge diagnostics", shows on screen
what the drivers recorded about PCI bridges and interrupts (`XHCIPFW`'s
log, the connection manager's bridge records on 98 SE and ME, and on
2000 the interrupt given to a device plugged in behind a tunnel); Edit >
Copy with nothing selected copies it, and the report carries it too.

`XHCITR /report <file>` writes the same report from a prompt, and
`XHCITR /setlevel xhci98=2` sets the level (on 2000 to 7 as an
administrator; on Vista and 7, an administrator's prompt). Each driver
reads its level only when it starts, so a change needs a restart of
Windows; the tool offers one.

The report ends with a "Snapshot" part for each xHCI controller
`xhci98.sys` drives: the device report (what the `.DEV` file below holds,
once) and the decoded dump (the `.TXT` below). At level 4 the dump can
hold internal addresses, so the report leaves it out and says so: use
level 2.

When a maintainer asks for the snapshot's files themselves, File > Save
Snapshot... writes them in a folder you pick, for the first controller,
and `XHCITR /snapshot` writes them in the current directory, under a name
made from the local date and time, `mDDhhmmS` (the month as one
character, `1` to `9` then `A`, `B`, `C`; the day, hour and minute; then
the first letter `A` to `Z` not yet taken), so a second run never
overwrites the first; it prints the names:

- `mDDhhmmS.DEV`: the controller as Windows sees it - its Device Manager
  status and problem code, the resources it was given, and the versions and
  SHA-256 hashes of `xhci98.sys`, `xhciuas.sys`, `xhcihlpr.sys`, `usbd.sys` and
  `usbui.dll` as installed, and of `xhcimsi.sys`, the `2.3.x` helper, where
  it is still on disk. The tool reads these itself, through the
  configuration manager, so every target reports the same.
- `mDDhhmmS.TXT`: the dump, plain text with no internal addresses in it
  below level 4.
- `mDDhhmmS.BIN`, the driver's raw internal state, which only a maintainer
  holding the exact build can decode, and `mDDhhmmS.PSC`, the raw port
  registers.

`XHCITR /snapshot MYPC` saves the files as `MYPC.DEV`, `MYPC.TXT`,
`MYPC.BIN` and `MYPC.PSC` instead of the clock's name (`/o MYPC` is the
same): 1 to 8 letters, digits, `_` or `-`, not starting with `-`, with no
path, drive or extension, and not a DOS device name (`CON`, `PRN`, `AUX`,
`NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`), in the same directory. The tool holds
the name to that rule itself, and refuses a name any file of whose set is
already there rather than overwrite it. `/c N` picks the controller
(`\\.\XHCI98HCN` on 2000 and later, or `\\.\HCDN` from a driver
before `3.3.0.0`; `\\.\HCDN` on 98 SE and ME; default 0). `XHCITR /dump [/o NAME]` writes the dump alone,
`NAME.TXT` with `NAME.BIN` and `NAME.PSC` beside it, replacing an earlier
set of that name; `XHCITR /probe` checks the route to the driver and
dumps nothing; `XHCITR /snapshot /?` is the long help. With the output
redirected to a file the text goes there; otherwise a message box shows
it, with the directory the files went to.

`/snapshot`'s last line names the files to attach (`Attach: A131554A.DEV
A131554A.TXT (the .BIN and .PSC only if asked)`), or says there is nothing
to attach. When the snapshot does not go through, it says so by its exit
code (`docs/contributing/snapshot-reader.md` has the same table):

| Exit | What it means | What to do |
|---|---|---|
| 0 | the `.DEV` and the `.TXT` were written in full | attach them |
| 2 | the name given was refused; the line above says why | give a name by the rule above, or no name |
| 3 | the `.TXT` is not complete | attach it, and say so |
| 4 | no dump was taken; the `.DEV` says why | often the log is off: Logging level 2, restart, reproduce, run it again; attach the `.DEV` |
| 5 | the `.DEV` could not be written in full | the disk is full or cannot be written: run it in a directory that can be written |
| 6 | no free name: all 26 for this minute are taken, or the name given is taken; nothing written or overwritten | wait a minute and run it again, or give another name, or none |
| 7 | the clock is not a valid date and time; nothing written | set the clock and run it again |
| 8 | another run made a file of this set while this one ran; that file was left as it was, and no dump was kept under the name | run it again: it takes the next letter (with a name, give another, or none) |

A controller Device Manager shows as disabled (Code 22) on Windows 98 SE or
ME still gives a dump; the `.DEV` and the text then say "dump taken from
a controller Device Manager reports as disabled (Code 22)", so that the
dump is read as what the driver held while Windows had it disabled.

`XHCISNAP`'s level switches did not move to the tool: `-verbosity`, `-on`,
`SNAPON.BAT`, `-disable` and `SNAPOFF.BAT` are View > Logging level... and
`XHCITR /setlevel`, and a dashed switch after `/snapshot`, `/dump` or
`/probe` is refused. On an install up to `3.0.0.0`, use that release's own
`XHCISNAP.EXE` and its notes.

From `2.3.0.0` the top of the `.TXT` also says which system it came from:
the Windows version, build and service pack, 32-bit or 64-bit, the
processor, the memory, and the make, model and BIOS where that Windows
records them (on Vista and 7 the make and model; on 2000 and XP, 98 SE and
ME the BIOS only). It does not print the computer's name, your user name or
the domain, and does not read any serial number; it reads those names only
to withhold a firmware string that contains one (a name of three characters
or more). The file is meant to be pasted into a public issue as it is, but
if your machine's firmware strings were customised, read the top of the
file first. If reading those details causes a problem on your machine, add
`/nosysinfo` to `XHCITR /snapshot` or `/dump` (`XHCITR /snapshot
/nosysinfo`): the dump then says they were skipped and is otherwise the
same.

| Value | What it does |
|---|---|
| `XhciLogVerbosity` | `0` off (the driver does not answer the snapshot at all); `1` counters only; `2` adds the log of what happened (use this one); `3` adds the USB port register table; `4` everything, including internal addresses in the plain-text report. The default is `2` since `3.0.0.0` (`0` before; an update keeps the value already there). A value outside `0`-`4` is treated as the default |
| `XhciLogDebugView` | `1` hands the log to a debug-output capture tool (DebugView) from the driver's own thread as it runs, and once more at each stop. Use it only when asked |

Since `2.2.0.0` the report also says what happened to a device on a root
port that did not appear. At `XhciLogVerbosity` `2` or above each root
port's enumeration leaves a few short `enum.port.` records - why the port
was looked at, the reset's result, the raw speed ID and how it was read,
Enable Slot's result, and a failure's cause - decoded in the report under
"root port enumeration notes, decoded", a few for each port between
enumerations so a port that keeps failing cannot fill the log. And from
`XhciLogVerbosity` `1`, each root port's enumeration state and failure
cause, and the driver's counters, are in the report as well, which
`2.1.1.0`'s was not. `docs/contributing/snapshot-reader.md` lists every record. A report
from `2.1.1.0` on a machine such as the E460 above showed the port trained
and nothing more; one from `2.2.0.0` names the step that stopped.

### Both logs in the xHCI98 USB Tree (from `3.0.0.0`)

The xHCI98 USB Tree (`XHCITR.EXE`, see "The xHCI98 USB Tree" in the
README) shows the logs of both drivers, read from the running drivers, with
no debug build and no DebugView, and needs no administrator rights to do
it. Each xHCI controller `xhci98.sys` drives has a "Driver log (xhci98)"
node under it, and each Thunderbolt or USB4 controller a "Driver log
(xhcicm)" node. Select the node to read the log: the details pane shows
whether the driver is recording, its counters, and the log, oldest record
first. Save Report (and `XHCITR /report <file>`) writes both logs and the
counters into the report, as plain text with no internal addresses in it.
With a Thunderbolt or USB4 report on Windows 2000 to 7, also send the
safe-removal mark service's log (from `3.1.0.0`),
`%SystemRoot%\System32\LogFiles\xhci98\XHCIMARK.LOG`: one line for each
mark it wrote or removed, each check that stopped it, and each restart it
made.

The tool reads `xhci98.sys` through a query request of the driver's own,
as it reads `xhcicm.sys`; an `xhci98.sys` older than `3.0.0.0` is read
through the driver's door, as `XHCISNAP` read it.
Both drivers record at level 2 by default. Where a driver's recording
is off, the node says how to turn it on; View > Logging level... (or a
right click on the node) sets either driver's level directly, asking for
administrator rights on 2000 to 7 when the user has none:

- **`xhci98.sys`**: it records at level 2 after a fresh install. Where an
  earlier install left it off, set level 2 in View > Logging level...
  (or `XHCITR /setlevel xhci98=2`), then restart Windows. At level 1 the node shows
  the counters only. At level 4 it shows the counters but not the log,
  because that level records internal addresses: use level 2.
- **`xhcicm.sys`**: set the DWORD value `XhciCmLogVerbosity` to `2` on the
  Thunderbolt or USB4 controller's driver key, then restart Windows. The
  node names the key in full (the "Driver key" its details show, under
  `HKEY_LOCAL_MACHINE`). On 2000 and later that needs an administrator.
  Reading the log writes nothing. Its counters are shown whatever the
  level.

`xhcicm.sys` also prints its log to a debugger's output (DebugView) when
`XhciCmLogDebugView` is `1` on the same key, in every build, from its own
thread and never from an interrupt, as `xhci98.sys` does under
`XhciLogDebugView`. Below level 4 it prints no address. The tool's node
needs neither.

`xhcicm.sys` keeps the last 16 KiB of its log, from when Windows first
loaded it for the controller; a restart of the controller does not clear
it. The tool reads it without removing anything, so it can be read again.
Three of its records hold an address as their value; the tool shows those
values as `--------`.

From `3.1.0.0` the same report also carries `xhci98.sys`'s snapshot (the
device report and the decoded dump), so it is the whole bug report; the
raw `.BIN` and `.PSC` files come from File > Save Snapshot... or
`XHCITR /snapshot` when a maintainer asks for them.

The log cannot capture a crash: a machine that has bugchecked is not running
for anything to read.

> **Set the level back to 2 (or 0) once you have sent a capture taken at
> level 3 or 4.** While the level is above 0, anyone using the machine can
> read the driver's diagnostic state through the tool, and at level 4 that
> includes internal addresses in the raw dump.

> **Do not run DebugView on Windows 98 on real hardware.** Under the `1.x`
> miniport, plugging in a device while it captured crashed a ThinkPad E460
> three times over. The `2.x` driver prints only from its own thread rather
> than from interrupt context, which is what that crash was traced to, but
> the warning stands.

## Known limitations

- **Windows 98 SE and ME: a USB disk left plugged in when the computer
  starts can hold Windows at startup for minutes, or stop it.** Plug USB
  disks in after Windows has started. To help find a fix, you can send an
  EDD report from your computer ("EDD reports",
  `docs/using/edd-reports.md`).
- **Windows 2000, XP and XP x64: a USB 3 controller the BIOS gives these
  systems no interrupt for** (new in `3.4.2.0`; some HP laptops of 2012
  with the Intel 7 Series USB 3 controller). Up to `3.4.1.0` XP and XP
  x64 stopped it at Code 12 ("This device cannot find enough free
  resources that it can use"), and Windows 2000 with an ACPI computer
  type stopped at start with STOP 0x000000A5. From `3.4.2.0` the driver
  offers Windows the line the chipset routes the controller to, on the
  listed computers by default (`XhciIntelNoRouteIrq` `1`; `2` on any of
  the Intel chipsets `XhciIntelIrqRoute` lists), and where Windows can
  give no line the controller starts without one and is polled, which
  uses a little more processor time. On Windows 2000 on the listed
  computers the driver asks for no interrupt and the controller is
  polled. On a computer whose BIOS turns the controller on before
  Windows starts, Windows 2000 with an ACPI computer type still stops at
  start: install it there as "Standard PC" (F7 in text-mode Setup). On
  the listed computers `XhciIntelNoRouteIrq` `0` brings the stop back
  under an ACPI computer type. If `0` was set after the last start that
  reached the desktop, "Last Known Good Configuration" at F8 undoes it;
  otherwise only "Standard PC" starts Windows 2000 there. On these
  computers the BIOS leaves the USB 3
  ports on the USB 2.0 controllers under XP and XP x64: set
  `XhciIntelPortSwitch` to `1` for them to reach the USB 3 controller
  ("An interrupt where the firmware gives none", `docs/using/tuning.md`).
- **Stor98: a drive with 4 KiB sectors partitioned in one view cannot be
  read in the other** (from `3.4.2.0`). Windows 98 SE to 7 see such a
  drive with 512-byte sectors, Windows 8, 8.1 and 10 with its own
  4096-byte sectors; a drive partitioned on one side sees its partitions
  and files as damaged on the other. Back up first. To move a drive
  freely, reformat the drive itself to 512-byte sectors with its maker's
  tool, or partition and format it again on the system that will use it
  (Stor98's README, "Known limits").
- **Windows 2000: the first update from `3.2.0.0` or earlier may end at
  Code 31 and ask for a restart, once.** Up to `3.2.0.0` a second
  controller, or Windows' own EHCI driver starting after this one, could
  attempt to create the controller's `\DosDevices\HCD<n>` name while it
  was held, and on Windows 2000 that kept the driver loaded, so the
  controller's next start failed with Code 31 until Windows restarted.
  From `3.3.0.0` the controller's name on Windows 2000 to 10 is
  `\DosDevices\XHCI98HC<k>`, with an `HCD<m>` alias from `HCD9` down on
  2000, XP and XP x64, so Windows' USB control panel (usbui's walk) still
  lists it there (roadmap-usb4 task 44.81). The fix works from the first
  start of `3.3.0.0`: an update from an earlier release on a 2000 machine
  with two controllers on this driver, or with an EHCI on Windows' driver
  beside one, may still end at Code 31 and ask for the restart, once.
  XHCITR `3.2.0.0` reaches this driver only through the `HCD<m>` alias
  (`/c m` for `HCD<m>`, usually 9) on 2000, XP and XP x64, and not at all
  on Vista and later; use the `3.3.0.0` one.
- **Windows 2000 updated from `3.2.0.0`: an internal NVMe drive added later
  can start on Stor98.** `3.2.0.0` matched Stor98 by the NVMe class. When it
  installed Stor98 on a tunnelled drive, Windows 2000 wrote the Critical
  Device Database key `pci#cc_010802` with Service `stor98`. The `3.3.0.0`
  update removes `3.2.0.0`'s `stor98.inf` but leaves that key. An internal
  NVMe drive added after the update then starts on Stor98 with no wizard, as
  it did under `3.2.0.0`. A fresh `3.3.0.0` install does not write that key.
  To remove it, delete
  `HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase\pci#cc_010802`
  with Regedit as an administrator when its `Service` is `stor98`, then
  restart. XP may do the same. `3.4.0.0` does not remove the key either
  (roadmap-usb4 task 45.1, dropped): the update over `3.2.0.0`, also from
  `3.3.0.0`, leaves it.
- **Selective suspend is off unless you turn it on, and hubs are never
  suspended.** With `XhciSelectiveSuspend` at its default `0` no device is
  suspended to save power, as up to `2.2.0.0`. At `1` a device is
  suspended when Windows asks, but a hub, the root hub and the controller
  never are, and the driver never suspends anything on a timer of its own.
  See "XhciSelectiveSuspend: putting idle devices to sleep".
- **On Windows 98 SE and ME the free and non-free drivers left unticked
  are copied, not staged.** The installer copies them to `USBDRV\FREE\`
  and `USBDRV\NONFREE\` in the install directory and adds to `DevicePath`
  only the folders of the packages ticked on the Free USB drivers and
  Non-free USB drivers pages (from `3.4.0.0` every package a set offers
  for staging on the system, tasks 45.7 and 45.8; in `3.3.0.0` only the
  free `uac2` and, on 98 SE, `usbmidi`). A long `DevicePath` stops 98
  SE's hardware wizard finding any driver, Windows' own in
  `C:\WINDOWS\INF` included, past 280 characters, and ME's hardware
  wizard stops responding past 259, so the installer keeps it within the
  system's limit and greys a box that would pass it.
  When the wizard asks for a driver, choose "Specify a location" and point
  it at the package's folder, `USBDRV\FREE\<class>\<package>\<folder>` (or
  `USBDRV\NONFREE\...`). A later run whose box is left unticked keeps
  an earlier run's staging (its page lists it); the uninstall removes
  what this installer placed and no device uses.
- **No USB storage on a stock Windows 98 SE without NUSB's storage files.**
  From `3.2.0.0` the download carries NUSB 3.3's seven storage files in
  `USBDRV\NUSB33\` and the installer offers them on Windows 98 SE; see
  "Windows 98 SE and ME: the stock install, and storage".
- **Windows Vista x64 and Windows 7 x64 need test-signing mode, or driver
  signature enforcement disabled.** The x64 drivers carry the project's own
  test signature, not Microsoft's. Turn on test-signing mode once (see
  "What this is"), or disable driver signature enforcement at every start
  (F8); otherwise the controller sits at Code 39 on Vista, Code 52 on 7, and
  nothing on it works.
- **Windows ME: plugging and unplugging a USB mouse (or another USB HID
  device) rapidly, over and over, can stop the machine.** The taskbar clock
  stops, typing does nothing, and only a restart ends it. The wait is in
  ME's own `hidusb.sys` (4.90): when a HID device's start fails because the
  device has already been pulled, ME stops it, and the removal that follows
  waits for ever inside `hidusb.sys`, before this driver is asked anything.
  Windows 98 SE's `hidusb.sys` does not have this flaw. Any USB stack can
  reach it, ME's own included: it can come after several back-to-back plug
  and unplug cycles under this driver, and ME's own USB stack, on a UHCI
  controller, stops the same way when each mouse is pulled a second after
  its plug. This driver starts a new device no later than ME's own stack
  does. Plugs a few seconds apart, each left until the device worked, did
  not reach it. From `3.4.2.0` a device pulled before its start is
  answered until ME removes it, so its start no longer fails for that; a
  device pulled while its class driver is starting can still reach the
  wait.
- **Windows 98 SE: a USB audio device can wedge the machine as it loads, and
  it is not this driver's doing.** After a cold boot, attaching a USB audio
  device wedges the machine often, with the taskbar clock stopped and
  "Msgsrv32" not responding. Every request the driver was given had been
  answered: the wait is above it, in Windows 98's configuration manager.
  Windows 98 SE's own USB stack on a UHCI controller, with this driver not
  loaded, wedges the same way at about the same rate, whether the device is
  attached soon after the boot or minutes later, with selective suspend on
  or off, and `1.2.0.0` and `2.2.0.0` showed it too. Waiting after the boot
  does not avoid it. Only a restart ends it. Likewise, USBView walked and
  closed while a USB audio device was attached often froze the machine, or
  stopped it with a fatal exception in `USBAUDIO`, on Windows' own stack as
  well; with no audio device, or no USBView, it never did. It is the first
  USB audio device to arrive after a boot that can freeze 98 SE (a sound
  card, a headset, a USB MIDI or audio interface, or a webcam with a
  microphone), whatever its driver: seen with Windows' own `usbaudio.sys`
  and with usb-wdm-free's `uac2`, under Windows' own UHCI driver, NUSB and
  `xhci98.sys` alike; once a first plug after the boot had come through,
  later plugs in that boot did not freeze, but a device that came up with
  Windows at the start does not protect the next plug (unplugged and plugged
  back, it often froze). For `3.2.0.0` it is documented, not fixed
  (the owner's decision). What helps: **plug the device in before switching
  on and leave it plugged in until shutdown**; if Windows freezes at a plug,
  restart with the device plugged in; and for a webcam whose microphone you
  do not need, usb-wdm-free's `NOAUD98.INF` gives the microphone no driver,
  so no audio driver starts and the camera keeps working (usb-wdm-free's
  README, "98 SE may freeze when the first USB audio device is plugged in
  after a boot").
- **Windows 98 SE: stop audio playback before unplugging a USB audio
  device** (from `3.4.2.0`). Unplugged while Windows' own USB audio
  driver (`usbaudio.sys`) plays it, the device can freeze Windows. An
  unplug with nothing playing is not affected. On Windows ME `3.4.2.0` fixes it; with an earlier release,
  stop the playback first there too.
- **Windows 98 SE: a keyboard's media keys.** Its volume and mute keys
  work after a restart; its other media keys (play, next, the browser and
  mail keys and the like) need the keyboard maker's software. ME, 2000 and
  XP handle them.
- **Windows 98 SE and ME: a USB Audio Class 2.0 device needs the free UAC
  2.0 driver before its first plug.** Windows' own USB audio driver, which
  matches every USB audio device but knows USB Audio Class 1.0 only, takes
  such a device by itself at its first plug unless a better driver is ready;
  with a Sound Blaster X4 it stopped Windows with a fatal exception 0E in
  `USBAUDIO`. From `3.3.0.0` tick `uac2` on the installer's Free USB drivers
  page (the `-free` download) before plugging the device in: `uac2` matches
  USB Audio Class 2.0 devices by their class
  (`USB\Class_01&SubClass_00&Prot_20` and `&SubClass_01&Prot_20`), which
  ranks above Windows' `USB\CLASS_01`. Otherwise, or if Windows already
  stopped: unplug the device and restart (in Safe Mode if Windows stops at
  the start), then in Device Manager open its audio entry, Update Driver,
  "Display a list of all the drivers in a specific location", Have Disk, and
  point it at `C:\XHCI98\USBDRV\FREE\audio\uac2\w98se-me`; restart with the
  device plugged in.
- **Windows 98 SE and ME: the free audio and MIDI drivers may ask for
  Windows' own `.cat` files.** Installing `uac2` or `usbmidi`, Windows asks
  for `wdmaudio.cat`, `ks.cat` and the other kernel-streaming files of its
  own WDM audio INFs, from the Windows CD. Point it at the CD's `WIN98`
  folder, or click Skip File for the `.cat` files: they only hold
  signatures, which 98 SE and ME do not check, and the device works
  without them.
- **Windows 2000 to 8.1: a USB Audio 2.0 or 3.0 device that already
  shows as "USB Audio Device" keeps Windows' own USB Audio 1.0 driver**
  (task 45.24). From `3.4.0.0` such a device is no longer offered to that
  driver, but Windows does not choose again for a device it already
  installed, and on XP no change of it completes until a reset. Unplug
  the device, restart Windows (reset the computer if the shutdown hangs),
  run `cmd /c "set devmgr_show_nonpresent_devices=1&& devmgmt.msc"`,
  choose View, Show hidden devices, update the greyed "USB Audio Device"
  to `uac2`, then plug the device in.
- **A device with more than one configuration that the split rules allow
  is split by its first one, so a driver of its maker that takes the
  whole device no longer binds**
  (from `3.4.0.0`, task 45.34; design record 13 section 10.8). Windows' own
  stack gives such a device whole to one driver; under xHCI98 its
  functions get drivers of their own. A list of devices to keep whole can
  be added if one is reported.
- **After a resume that starts the controller afresh, some devices are
  still found again** ("XhciResumeKeep"): a hub and everything behind it,
  a Bulk-Only drive, a drive with no serial number or one that could not
  finish its requests before the sleep, a drive whose medium cannot be
  checked after the wake (a removable-medium drive with a medium in it,
  or one whose units report neither identification page), a device with
  an isochronous stream open (audio or video in use), and a device
  selective suspend held. A file open on such a drive across the
  sleep is lost, and cached writes with it.
- **Windows 98 SE: a device that holds a shared IRQ with no working
  driver makes Windows crawl or freeze once this driver is installed**
  (task 45.33; ME is expected to behave the same). Windows 98
  keeps such a line masked while no driver uses it; once any driver
  connects to it, Windows unmasks it and the holder's interrupt storms,
  which xHCI98's interrupt routine rightly declines. Seen on a desktop
  board whose system NVMe drive the
  BIOS still ran (System Properties, Performance: "MS-DOS compatibility
  mode") on a line shared with three xHCI controllers. Look in Device
  Manager (Computer, Properties, View Resources, Interrupt request (IRQ))
  for a device without a working driver on an xHCI98 controller's IRQ.
  Give it a working driver or remove it; or, if no other working driver
  uses that IRQ, disable the xHCI98 controllers on it, or set
  `XhciInterruptMode` to `2` (polling) and restart, which leaves the line
  masked with USB working (`docs/using/tuning.md`).
- **Windows 98 SE: a device at a location the system has not seen before
  raises the Add New Hardware Wizard, and holds that port until it is
  answered.** While the device stays plugged in, that hold is per port and
  by design of Windows 98. **Finish the
  wizard before unplugging the device.** A device unplugged while its
  wizard is open holds more than its port: Windows asks the bus about no
  port again until the wizard is closed, so neither that device plugged
  back in nor a device plugged in, at any port, where another was
  unplugged meanwhile is seen until then; closing the wizard brings them
  all at once. Keys typed into the abandoned wizard wedged the machine.
  The same happens on Windows 98 SE's own USB stack and under `2.2.0.0`.
- **Windows 98 SE: remove USB disks before Stand by.** With a USB disk that
  was attached when the computer started, the resume stops before the
  desktop returns, under this driver and under NUSB's USB 2.0 (EHCI) driver
  alike. The BIOS presents such a disk to Windows, and Windows 98 SE then
  uses it in MS-DOS compatibility mode (System Properties, Performance tab).
  A disk plugged in after Windows started does not stop the resume. With
  only a USB mouse attached, at a root port or behind a hub, the resume
  completes and the mouse works through this driver.
- **Windows 98 shows no driver version on the Driver tab**, only the file
  date; the four-part version is under Driver File Details. (Up to
  `3.2.0.0` that list could also show `xhci98.tmp`, an earlier install's
  leftover temporary copy, which could replace `xhci98.sys` at the start
  after a controller was added; from `3.3.0.0` the installer deletes such
  a leftover, roadmap-usb4 task 44.13.)
- **Windows ME: do not unplug a device while Windows is installing it.**
  ME's own device manager stops responding; it does the same on Microsoft's
  own USB stack.
- **Windows ME: Shut Down can hang with a USB disk that has not been opened
  since the start.** Open the disk once (a `dir` of it is enough), or
  remove it, before shutting down. ME does the same on Microsoft's own USB
  stack, and `2.2.0.0` did too.
- **Windows ME: Shut Down can hang after the controller was disabled and
  enabled in Device Manager** in the same session: a black screen that
  stays, the processor busy. After such a cycle, save your work before
  shutting down; if Shut Down hangs, switch the computer off.
- **Windows ME: a UAS drive as the first USB storage device shows Code 2.**
  On a fresh Windows ME, ME has not yet copied its own `USBNTMAP.SYS` and
  `USBMPHLP.PDR`, which it installs when its first ordinary USB stick is
  plugged in. Plug in any ordinary USB stick once, then unplug the UAS drive
  and plug it back in; no Remove and no restart are needed. From `3.0.0.0`
  `INSTALL.BAT` gives this step for a UAS drive it finds at Code 2, and on a
  Windows ME that has not yet copied the file it says so before the first
  UAS drive is plugged in.
- **Windows Vista and 7: the controller's Advanced tab shows no bandwidth.**
  The figure comes from a query the driver does not answer, and stays at
  zero. See "The controller's Advanced tab and the root hub's Power tab".
- **A SuperSpeed device, other than a bus-powered hub, declaring more than
  510 mA reads 510 mA** on the
  Power tab, on every system: the value the page reads cannot count past
  510 mA. The snapshot gives the exact figure. Same section. (Under
  `2.1.0.0` every SuperSpeed device read a quarter of its draw there.)
- **Intel 7-, 8- and 9-series chipsets, with the switchover turned on
  (`XhciIntelPortSwitch` `1` or `2`; off by default since `2.3.0.0`): a
  device on a blue connector moves between the USB 2.0 controller and this
  driver when the controller starts or stops**: it is disconnected on one
  side and found again on the other. A drive in use is disconnected the same
  way, so do not have one busy on a blue connector when the controller
  starts or stops. See "Intel 7-, 8- and 9-series chipsets: the port
  switchover".
- **A device the controller faults on is unplugged and plugged in again by
  the driver** (since `2.2.0.0`; "Controller faults: what the driver does
  about them"). Whatever it was doing ends as on an unplug: a file copy to a
  drive fails part way, and a drive should be checked as after any
  surprise removal. After three on one port, the next fault there removes
  the device and holds the port, and the device stays gone until it is
  unplugged, or, on a root port held unpowered after over-currents, until
  the controller is restarted. `XhciTolerance` `0` turns it off, except for
  a device the controller reports incompatible. A recovery of the
  controller itself re-enumerates every device on it the same way, so a
  copy running across it fails, as it did under `2.1.1.0`.
- **After the driver closes off a controller it could not prove stopped,
  disabling the controller hangs: restart the machine** (since `2.2.0.0`;
  same section). Only when the controller stopped answering altogether, or a
  recovery could not stop it, and the driver could not prove it had stopped
  writing to memory, so kept everything it might still write into (the
  snapshot reads "CONTAINED" and "PINNED"): a later stop of the controller -
  disabling it in Device Manager, or updating or removing its driver - waits
  for ever on the transfers kept, and only restarting the machine ends it.
  Restart the machine instead of disabling the controller. A later release
  is planned to hand back what is kept at the stop, once the driver can
  prove the controller has left the bus or has stopped writing to memory.
  From `3.4.0.0` one such proof exists: for a dock's USB controller on 2000
  and XP behind a USB4 tunnel that this release's connection manager closed
  and read back closed before the proof was asked (task 45.35), its
  transfers are released; a closure read only after the proof was asked
  leaves the pin, and in every other case they are still kept, and a dock
  unplugged with an input device on its USB controller waits for ever
  ("Thunderbolt and USB4: known limitations").
- **Windows 2000: after the driver recovers the controller, or after a
  resume from hibernation, a USB drive whose volume was mounted may come
  back at Code 31** (same section). The mouse and other devices come back;
  the drive stays at Code 31 until it is unplugged and plugged in again, or
  the controller is disabled and enabled in Device Manager. The recovery
  path is the one `2.1.1.0` has, but `2.2.0.0` recovers the controller on
  more faults. A controller that lost its state across the hibernation is
  started afresh, and up to `3.3.0.0` the driver then found every device
  again as on a recovery; from `3.4.0.0` a device on a root port is kept
  where "XhciResumeKeep" allows. A restart of the drive's own device clears
  it.
- **Windows 98 SE and ME: a device name with characters outside plain ASCII
  shows them as `?`.** See "Devices and hubs in Device Manager".
- **Windows 2000 from the F6 floppy needs three files from its own CD.**
  Picking the first entry, "(32-bit Windows 2000/XP)", with a USB keyboard,
  mouse or storage device on the controller stops the new system at its
  first start (`STOP c000026c`, `0xc0000034`, naming `hidusb.SYS` or
  `usbstor.SYS`): text mode copies those class drivers but not the Windows
  files they need, `usbd.sys` among them. The Windows 2000 entry, with
  `USBD.SY_`, `HIDCLASS.SY_` and `HIDPARSE.SY_` copied from the CD to the
  floppy, avoids it; this package does not carry those files. Text mode and
  the Recovery Console work with either entry, XP with a USB keyboard and
  mouse is not affected, and an install with no USB keyboard, mouse or
  storage device on the controller goes on with no stop. See "Installing
  Windows 2000 or XP, or using the Recovery Console: the F6 floppy".
- **Windows XP and XP x64 installed with the F6 floppy: GUI-mode Setup asks
  about the unsigned driver before the USB keyboard works.** Setup copies
  Windows' own HID and USB helper files only with Microsoft's own USB
  controller drivers, so the USB keyboard and mouse wait for GUI-mode
  Setup's device install, which first shows the unsigned-driver prompt. A
  machine whose only keyboard is USB cannot answer it and Setup stops; a
  laptop's built-in keyboard or any PS/2 keyboard answers it. Windows 2000
  shows no such prompt, and the Recovery Console is not affected. See
  "Installing Windows 2000 or XP, or using the Recovery Console: the F6
  floppy".
- **After an update from `2.0.0.0`, some devices are found once more as new
  hardware**, one time each. See "Updating from 2.y.y.y".
- **After an update, the root hub's Driver tab still shows the old version**
  until the root hub is updated too, although it runs the new file. On
  Windows 98 SE and ME the old driver also keeps running until the next
  restart, which they do not ask for. Same section.
- **Windows Vista and 7: deleting the driver keeps the older packages.**
  "Delete the driver software for this device" removes only the package in
  use, and Windows may install an older one the next time it finds the
  controller; `pnputil -d` removes them. Same section.
- **32-bit Windows Vista and 7: the install asks for a restart**, and the
  controller does not start until it is done, because the helper
  `xhcihlpr.sys` (`xhcimsi.sys` in `2.3.x`) starts only at boot ("Windows
  Vista and 7: message-signalled interrupts"). A reinstall needs the restart
  even when Windows does not ask for it: after a remove and a scan, 32-bit
  Vista showed only an "installed successfully" balloon, the controller at
  Code 1 until a restart.
- **32-bit Windows 2000 and XP: the finer timer waits for the next boot**
  (since `2.4.0.0`), when `XhciPollFineTimer` is set to `1` (it is off
  by default). The install brings the helper `xhcihlpr.sys` with no
  restart asked for, and a polled controller keeps the clock tick until
  the helper loads at the next start of Windows ("XhciPollFineTimer: a
  finer system timer while polling"). Text-mode Setup (F6) copies the
  helper's file from the floppy and does not load it.
- **Intel 7-series (Panther Point, `8086:1E31`): a periodic schedule larger
  than the controller can serve may lose data instead of being refused.**
  Linux keeps its own bandwidth accounting on this controller
  (`XHCI_SW_BW_CHECKING`), because the controller accepts periodic schedules
  - audio, mice, keyboards, hubs' status - that it cannot then serve. This
  driver relies on the controller's own admission there, as on every
  controller, so too many periodic devices at once may lose data rather than
  be refused. It was not taken in `2.4.0.0` ("Linux's controller quirks"); a
  later release may take it if a report needs it.
- **Thunderbolt and USB4** (new in `3.0.0.0`): the BIOS settings the
  controller needs, safe removal on each system, standby with a device
  attached, and the rest are in "Thunderbolt and USB4: known limitations",
  under "What `3.0.0.0` adds".

`2.0.0.0`'s list also had "a device moved to a different port is found
again as new hardware", which `2.1.0.0` answers for every device with a
usable serial number, and Windows ME's controller re-enable hang, which
`2.1.0.0` fixes ("Targets and their standing").

The `1.2.0.0` limitations, and what each is under `2.0.0.0`, are in the
next section.

## The 1.2.0.0 known limitations, under 2.0.0.0

`1.2.0.0`'s own notes (linked at the top) list its limitations. Each was
re-measured, or answered by the design, under `2.0.0.0`. No change since
`2.0.0.0` answers a row differently, except the standby row, which says
what `2.3.0.0` adds:

| `1.2.0.0` limitation | Under `2.0.0.0` |
|---|---|
| Windows 98 under NUSB: stopping the controller (disable, uninstall, upgrade) crashed the machine, in NUSB's `usbport.sys` | `2.0.0.0` does not use `usbport.sys`. Disable, enable, remove and rescan of the `2.0.0.0` controller completed under NUSB 3.3 and under SweetLow's stack in Windows 98 SE virtual machines. |
| Windows 98: fast, repeated plug and unplug could freeze the machine | Gone in every reading: the hub churn that froze Windows 98 under `1.2.0.0` at 12 to 18 hubs ran 120 of 120 with the guest responsive, and the device matrix's hub churn passed on Windows 98 SE and 2000 on the final build |
| Windows 7 (32-bit, on a ThinkPad E460): disabling the controller hung | Gone under `2.0.0.0`. It belonged to the miniport under Microsoft's `usbport.sys`; under `2.0.0.0` it did not occur in five disable and enable cycles each on Windows 7 x86 and x64 |
| Windows 2000: a USB audio device unplugged during playback was never fully removed | Gone: the removal arrived within about a second, 7 times out of 7 |
| The controller never went to sleep | The driver idles nothing it is not asked to; see the selective-suspend entry in "Known limitations" |
| Every root-port device reported to Windows as High Speed, and its consequences: root-port polling in 1, 2 and 4 ms bands, a Full-Speed audio device on a root port silent from Windows XP on, a USB 1.1 hub on a root port crashing Vista and 7, and the Advanced tab's bandwidth figures | The cause is gone: there is no `usbport.sys` to report to, and every device is given its true speed, read on every device row of the virtual-machine matrix on both primary targets. A Low-Speed mouse is polled every 8 ms at a root port and behind a hub, on real hardware under Windows 98 SE. A Full-Speed hub on Vista and 7 enumerated with devices behind it, and a Full-Speed audio device on a root port bound on XP, Vista and 7 and played in real time on XP x64, in virtual machines |
| Windows 98 SE: USB audio could stutter while a drive was read at full speed | Not re-measured under `2.0.0.0` |
| Windows 2000: a newer package over an older one was refused; disabling the controller with an audio device attached asked for a restart | The upgrade from `1.2.0.0` installs in place on Windows 2000. The disable with an audio device attached was not re-measured |
| Windows 98: a driver that failed while starting the controller stopped the machine with a protection error | Not re-measured: no failing start was provoked under `2.0.0.0` |
| On a controller without Force Save Context, a wake from standby rebuilt the bus | Under `2.0.0.0`: untested, the power handlers had not run. Since `2.3.0.0` (read in QEMU, which has no Force Save Context): hibernate and resume on XP, Vista and 7, and standby and hibernate on ME, idle, rebuild the bus through the controller's reinitialization and, up to `3.3.0.0`, every device is found again (from `3.4.0.0` a device on a root port is kept, "XhciResumeKeep"); with a copy running on a stick on XP, Vista and 7, the copy's requests fail for about 10 s while the stick is found again, then work. Open: Windows ME hangs on a standby or hibernate with a copy running on an xHCI stick. Windows 98 SE's standby resumed with a USB mouse at a root port or behind a hub, and with a USB disk plugged in after Windows started, and stopped with a USB disk attached when the computer started (the BIOS presents it, and Windows uses it in MS-DOS compatibility mode), as it does under NUSB's EHCI driver; QEMU's Windows 98 SE does not resume reliably with no USB controller either (the 98 SE Stand by entry in "Known limitations"). Standby on the other systems, and with devices suspended, is not yet read |
| The virtual High-Speed hub switch and its costs. It also did nothing unless `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid` were set beside `XhciVirtualHSHub`, since `1.2.0.0` has no default for either (reported on GitHub issue 4, 2026-10-03) | No switch: all three values have no effect (see "Registry settings") |

## Licensing

This driver's own source is under the GNU General Public License, version 2
or later (`GPL-2.0-or-later`); see `LICENSE`. The full third-party material and provenance
record is `docs/contributing/legal-provenance.md`.

`xhci98.sys`, `xhciuas.sys`, `xhcicm.sys` and their INFs are this
project's own work. A download that bundles Stor98 carries it built from
this project's source tree but under its own licence (BSD-3-Clause), with
its licence and notice beside it ("NVMe drives behind Thunderbolt or
USB4"). The `usbd.sys` and `usbui.dll` the install needs are not in the
download: they are Windows' own and are copied by Windows from your own
installation source. No download up to `3.1.0.0` carries a Microsoft
file. From `3.2.0.0` the base and `-free` downloads' only Microsoft files
are NUSB 3.3's seven Windows 98 SE storage files ("Windows 98 SE and
ME: the stock install, and storage"; the `-nonfree` download adds
more, below), unmodified, in a folder of their own with a
`NOTICE.md` saying whose they are; they are not this project's work and are
under no licence of this project's, and no licence to redistribute them has
been established. A download built without them carries no Microsoft file,
and its `readme.txt` says so. On Windows 98 SE, `xhciuas.inf` names
`USBNTMAP.SYS` as a filter on the UAS device.

From `3.2.0.0` a release has up to three downloads (roadmap-usb4 task
43.48, design record 26 section 6.7): `xhci98-<version>.zip`;
`xhci98-<version>-free.zip`, which adds the usb-wdm-free USB class
drivers in `USBDRV\FREE\` (experimental: they may not work with every
device and can make Windows unstable; their problems go to that
project) and the CamCheck webcam preview tool in
`USBDRV\FREE\tools\camcheck\`; and `xhci98-<version>-nonfree.zip`, which
adds other manufacturers' drivers from usb-wdm-nonfree in `USBDRV\NONFREE\`, not
this project's work (NUSB 3.3's storage files are in
`USBDRV\NUSB33\` of every download that carries them). From `3.3.0.0`
download 3 has the makers' Ethernet, HID and serial drivers. Wi-Fi
drivers are not in this download. Get them from the
[usb-wdm-nonfree](https://github.com/yeokm1/usb-wdm-nonfree) release.
`USBDRV\NONFREE\` holds more Microsoft files, the KMDF co-installers
ASIX and Davicom ship with their drivers (`WdfCoInstaller01007.dll` and
`WdfCoInstaller01009.dll`, in several folders),
unmodified, with no licence to redistribute them established either.
From `3.4.1.0` download 3 carries usb-wdm-nonfree `v1.2.0`, which adds
an audio driver: TASCAM's US-122 driver 3.24 for 98 SE and ME
(`USBDRV\NONFREE\audio\us122\w98se-me\`), which TASCAM no longer
offers. It carries no licence of its own, and no licence to
redistribute it has been established (`legal-provenance.md` section 5).
From `3.4.2.0` download 3 carries usb-wdm-nonfree `v1.3.0`, which adds
Microsoft's USB smart card reader driver for 2000 and XP
(`USBDRV\NONFREE\smartcard\usbccid\`), unmodified from the Microsoft
Update Catalog. Its cabinets carry no licence, and no licence to
redistribute it has been established either.
The installer offers each set the download carries on a page of its
own after Components, unticked: "Free USB drivers" and "Non-free USB
drivers" from `3.3.0.0` (one page, "Other USB drivers", before;
roadmap-usb4 task 44.10). Ticked, the set is copied to the install folder
and nothing is installed, and a box per driver package, grouped by kind
and unticked, lets Windows 2000 to 10 use that package's drivers at a
device's first plug; on 98 SE and ME the sets were copied only, but for
the free `uac2` (98 SE and ME) and `usbmidi` (98 SE), in `3.3.0.0`, and
from `3.4.0.0` every package a set offers for staging there can be
ticked while `DevicePath` stays within the system's limit ("Known
limitations"). A
set the download does not carry has no page; the Finish page names the
download and the address that have it. The uninstall removes what it
placed.
From `3.3.0.0` the `-free` download carries usb-wdm-free `v0.1.4`
(`v0.1.1` re-released under GPL-2.0-or-later as `v0.1.2`, with
`rtl815x`'s chip core rewritten; then `v0.1.3`, whose class drivers match
their USB class on the systems where Windows has no class driver of its
own: `uac2` USB Audio Class 2.0 devices, `usbmidi` MIDI streaming on
98 SE and 2000, `cdcnet` CDC ECM, NCM and RNDIS, `usbcom` CDC ACM serial
(new on 98 SE, ME and 2000), `usbflop` UFI floppy drives and `uvc98` USB
Video Class cameras; its packages and the systems they are staged or
copied on as in `v0.1.1`; then `v0.1.4`, whose `rtl815x` adds the Realtek
RTL8153D, RTL8156, RTL8156B, RTL8157 and RTL8159 on 98 SE, ME and 2000,
and on XP to 7, 32-bit and x64, the ones Realtek's driver in the
non-free set does not serve there, and whose `uac2` adds playback volume
and mute). `v0.1.1` added `ax88179`, its driver for USB Gigabit Ethernet adapters with an
AX88179 or AX88178A chip (`USBDRV\FREE\net\ax88179\`): `w98se-me\` for
98 SE and ME, `w2k\` for 2000; copied only on 98 SE and ME up to
`3.3.0.0` (from `3.4.0.0` it can be ticked there), and never
staged on 2000 (the pack lists that INF as copy
only: no 2000 leg). Its open point is usb-wdm-free's: in the first
session after the adapter was plugged in, the chip was once seen passing
each frame with its checksum, which the driver strips (usb-wdm-free's
release notes, "Release 0.1.1"). `rtl815x` stays copy only, its legs
with a simulated adapter only (usb-wdm-free's release notes, "Release
0.1.2" and "Release 0.1.4").

## Trademarks

Xbox is a trademark of Microsoft Corporation. It names the controllers the
free driver set's `xpad98` game pad driver is for; neither this project
nor that driver is Microsoft's, and no Microsoft name or logo is part of
any product name here.
