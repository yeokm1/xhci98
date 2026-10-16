# Run 41 - XHCITR, the USB and Thunderbolt Tree

Phase 41 of `roadmap-usb4.md`, design record 25. Readings of 2026-10-16 on
development host A, in QEMU guests, with the emulator binaries pinned for
the pre-cut legs (`vm\usb4\bin-5ba7f60`): `usb4-generic` (`intel-adl`) with
the NVMe enclosure, and `tb3-alpine-ridge` (the ICM), each beside a
`qemu-xhci` carrying plain USB devices (a tablet, a storage stick, a hub
with a second device behind it). Every guest was a copy-on-write overlay;
every tool run below that says "standard user" ran under an account in the
Users group only.

## The build read

- Branch `p41-xhcitr` at `2a19ac33`: `build-driver.cmd all` and
  `all -amd64` passed every gate; the tools built; the dry cut (x86, TEST
  label, nvme98 `0.1.1.0`) passed, with `tools\xhcitr\XHCITR.EXE` and
  `XHCITR64.EXE` in the package.
- The x86 drivers ran unsigned on 98 SE to 7 x86 and XP x64 (release
  flavour). On Vista x64 and 7 x64 only the tool was new: the drivers
  were the test-signed package of 2026-10-16 01:18, whose `xhcicm.sys`
  has no query device, so those legs read the tool's fallback.
- The tool's later fixes from these legs (sections below) are in
  `dedef2ce` and after; they change only `xhcitr\xhcitr.c`.

## The query device before the tool (Windows 2000)

On `usb4-generic` with the NVMe enclosure, the qemu flavour of the query
device's first complete version: `\\.\XHCICM0` opened with no access
rights, by a standard user too; the IOCTL answered 411 bytes (pass 168,
age 30 ms, live ACTIVE, one router, the extension 20 Gbit/s per lane x2,
link state CL0, read from the lane adapter). A 32-byte buffer failed with
error 122; 64 bytes returned the header with the size needed; another
IOCTL failed with error 1; `\\.\XHCICM0\x` failed with error 87. Disabling
the NHI with a handle open turned every later query into error 1617
(device removed), with no crash or hang; enabling it again brought
`XHCICM0` back with a new epoch.

## Legs

| Leg | 98 SE | ME | 2000 | XP | XP x64 | Vista x86 | 7 x86 | Vista x64 | 7 x64 |
|---|---|---|---|---|---|---|---|---|---|
| T.1 window, USB tree, descriptors, `/report`, standard user | pass | pass (no USB device) | pass | pass | pass | pass | pass | pass | pass |
| T.2 Thunderbolt branch | pass (ICM), after the fix | pass (ICM), after the fix | pass (USB4) | pass (ICM) | - | pass (ICM) | pass (USB4) | fallback pass | fallback pass |
| T.3 Safely Remove, window and tray | pass, after the fix | pass, after the fix | pass | pass | - | - | pass | eject refused, as designed | eject refused, as designed |
| T.4 balloons | tooltip not read | pass, after the fix | pass | pass | - | - | pass | - | - |
| T.5 install, login start, uninstall | pass | - | install, login start | install, login start | install | install | pass (in-use upgrade not read) | - | - |
| T.6 `INSTALL /devices`, `/eject` | pass | - | pass | pass | - | - | pass | - | - |

### Windows 2000 and XP

- Installed with `INSTALL /defaults /yes /nopause` as an administrator
  (the first run ended "steps left" while Windows' own new-hardware
  wizards were open, and said so; the second ended "restart"). The tray
  started at the standard user's login on both.
- T.1: no prompt; HCD0 (2000), HCD0 and HCD1 (XP); the tablet and stick
  at High Speed, the hub and the stick behind it at Full Speed; the
  Thunderbolt branch read "Query device: XHCICM0"; descriptors decoded
  with their bytes; `/report` exit 0 as the standard user.
- T.2, 2000 (USB4): the host router active, liveness answering, the last
  pass 80 ms old; the enclosure USB4 v2.0, approved, "USB4 Gen 3 x2 (40
  Gbit/s) (read from the lane adapter)", PCIe and USB3 up, the NVMe unit
  under the PCIe tunnel. XP (ICM): security level 1, firmware 0x60, "20
  Gbit/s per lane, x2 (reported by the firmware)", PCIe up, USB3 none.
