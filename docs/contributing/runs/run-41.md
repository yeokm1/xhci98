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

## Plain USB devices (record 25 revision 7)

The owner's decision of 2026-10-16 (Safely Remove for plain USB devices,
decision T6 reversed), read on branch `tr-usbeject` on development host
A, every window shown. xhci98's controller with four ports, each USB 2
and USB 3; a Bulk-Only stick, a UAS disk on `xhciuas.sys`, a tablet, a
hub with a keyboard behind it. XP and 7 as the standard user; 98 SE and
ME as their one user. The dry cut's `XHCITR.EXE` (x86, TEST label) was
read first; the fixes below came out of these readings, and each system
was read again on the tool of `88b2a108` or later.

| Reading | 98 SE | ME | XP | 7 x86 |
|---|---|---|---|---|
| label with its letter, "[can be stopped]" | pass | pass | pass | pass |
| stick at SuperSpeed: Delete, right click, Action | pass | pass | pass | pass |
| stick at High Speed | pass | - | - | - |
| UAS disk | pass | - | pass | pass |
| the tray's "USB devices" group and its stop | pass (message box) | pass (balloon) | pass (balloon) | pass (balloon) |
| after the stop | gone from Device Manager | gone | Code 47, "[stopped - unplug it]" | Code 47 |
| unplug, re-plug, the drive back | pass | pass | pass | pass |
| refusal with a file open | pass | - | pass | pass |
| tablet, keyboard, hub not offered | pass | tablet, hub | pass | tablet, hub |

- 98 SE: "QEMU QEMU HARDDISK (F:) SuperSpeed [can be stopped]"; the
  confirmation listed "USB Mass Storage Device", "USB Disk" and the disk.
  An MS-DOS Prompt with `type con > F:\HOLD2.TXT` running gave "Windows
  refused: F: is in use..."; a prompt merely sitting on `F:` did not, and
  the stop went ahead. The UAS disk: "(F:) SuperSpeed", stopped from the
  Action menu.
- XP: the details listed "Generic volume" in what stops, from the disk's
  removal relations. With `cmd` on the drive: "Windows refused: D: is in
  use (a program has a file or folder open on it). Close it and try
  again." `/eject id=` with its output redirected wrote the answer wrapped
  at 79 columns; `/report` ended 0.
- 7: as XP, the stick and the UAS disk, the tray item greyed "(stopped -
  unplug it)" after the stop.
- ME: the stop from a right click and from the tray, with the balloon.
- The keyboard read "Safely Remove: not offered - an input device"; on
  the development host a Bluetooth radio read "not offered - a wireless
  radio".

Defects found and fixed in these readings:

- XP answered a vetoed eject `CR_SUCCESS` with the veto type set: the
  tool first reported the stop, then "The device has started again, so it
  is not safe to unplug." (exit 4). A success with a veto type is now a
  veto (`655937a5`), and a veto from the device's own volume names the
  drive (`ba1341e7`).
- After the stop the held device read "not offered - no disk on it": the
  disk had gone with the eject (`655937a5`).
- 7 answered the relations call `CR_NO_SUCH_VALUE` for a stick with none,
  so it was "not now"; that answer now means none (`fadbc9b4`).
- ME: the tool page-faulted in the C runtime, in `/report` too, after a
  restart left the new controller half installed; the earlier tool did
  not. Property reads now go through a scratch buffer, and the
  registry-only drive-letter value is no longer asked of the configuration
  manager (`88b2a108`). Not seen again on ME, 98 SE, XP or 7.
- From the owner's P14s report: a unit at Code 29 read "[stopped - unplug
  it]". Only Code 47 says that now; any other code reads "not started -
  problem code N" (`5f5d8ce7`).

Not read: ME's refusal; the UAS disk's re-plug (the emulated UAS device
does not attach when added to a running machine). ME's controller in the
guest had no driver yet, and `INSTALL` left it to Device Manager, as it
says.

## Noted, not changed

- `INSTALL /devices` says "speed not known" where XHCITR shows the link
  rate: XHCIINST reads the registry record, which has no rate (record 24
  section 3.1); XHCITR reads the query device.
- An install whose only change was the tool says "Please restart
  Windows" (exit 3).
- On Vista the tray was once "Not Responding" in tasklist at the first
  login while devices were still installing, and "Running" at the next
  sample.