- T.3 as the standard user, from the window and from the tray: the
  confirmation listed the NVMe controller and its disk. 2000: "Windows
  has stopped ... The PCIe tunnel was closed.", the unit left, the router
  read "PCIe tunnel: closed after an eject". XP: "... You can now unplug
  the device.", the devnode Code 47, the unplug with no dialog. From the
  tray the outcome came as a balloon. A re-plug brought the NVMe back
  started on both.
- T.4: balloons on plug, disconnection and the eject's outcome on both;
  the approval balloon was not read (the ICM approved at once).
- T.6: `INSTALL /devices` and `INSTALL /eject 1` passed on both with the
  new `xhcicm.sys`.

### XP x64, Vista x86 and 7 x86

- Installed as an administrator (XP x64's first run ended "steps left"
  with Windows' hub wizard open, the known XP x64 behaviour; the second
  ended "restart"); the tool and the Run value in place on all three.
- T.1 on all three as the standard user, medium integrity: no UAC prompt;
  the USB tree with speeds, Windows' own UHCI labelled "(Windows)";
  descriptors decoded; `/report` exit 0 (`XHCITR64` on XP x64).
- T.2: 7 (USB4) as on 2000, "Windows lists it in Safely Remove Hardware
  too"; Vista (ICM) as on XP.
- T.3, T.4 and T.6 on 7: as on 2000 (the confirmation listing two
  devices, "The PCIe tunnel was closed.", the unit gone, balloons on plug,
  disconnection and the outcome, `INSTALL /eject 1` exit 0).
- T.5 on 7: the Run value `"C:\Windows\XHCI98\XHCITR.EXE" /tray`; the
  tray started for the administrator and the standard user; INSTALL again
  with the same package left the running tool alone ("is current", exit
  0); uninstall with the tray running closed it, removed the file, the
  Run value and `%windir%\XHCI98`. The upgrade of a tool in use was not
  read (no older build to upgrade from).

### Vista x64 and 7 x64 (the tool on the earlier signed drivers)

- T.1 as the standard user: no UAC prompt; the USB tree with speeds,
  every descriptor decoded; Properties opened Device Manager's dialog; a
  second start handed over; the tray's Exit ended it.
- The Thunderbolt branch with no query device: "(not confirmed: eject
  unavailable)" on the controller, "Source: the registry record (shown
  only)"; the enclosure, its tunnels and the NVMe unit shown; Safely
  Remove greyed; `/eject id=` refused with exit 1 and nothing stopped; no
  crash over plug, unplug and two fast cycles; no balloons (they come
  from the query only, record 25 section 7.2).

### Windows 98 SE and ME

**In the first build the query device did not answer the tool on either system.** Every
reading shows the controller's state from the registry record, "not
confirmed: eject unavailable (its query device did not answer for it)",
although ME's NHI hardware key names `XHCICM0`; it held after a restart
on 98 SE. So the tool's Safely Remove and its balloons, which come from
the query device, are not available on 98 SE and ME in this build, and
record 25 section 13's open question is answered "not as built". XHCIINST
on the same machine read a confirmed record and stopped the unit.

- T.5 on 98 SE: `INSTALL /defaults /yes /nopause` copied the tool to
  `C:\WINDOWS\XHCI98\XHCITR.EXE` and wrote the Run value; the tray was
  there at the next login; `/uninstall` closed the window and the tray
  and removed the Run value and `C:\WINDOWS\XHCI98`; a re-install brought
  the tray back at the next login. (98 SE's Device Manager steps were
  needed as `INSTALL` prints them.)
- T.1: on 98 SE the USB branch shows both controllers, their root hubs and
  a tablet at High Speed, with its descriptors decoded (the guest's older
  `xhci98.sys` had to be updated first; with it the branch was empty); on
  ME the Thunderbolt controller's xHCI with its root hub, no USB device.
- T.2: on 98 SE (ICM) and ME (ICM and USB4) the tree shows the NHI, the
  port and bridge, the enclosure approved, the PCIe tunnel up and the NVMe
  unit, from the registry record.
- T.3 on 98 SE: Safely Remove greyed, the tray's menu "No Thunderbolt or
  USB4 device to stop" (the build before the fallback fix), as follows
  from the missing query answer.
- T.6 on 98 SE: `INSTALL /devices` "information from the driver: confirmed
  now"; `INSTALL /eject 1` "Windows has stopped NVM Express Controller
  (nvme98). ... You can now unplug the device.", exit 0.
- On ME `INSTALL /yes` did not see the NHI on the first run and answered
  No to the Thunderbolt question (as `v-9x` read before); `/thunderbolt`
  installed it.
**The cause, and the fix.** A probe in the 98 SE guest read the query
device working: `\\.\XHCICM0` opened with no access rights, the IOCTL
answered 352 bytes (pass 731, age 41 ms, live ACTIVE, the record and one
extension, 20 Gbit/s per lane x2), a 32-byte buffer failed with error 122
and 64 bytes returned the header. But the answer's instance ID was empty
and its bus, device and function unknown: on Windows 98 the driver gets
neither. The tool rejected every answer whose ID did not equal the NHI's
(`PCI\VEN_8086&DEV_15BF&SUBSYS_22B017AA&REV_01\0000083000`). The tool now
accepts an answer with an empty ID when the record's NHI vendor and
device equal the devnode's and no other NHI carries the same
`XhciCmQueryName`; an answer with an ID still has to match it (`85f3fb90`).

**With the fixed tool** (98 SE and ME, the Thunderbolt 3 controller, with
no disk in the BIOS's view at power-on, which avoids an emulator hang at
the 98 logo): the controller's details read "Source: the driver's query
device", "Query device: XHCICM0 (matched by name ...)", the last pass and
its age, "Liveness: answering", the start epoch, and the router "20
Gbit/s per lane, x2 (reported by the firmware)". Safely Remove from a
right click on the unit, from the Action menu, from the tray's menu and
with `XHCITR /eject id=` showed the confirmation and "Windows has stopped
NVM Express Controller (nvme98). ... You can now unplug the device."; the
unit left the tree; unplug and re-plug brought it back. On ME the
balloons read "... was disconnected." and "... connected (Thunderbolt
3)." A plain USB device's Safely Remove is greyed. Not read: the 98 SE
tooltip text, `/eject`'s exit code there, USB4 on 98 SE.

## Defects found and fixed in these legs

- The tree pane collapsed to 60 pixels after a minimise and restore, and
  that width was saved (every system): the layout now skips a minimised
  window and only a drag changes the stored width.
- With no query device the tray said there was no device to stop: the
  unit is now listed greyed with its reason, and `/eject` refuses with it.
- A double click on the icon also left the left-click menu open.
- Under the ICM a unit already stopped (Code 47 until the unplug) was
  still offered for removal: it is now shown stopped, "unplug it".
- Messages from XHCIINST's texts kept console line breaks in balloons and
  message boxes; counts said "1 ... devices"; a router's details had two
  "Link:" lines; string descriptors were not shown in hex.

### The final tool, read again

With the tool of `63086eb1` (every fix above), as the standard user:

- XP with the ICM: after Safely Remove ("Windows has stopped ... You can
  now unplug the device.") the unit reads "[stopped - unplug it]", its
  details "Stopped: you can unplug it.", Action and Delete are disabled,
  the tray item is greyed "(stopped - unplug it)", and `/eject id=`
  refuses with "cannot be stopped here: stopped - unplug it." (exit 1);
  on a running unit `/eject id=` stops it (exit 0). Unplug: "was
  disconnected." balloon, no dialog; re-plug: "[can be stopped]" again.
  The status bar reads "4 USB devices, 1 Thunderbolt/USB4 device"; the
  router's rows read "Position: link 1, depth 1" and "Link: 20 Gbit/s per
  lane, x2 (reported by the firmware)".
- 2000 with USB4: Safely Remove from the window and from the tray; the
  answer "Windows has stopped NVM Express Controller (nvme98). The PCIe
  tunnel was closed." and its second paragraph read with no break inside
  a sentence, in the message box and the balloon; the router "PCIe
  tunnel: closed after an eject"; unplug and re-plug as before.

### A unit named by its drive, and SuperSpeed devices

The owner asked that Safely Remove name the drive rather than the
controller. The unit stopped is still the controller behind the tunnel
port; the tree, the tray item and the answer now name its disks and their
drive letters, and the confirmation says what stops ("This stops NVM
Express Controller (nvme98) and everything on it:" then each disk and
volume). With that build, as the standard user:

- 98 SE (ICM): "QEMU NVM e Ctrl (E:)" in the tree, the tray and the
  answer, the letter from the disk's `CurrentDriveLetterAssignment`; the
  eject, unplug and re-plug as before. The model string carried trailing
  blanks; names are now trimmed.
- XP (ICM): "... (F:)"; 7 x86 (USB4): "... (G:)", "The PCIe tunnel was
  closed."; the letters found with no access rights. The tool's window
  was up in 3 to 4 s with the CD drive empty or holding a disc.
- The disk's name as Windows gives it reads "NVM e" (with a space) on every
  system: it comes from the NVMe driver's inquiry data, not from the tool.

SuperSpeed devices on `xhci98.sys` (QEMU's xHCI with USB 3 ports):

- 98 SE: a storage stick added after boot reads "SuperSpeed (5 Gbit/s)",
  "Port supports: USB 3.x; device SuperSpeed capable", bcdUSB 3.00, the
  bulk endpoints at 1024 bytes with their SuperSpeed companions, and the
  BOS (USB 2.0 extension, SuperSpeed capability: full, high, 5 Gbit/s).
- XP: the stick as on 98 SE; a UAS drive on `xhciuas.sys` 3.0.0.0 with
  four pipes, SuperSpeed; the root ports split into four USB 3 and four
  USB 2 ports, the USB 2 devices at High and Full Speed.
- Once, QEMU itself stopped answering while booting XP with a UAS disk on
  a USB 3 port (its monitor connections hung before any guest driver had
  loaded); the same command line booted normally on the next start.

### Vista x64 and 7 x64 with the new drivers

The owner test-signed the x64 package of the final drivers (the query
device included) on 2026-10-16 at 07:33. With it, and the tool of the dry
cut, as the standard user:

- Install: a fresh install ended "restart" and put the tool and the Run
  value in place; over the earlier test build of the same version the
  first run asked for `INSTALL /restage` (the store held a different build
  under an identical INF), which then replaced every package.
- 39-V.3 and 40-V.3 on the new drivers: the NVMe at plug with no Refresh;
  Windows' Safely Remove to Code 47 and the unplug with no dialog;
  `INSTALL /devices` "confirmed now"; `INSTALL /eject 1` exit 0 (on Vista
  once refused, exit 4, while an Explorer window held the volume, as
  Windows reports it).
- T.1 to T.6: no UAC prompt; the tray at login; the Thunderbolt branch
  from the query device ("Query device: XHCICM0", the last pass under
  160 ms old, liveness answering), USB4 "Gen 3 x2 (40 Gbit/s) (read from
  the lane adapter)", TB3 "20 Gbit/s per lane, x2 (reported by the
  firmware)"; Safely Remove from the Action menu, Delete, a right click
  and the tray, with no elevation; balloons on connect, disconnect and the
  eject's outcome; uninstall with trays running in two sessions closed
  both and removed the tool, its folder and the Run value. The approval
  balloon was not read (the emulator's device is approved at once).

### The emulator moved to `2a893ea`

`usb4-emu` landed its TCG SMM boot-logo fix and the TS3 Plus dock's Host
Interface adapter (`main` `b8b8818`, QEMU fork `2a893ea`), staged in
`vm\usb4\bin-2a893ea` with its SHA-256 checked before use. On 98 SE with
the `3.0.0.0` drivers and the tool, every window shown, as the standard
user:

- `tb3-alpine-ridge` (the ICM): the enclosure and the unit "QEMU NVM e
  Ctrl (E:)"; Safely Remove from a right click, the unplug, then a hot
  plug brought the enclosure, the PCIe tunnel and E: back. Pass.
- The boot logo with a disk the BIOS sees at power-on (`Bios OFF_ON`, the
  NVMe tunnelled before POST): past the logo to the desktop. Pass.
- The TS3 Plus dock (`dialect=tb3`) behind `usb4-generic` on i440FX: the
  connection manager read the dock's ROUTER_CS_0 to 6 once and went no
  further; the tool showed the port held with no tunnel. ROUTER_CS_4's
  USB4 Version read `02h`, which the decode took as reserved (only `00h`
  was a Thunderbolt 3 router). The `5ba7f60` build reads the same, so the
  move is not the cause. The decode is corrected on its own (EMU.1).

The legs after these use `vm\usb4\bin-2a893ea`.

### A Thunderbolt 3 dock behind a USB4 host (task 40-A.8)

The first guest runs of record 22 section 8: the emulator's TS3 Plus
model (`usb4-device ...,dialect=tb3`, from the Framework 13 capture)
behind `usb4-host profile=usb4-generic` on i440FX, on the emulator
binaries `vm\usb4\bin-2a893ea` (QEMU fork `2a893ea`), the device line of
the 98 SE leg that found the defect. Branch `tb3ver`; 98 SE ran the qemu
flavour of `xhcicm.sys`, installed through the hardware wizard, and 7
x86 the release package through `INSTALL /defaults /yes /nopause
/thunderbolt`; the NVMe disk behind the dock ran `nvme98`.

**What stopped it, three things in turn.**

- The connection manager read the dock's ROUTER_CS_0 to 6 once and
  refused it (`u4.router.version`): ROUTER_CS_4 reads `0x0200000A`, USB4
  Version `02h`, major 0 and minor 2, and the version was compared as a
  whole byte (`00h` only for Thunderbolt 3). It is now decided by the
  major alone, as Linux decides it. The 3.0.0.0 build of the emulator
  pinned for the cut (`bin-5ba7f60`) reads the same.
- With that fixed, the port went FAILED for enumeration at once
  (`u4.port.state=00010107`): the dock reads ROUTER_CS_1 `0x0602C005`
  before enumeration, Upstream Adapter 0. A Thunderbolt 3 router's
  upstream adapter is the connection manager's to set; it now comes from
  the Adapter Num of the read's response (adapter 1 here), and the
  enumeration write carries it.
- With that fixed, the dock came up with its PCIe tunnel and the disk,
  but unnamed: its DROM sits behind the Plug Events capability's EEPROM
  bit-bang, which the first pass of 40-A.8 left out. It is now read.

**98 SE** (the gold image's overlay, the dock hot-plugged after the
driver update and then at power-on):

- The dock up (`u4.router.up=808615D3`, `u4.router.kind=01000502`: version
  `02h`, Thunderbolt 3, bonded), the PCIe tunnel up
  (`u4.pcie.up=00010809`), the re-enumeration; the PCI Card wizard at once,
  `nvme98` from the transfer drive, E: ("ORICO") with its test file.
- XHCITR: "QEMU USB4 NVMe Enclosure (Thunderbolt 3), approved",
  Generation "Thunderbolt 3", IDs 8086:15D3, "Tunnels: PCIe up, USB3 none,
  DisplayPort none", the NVMe unit "[can be stopped]" beneath it, the
  source "the driver's query device".
- Safely Remove from XHCITR's Action menu and with Delete: "Windows has
  stopped QEMU NVM e Ctrl (E:). ... You can now unplug the device."; the
  unplug left the port "active, no device"; the re-plug brought the
  router, the tunnel and E: back. Device Manager listed "NVM Express
  Controller (nvme98)" under SCSI controllers and the disk.
- At power-on with the dock attached: the DROM read, the router and the
  tunnel up before the desktop, E: open at the desktop, XHCITR as above.

**7 x86** (a fresh overlay; the install, then the dock hot-plugged, then
at power-on):

- INSTALL ended "restart" with "xHCI98 USB4 Host Router: xhcicm 3.0.0.0,
  started" and the tunnel port's filter. After the restart the hot-plugged
  dock's PCIe tunnel came up; at power-on with the dock attached too.
  `nvme98` installed on the NVMe controller; F: ("NVME32") readable.
- XHCITR `/report`: the host router "xhcicm", the dock "QEMU USB4 NVMe
  Enclosure", Thunderbolt 3, "PCIe up, USB3 none, DisplayPort none", the
  PCIe tunnel's unit "QEMU NVM e Ctrl SCSI Disk Device (F:)", every
  devnode Problem code 0.
- `INSTALL /devices` listed the dock "Thunderbolt 3, port 1, depth 1,
  approved", "two lanes bonded"; `INSTALL /eject 1`: "Windows has stopped
  NVM Express Controller (nvme98). The PCIe tunnel was closed. You can
  now unplug the device." (exit 0); unplug and re-plug brought F: back
  with its files.

**Also found.** XHCITR and XHCIINST decoded the status record's version
byte as two nibbles, so a USB4 1.0 router (`20h`) read "USB4 v2.0": that
is the "USB4 v2.0" of the 2000 reading above, the emulator's enclosure
being USB4 1.0. They now take the major and minor as the specification
splits them. The 2000 and XP legs of the dock were not run.

### Stor98 behind the Thunderbolt 3 controller (STOR.2)

Readings of 2026-10-16 on development host A, `vm\usb4\bin-2a893ea` (its
`qemu-system-x86_64.exe` SHA-256 `6a1dfcc8...bd66` checked before use),
`tb3-alpine-ridge` with the NVMe enclosure hot-plugged on port 1 after the
desktop was up (`device_add` / `device_del` of `enc0`), every guest a new
overlay under `E:\xhci98-vm\stor1-legs` with its SDL window shown. The
package is a dry x86 TEST cut ("Stor98 legs") of branch `stor1-legs` at
`da55d9e2` (`3.0.0.0` with STOR.1 merged), bundling Stor98 `0.3.0.1`
(`SHA256SUMS.txt` SHA-256 `aee07c9c...cb9`, interface version 4); its
`xhci98-3.0.0.0.zip` was unpacked onto each guest's transfer drive. The 98
SE and ME legs were first read on a cut bundling Stor98 `0.3.0.0` and then
read again on this one; where the two differ it is said below. `3.0.0.0`
will bundle Stor98 `0.4.0.0` (owner, 2026-10-16), so these legs are owed
again on that build. Before any
reading every NVMe controller devnode was checked to be Stor98's: Device
Manager listed one "Stor98 Storage Controller for NVMe SSDs" and no nvme98
entry, `IOSUBSYS` held `STOR98.MPD` alone (98 SE, ME) and
`system32\drivers` `stor98.sys` and no `nvme98.sys` (2000). The disk is
"QEMU NVMe Ctrl", Stor98's new split of the model number.

| Leg | 98 SE | ME | 2000 |
|---|---|---|---|
| S.1 install: `INSTALL`, then Stor98 by the wizard from `release-x86\` | pass | pass | pass |
| S.2 the drive usable: a file written and compared | pass | pass | pass |
| S.3 XHCITR Safely Remove, unplug, re-plug, compare | pass | pass (fresh overlay; see below) | pass |
| S.4 surprise unplug with the volume mounted, re-plug, compare | pass | pass (written 15 s before) | pass |
| S.5 the link-state channel | not offered, as designed | not offered, as designed | published; Stor98 `0.3.0.1` does not read it |

**Install.** On the 98 SE base (`t26\win98-gold.qcow2`, an older xhci98
installed) `INSTALL /defaults /yes /nopause` staged the drivers and ended
"steps left" (exit 4) as on every 9x install: the Thunderbolt 3 controller
("PCI System Peripheral") and the controller still on the older driver were
updated from Device Manager as `INSTALL` printed it, with the staged folder
for the "Copy files from" prompts; the Windows 98 CD's `usbd.sys`,
`usbui.dll` and HID files were skipped, being on the image already. On ME
(`t37me\winme-stock-base.qcow2`) `INSTALL ... /thunderbolt` likewise left
the QEMU xHCI, the Thunderbolt 3 controller and its xHCI to Device Manager,
then a restart. On 2000 (`win2k-xonly.img`) `INSTALL` ended "restart, then
run INSTALL.BAT again" (exit 2), the second run "Please restart Windows
now" (exit 3) with every device started. The enclosure's hot plug then
showed "PCI Card" (98 SE, ME) or "PCI Device" (2000); the wizard pointed at
`release-x86\` found `stor98.inf` ("Stor98 Storage Controller for NVMe
SSDs"); 2000 asked once about the missing digital signature. The volume
NVME32 mounted with no restart: E: on 98 SE and ME, D: on 2000. XHCITR
showed the unit with driver `stor98.mpd` or `stor98`, version `0.3.0.1`,
started, problem 0. On 98 SE and ME the guests that had read `0.3.0.0` were
moved to `0.3.0.1` by Update Driver, "Display a list", Have Disk
(`IOSUBSYS\STOR98.MPD` 42,573 bytes became 43,533).

**I/O and removal.** On each system the package's `readme.txt` (162 KB)
was copied to the NVMe volume and compared with `fc /b`: no differences.
XHCITR's Safely Remove from a right click on the unit: "Windows has stopped
QEMU NVMe Ctrl (E:) ... You can now unplug the device." (98 SE, ME) or
"Windows has stopped Stor98 Storage Controller for NVMe SSDs" (2000), shown
within the first screenshot after OK (about 4 to 5 s, the capture's own
latency included); the unplug ("QEMU USB4 NVMe Enclosure was
disconnected.", ME and 2000), a re-plug, and the file compared again with
no differences. The surprise unplug with the volume mounted: 2000 showed
its "Unsafe Removal of Device" dialog naming the Stor98 controller and
"QEMU NVMe Ctrl SCSI Disk Device"; after the re-plug the file compared
clean on every system.

**ME write-through.** A 298 KB file copied to E:, 15 s of idle, then a
surprise unplug with no eject: ME showed its "Disk Write Error ... drive
E:" at once, on both Stor98 builds. After the re-plug the file was there at
its full size and compared with no differences. This is the case Stor98's
readme describes: data ME's own write-behind cache still held never
reached the driver; what Stor98 had acknowledged was on the disk. With the
drive still attached, a restart of the fresh ME guest ran ScanDisk on
NVME32 (the unplug had left it dirty) and reached the desktop with E: and
its files.

**The channel on 9x.** `3.0.0.0` publishes no Stor98 channel on 98 SE and
ME (design record 21 section 7.2): no `Services\stor98` key was written
(`regedit /e` of it on ME exported nothing), no UP was published and no
removal waited for an acknowledgement. Stor98 runs there on its own gone
deadline, with no clean-eject handshake, and the Safely Remove above is
Windows' own query and removal.

**The channel on 2000.** The release `xhcicm.sys` was replaced by the qemu
flavour of the same commit (copied over `system32\drivers\xhcicm.sys`,
`XhciLogVerbosity` 2 on the controller's driver key) to read its port-0xE9
notes, `E:\xhci98-vm\stor1-legs\w2k\debugcon-d.log`:

| Moment | Notes |
|---|---|
| start | `cm.nvlink.nt=80000000`: the callback object `\Callback\Stor98LinkState` registered, no query yet |
| hot plug | `cm.nvlink.up=01000401`: entry 1, bus 4, slot 1, generation 1, before the invalidation |
| unplug after Safely Remove | `cm.nvlink.icm-down=00000402`, `cm.nvlink.down=00000001` (the statement sent on the emulator's SHPC bridge), `cm.nvlink.await=00000000`, `cm.nvlink.retired=00020801` |
| re-plug | `cm.nvlink.up=02000401` |
| surprise unplug | `cm.nvlink.icm-down=00000402`, `cm.nvlink.down=00000003`, `cm.nvlink.await=00000000` |
| the controller's stop (restart) | `cm.nvlink.ups=2`, `sent=2`, `acked=0`, `ack-timeouts=0`, `no-receiver=2`, `queries=0` |

`queries=0`: Stor98 `0.3.0.1` never called the callback, so the statements
had no reader and nothing waited for an acknowledgement. Its source says
the same: the NT receiver is a stub in `0.3.0.1` (`src/platform.c`,
`PlatCmLinkOpen`, "STUB on NT ... the table is not opened yet"). The
release notes were worded to match.

**Two ME readings on the guest that was updated in place.** On the ME
overlay that had run Stor98 `0.3.0.0` and was moved to `0.3.0.1` by Update
Driver with E: mounted, the restart that followed stopped on "VCACHE(01) +
00000E4D Error: 0E : 0028 : C004E4E9", and the boot after it hung in
ScanDisk of NVME32 with "Statemgr" not responding; after a power cycle the
hot plug, the Safely Remove and the unplug passed, but the first read of E:
after the re-plug never returned and the guest's clock slowed to a crawl.
Neither was seen on a fresh ME overlay installed with `0.3.0.1` alone
(`E:\xhci98-vm\stor1-legs\me2`), where every leg above passed, the restart
with the drive attached included. Not investigated further; an in-place
update of the NVMe driver with its volume mounted is not a path the
release notes describe. The first boot of that same overlay after a
relaunch also hung once at a cursor before the logo; a reset brought it up
in Safe mode, and every boot after was normal.

Evidence: `E:\xhci98-vm\stor1-legs\` - `shots\` (screendumps by guest and
step), `w98\`, `me\`, `me2\`, `w2k\` (`events.txt`, `debugcon-*.log`,
`serial-*.log`, `trace-*.log`, `qemu-*.cmdline`), `pids.txt` (every QEMU
started and how it ended); the cut's log `E:\xhci98-wt\stor1-legs\out\cut-x86.txt`.

## Noted, not changed

- `INSTALL /devices` says "speed not known" where XHCITR shows the link
  rate: XHCIINST reads the registry record, which has no rate (record 24
  section 3.1); XHCITR reads the query device.
- An install whose only change was the tool says "Please restart
  Windows" (exit 3).
- On Vista the tray was once "Not Responding" in tasklist at the first
  login while devices were still installing, and "Running" at the next
  sample.
