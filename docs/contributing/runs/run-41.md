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

### The installer's screens and its install directory

The owner's requests of 2026-10-16, after running the installer on a real
machine under Windows 98 (record 23 revision 10, record 25 revision 8):
every line at most 79 columns, each choice on its own line with its
meaning, "Enter for [x]", and an install directory for the xHCI98 tools,
`C:\XHCI98` by default, with the package copy of 98 SE and ME in it and no
subfolder. Read on development host A, from a dry x86 cut (TEST label, no
NVMe driver) of branch `inst-ux`, in QEMU guests with a `qemu-xhci` and a
tablet, each a new overlay, every window shown. Before them,
`XHCIINST /selftest` rendered every screen of an interactive and a
listing run with made-up devices, at ordinary and at the longest names,
ids and paths, with no line wider than 79 columns.

- **98 SE, a first install, Enter for everything** (over the gold image's
  `2.3.x` driver): every screen fitted the 80-column MS-DOS window with no
  wrapped blank line; the Thunderbolt question, the three registry
  questions, the install directory (`Enter for [C:\XHCI98]`) and the
  confirmation (`Enter for [Y]`) each listed their choices one a line.
  The package's drivers went straight into `C:\XHCI98` with
  `XHCIINST.LST` and `XHCITR.EXE`; `DevicePath` named `C:\XHCI98`;
  `HKLM\Software\xhci98` `InstallDir` read `C:\XHCI98`, and the Run value
  `"C:\XHCI98\XHCITR.EXE" /tray`. Enter at "Open Device Manager now?"
  opened System Properties at its Device Manager tab. Update Driver on the
  controller, "Search for a better driver" with no box ticked, found
  `C:\XHCI98\XHCI98.INF`; asked for the "xhci98 xHCI Host Controller
  Disk", `C:\XHCI98` typed under "Copy files from" installed it (the
  driver date then read 10-16-2026). After the restart the tray icon was
  there. Pass.
- **98 SE, an update over an install of the `a5a1b31f` package**, whose
  tool was in `C:\WINDOWS\XHCI98` with its copy in
  `C:\WINDOWS\XHCI98\46CC237E` and no record, its tray running: the
  question offered `Enter for [C:\WINDOWS\XHCI98]` with "now:" naming it.
  Kept: the copy went flat into `C:\WINDOWS\XHCI98`, the `<hash>`
  directory and its `DevicePath` entry went, the tool stayed with its Run
  value, and `InstallDir` recorded `C:\WINDOWS\XHCI98`. Pass.
- **The same update, moving to `C:\XHCI98`** (typed at the question): the
  running tray was closed, the copy and the tool went to `C:\XHCI98`, the
  Run value named it, `C:\WINDOWS\XHCI98` was removed whole, `DevicePath`
  named `C:\XHCI98` alone, and no `MoveFrom` was left. The tray started
  from `C:\XHCI98` at the next login. Pass.
- **98 SE, the uninstall** of that moved install (REMOVE typed): the tray
  closed, `C:\XHCI98\XHCITR.EXE` and the copy removed, `C:\XHCI98`
  removed, the Run value, `DevicePath`'s entry and `HKLM\Software\xhci98`
  gone; the Device Manager steps printed, exit 4. Pass.
- **ME, a first install, Enter for everything** (the stock image, the
  controller with no driver): the copy went flat into `C:\XHCI98` with the
  tool, the numbered steps named `C:\XHCI98`, and Enter at "Open Device
  Manager now?" opened the Device Manager tab. Pass.
- **XP, `INSTALL /to C:\Tools\xhci98`** (the guest has no second hard
  disk): the question was not asked ("Install directory:
  C:\Tools\xhci98 (/to)"); the drivers installed from the local copy in
  `C:\WINDOWS\XHCI98\AE12C8F3`, the tool went to
  `C:\Tools\xhci98\XHCITR.EXE`, `InstallDir` and the Run value named it.
  The uninstall removed the tool, `C:\Tools\xhci98`, the copy,
  `C:\WINDOWS\XHCI98` and the record; `C:\Tools` stayed. Pass.

Noted: on 98 SE every update of the controller re-enumerates the tablet,
whose HID install asks for the Windows CD; the guests have none, and the
files were skipped. The cosmetic wrap of the question's drivers line seen
on these legs was shortened afterwards (one line), with no other change.

### The ORICO enclosure's pre-OS tunnel held at start (`u4-preos`)

The `usb4-emu` cross-check (`scripts\usb4-crosscheck`) built against QEMU
fork `2a893ea` failed one case of 225,320 checks: the ASM2464 model
behind `intel-adl`, tunnelled by the model's pre-OS connection manager,
with `XhciCmPreOsCleanup` 0 - the default - ended HELD for discovery
(state 6, why 14) instead of preserved.

**Where it was decided.** `tbtU4DiscoverPort` reads ROUTER_CS_0 to 6 of
the router once and requires Router Ready in step 2 (record 22 section
9.2). The PCIe walk was whole (both ends' Path Enable, `hd` 8, `hu` 9,
the LTSSM live, the lanes bonded), ROUTER_CS_5 read `81000000` (PTO and
CV), and ROUTER_CS_6 read `02000000`: Configuration Ready set, Router
Ready clear. The model sets Router Ready 5 ms after Enumerated
(`usb4-router-asm2464.c`, `ASM2464_RR_DELAY_MS`), and the harness starts
the connection manager the moment the model's pre-OS connection manager
has written Enumerated.

**Why it passed before.** The same harness and the same driver source,
built against an archive of QEMU fork `20655c3` (the `5ba7f60` emulator),
pass the case: ROUTER_CS_6 reads `03000000` there and the port is
preserved (`u4.preserved=00010809`). No ASM2464 file changed between the
two forks. What changed is fork `558d663`: `intel-adl` now answers Get
PCIe Downstream Entry Mapping Not Supported, so the connection manager's
mapping stops at once, about 4 ms of the simulated clock earlier, and the
discovery read lands inside the 5 ms. A model change exposed a
connection-manager rule that sampled Router Ready once: the real ASM2464
has not set it 0.4 ms after Enumerated (`usb4-emu`
`docs/captures/ORICO-ASM2464.md`, ROUTER_CS_6), and Linux waits up to
500 ms for it on a discovered router as well (`usb4.c`
`usb4_switch_setup`, which `tb_switch_configure` runs for one). This
driver's own connect path already waited (`tbtU4WaitReady`).

**The fix** (branch `u4-preos`, record 22 sections 9.2 and 11):
discovery waits for a clear Router Ready, up to 500 ms and reading only,
and then reads ROUTER_CS_5 and 6 again; past the 500 ms the step fails as
before. A resume verification does the same inside the resume's budget,
and since every wait there lets hot plug events through that are not
acted on for the port under verification, each verification of a USB4
router first proves the router the one classified (an unplug hint only
with no removal evidence, lane 0 unlocked, the same IDs and UUID, still
enumerated at its route) and proves it again after the wait.

| Reading | Result |
|---|---|
| Cross-check, fork `2a893ea`, the fixed driver | 225,122 checks, 0 failures; the case reads `u4.preserved=00010809` |
| Cross-check, fork `2a893ea`, `7d169c09` | 225,320 checks, 1 failure (the case above) |
| Cross-check, fork `20655c3`, `7d169c09` | the case passes; the two TS3 Plus cases fail as expected (that fork has no TB3 router under a software connection manager) |
| `test_u4cm` | 1,015 checks, 0 failures. New cases: the ASM2464 pre-OS tunnel with Router Ready 5 ms late (preserved, nothing held, the router written exactly as with Router Ready on time) and never set (HELD for discovery after one wait, left as found); a resume with Router Ready 5 ms late (verified, nothing written to the router), never set (HELD), the budget spent inside the wait (no poll past it) and another router (another UUID) in place during the wait (never verified as the first). Without the fix the first four of these fail; without the identity proof the last one does |
| `build-driver.cmd all` and `all -amd64` | BUILD + GATES PASSED (x86 at `da35584a`, amd64 at `60538f1b`; nothing under `src\` changed between them) |

**The guests** (QEMU fork `2a893ea`, `vm\usb4\bin-2a893ea`, SHA-256
`6a1dfcc8...bd66` checked before use; `usb4-generic`, `intel-adl`,
`preos-cm=on`, `usb4-device,device-model=asm2464` with the 128 MiB FAT16
NVMe image; i440FX, so the NVMe sits alone behind the tunnel bridge).
Overlays of the 98 SE gold image and of the Windows 7 x86 image under
`E:\xhci98-vm\u4-preos` on development host A, every window shown.

- **The `3.0.0.0` package, 98 SE** (the tunnel bridge, `xhcicm.inf` and
  `nvme98` through the hardware wizard): after the install, and again at a
  cold boot with the enclosure attached at power-on, XHCITR read "ASMedia
  246x ... approved", "PCIe tunnel: up (found at start)", the unit
  "QEMU NVM e Ctrl (E:)"; E: ("ORICO") opened with `TESTDATA.BIN`.
- **The `3.0.0.0` package, 7 x86** (`INSTALL /defaults /yes /nopause
  /thunderbolt`, then a cold boot with the enclosure attached): "PCIe
  tunnel: up (found at start)" (the NVMe controller had no driver; the
  tool labels it "stopped - unplug it").
- So the guests do not show the HELD port: the model's Router Ready comes
  5 ms after the pre-OS enumeration at power-on, and the driver starts
  seconds later. Only the cross-check, which starts the connection manager
  with no time elapsed, reached the window.
- **The fixed driver** (the `qemu` flavour of `xhcicm.sys` at `da35584a`,
  SHA-256 `87dfa347...a732`, copied over the installed file, then a cold
  boot with the enclosure attached): 98 SE, with `XhciLogVerbosity` 2,
  logged `u4.drom.unused=00000010` and `u4.preserved=00010809`, no
  `u4.router-not-ready`, and XHCITR read the tunnel "up (found at start)";
  7 x86 (its hash read in the guest) read the same in XHCITR.

## Bridges behind the tunnel port on NT

The question: when a chain of PCI-to-PCI bridges appears behind a
Thunderbolt tunnel port after boot, does Windows' own `pci.sys` on 2000,
XP and 7 x86 number it, nest its windows and start it with no restart?
98 SE's `PCI.VXD` does not; that is the P14s hot-plug Code 29. The earlier
emulator NVMe legs had no bridge between the tunnel port and the NVMe.

### Setup

- **Emulator and package.** Emulator `vm\usb4\bin-2a893ea` (SHA-256
  checked). Guests are overlays of `vm\win2k-xonly.img`, `vm\winxp.img`
  and `vm\win7.img`. The 3.0.0.0 cut package (no NVMe driver) was
  installed with `INSTALL /yes /thunderbolt /notray`.
- **Tree.** i440FX with the `tb3-alpine-ridge` NHI and its xHCI on bus 0.
  The tunnel port `tb0` is the emulator's SHPC `usb4-tunnel-bridge`, with
  `bus-reserve=8,mem-reserve=32M`. SeaBIOS honours those hints only on
  Red Hat bridge ids, so `tb0` got buses 1-9, memory
  `fca00000-fe9fffff`, prefetchable `fc000000-fc1fffff` and IO
  `c000-cfff`, like the P14s's 06-2A. The `8086:15C0` tunnel port of the
  Alpine Ridge tree gets no spare bus from SeaBIOS.
- **The chain.** A switch stand-in sits behind `tb0`'s gate: a USP
  `usb4-asm-pci-usp` (`1b21:2421`) at `tb0` slot 3, a DSP
  `pci-bridge-seat` (`1b36:000a`) below it, and an NVMe below that. Both
  bridges have no BAR (`shpc=off,msi=off`), as the ASM2464's `1b21:2463`
  ports have none. While the tunnel is down the gate hides the chain, so
  it stays in reset state (bus numbers 0, windows closed) until the
  enclosure is hot-plugged. Then the ICM approves it, the tunnel comes up
  and xhcicm invalidates `tb0`. The ORICO model (`device-model=asm2464`)
  also puts its own NVMe directly on the tunnel bus at slot 1.
- **Not usable.** The `usb4-asm2464-usp/-dsp` types are not
  user-creatable. 2000 and XP cannot boot on q35.
- **Two emulator traps.**
  - The emulator's tunnel-port id `1b36:0001` is in `xhcicmf.inf`'s
    model list, so a plain `pci-bridge` in the chain gets the filter INF.
    The first 2000 attempt did, and asked for a restart.
  - Hot-adding the chain with `device_add` needs `shpc=on`, which gives
    the DSP a BAR, and the DSP's 1 MB window plus that BAR does not fit
    the 1 MB window 2000 gives the USP: Code 12. Neither is the hardware's
    behaviour.
- **Re-plugs.** The emulator gives the gated chain back with its earlier
  configuration, so a re-plug reads re-enumeration and the first plug
  reads fresh numbering.
- **Recipe.** The command lines and steps are in
  `E:\xhci98-vm\nt-bridge\RECIPE-first-plug.txt` on development host A.

### pci.sys: numbered, nested, started, no restart

`info pci` was read with no prompt open. The bridges show in Device
Manager as "PCI standard PCI-to-PCI bridge, This device is working
properly". The NVMes have no driver: Code 1 once the wizard is cancelled,
BAR unassigned. Device Manager's Scan for hardware changes changed
nothing.

| | USP `01:03.0` | DSP `02:00.0` | Restart asked |
|---|---|---|---|
| 2000 | bus 2-3; memory `fe900000-fe9fffff`, prefetchable `fc100000-fc1fffff`, IO `c000-cfff` | bus 3-3; the same windows | no |
| XP SP3 | bus 2-3; memory `fca00000-fe9fffff`, prefetchable `fc000000-fc1fffff`, IO `c000-cfff` (all of `tb0`'s) | bus 3-3; the same | no |

The re-plugs on 2000 (two) and XP (three) brought both bridges back
started, with no restart. **So `pci.sys` does what `PCI.VXD` does not.**
Windows 7 x86 is read under "The child filter on a bridge" below.

### Safely Remove does not offer the unit

The ORICO's USP bridge sits directly on the tunnel bus, so on NT the
bridge is the unit. In these legs that bridge was not removable on the
first plug:

- **2000, first plug.** `INSTALL /devices` printed "PCI standard
  PCI-to-PCI bridge - pci, started ... removable: no". Unplug or Eject
  Hardware listed only the driverless NVMe on the tunnel bus ("PCI Device
  at PCI Slot 4", PCI bus 1, device 1). `INSTALL /eject 2` answered
  "Windows will not stop this device: it does not see it as removable
  now" (exit 4).
- **2000, plain unplug** (the enclosure removed with no eject). 2000
  showed its "Unsafe Removal of Device" dialog, naming both "PCI standard
  PCI-to-PCI bridge" entries, and the subtree left Device Manager.
- **2000, re-plug** of the same devnode. `/devices` read "removable: yes
  ... (listed in Safely Remove Hardware)". The applet listed the bridge
  with its subtree, and Stop gave "The 'PCI standard PCI-to-PCI bridge'
  device can now be safely removed from the system". The unplug after it
  raised no dialog, and the next re-plug came back started.
- **XP, every plug.** Two first plugs (two chain instances) and three
  re-plugs all read "removable: no". Safely Remove listed neither the bridge nor
  the driverless NVMe. A plain unplug on XP raised no dialog.
  `INSTALL /eject 2` printed "Windows has stopped PCI standard PCI-to-PCI
  bridge. The device has started again" (exit 4).

The qemu flavour of `xhcicm.sys` from the same tree (3.0.0.0 at
`1e7d667e`, unmodified) showed why. It ran with `XhciLogVerbosity` 3, and
the counters were read at the NHI's stop. Each first plug used a
never-seen chain instance (another `tb0` slot).

- **XP:** `cm.reenum.child.attached` 1 and `child.skipped` 1 on the first
  plug, 3 and 3 after two more plugs. On every plug the NVMe got the
  child filter and the bridge was skipped. A skip with no full snapshot is
  `cmChildCreate`'s `STATUS_DEVICE_BUSY`: the bridge's PDO already carried
  a device object when the answer passed the tunnel port's filter.
- **2000 (ACPI off):** `child.attached` 2, `skipped` 0 on the first plug.
  The bridge got its child filter and still read not removable. This
  fits the case roadmap EJRB.1 describes: Windows installing the bridge's
  driver for the first time rebuilds its stack without the child. The
  re-plug of the known devnode was removable.

### The child filter on a bridge (record 21 section 7.4 revision 2)

**Branch.** The fix is on branch `nt-childattach`: `5cbe3ac3`, with the
review rounds' `2d94a491` and `87935d29`.
- `build-driver.cmd all` and `all -amd64` passed every gate at
  `87935d29`, and the host suites pass.
- The legs ran its qemu `xhcicm.sys`, SHA-256 `9D7F8BBF...5E9ACA`, copied
  over the installed driver with `XhciLogVerbosity` 3.
- Each first plug used a chain instance the guest had never seen: XP `tb0`
  slot 6, 7 slot 3 (a fresh install), 2000 slot 8.

**What the log names.** On XP and 7 the driver already on the bridge's PDO
is ACPI's: `cm.reenum.child.under` reads `41435049` ("ACPI", after
`\Driver\`), with `under.count` 1. `child.decision` reads 2, attached above
it, on every plug. On 2000 with ACPI off nothing is attached, so no
`under` line is written, as before.

| | First plug of a never-seen chain | Re-plug |
|---|---|---|
| XP SP3 | attached above ACPI; the USP still "removable: no" | "removable: yes ... (listed in Safely Remove Hardware)"; the applet listed "PCI standard PCI-to-PCI bridge" with its subtree; Stop gave "Safe To Remove Hardware: The 'PCI standard PCI-to-PCI bridge' device can now be safely removed"; the unplug and the next re-plug brought it back started and removable |
| 7 x86 | attached above ACPI; "removable: yes ... (listed in Safely Remove Hardware)"; the applet listed the bridge; Stop gave "The 'PCI standard PCI-to-PCI bridge' device can now be safely removed from the computer" | after that unplug: started, "removable: yes" |
| 2000 | unchanged: "removable: no"; a plain unplug again showed "Unsafe Removal of Device" naming both bridges | "removable: yes" |

The NVMe directly on the tunnel bus read "removable: yes" on every plug on
all three systems.

**XP's first plug.** The child was attached there, and the bridge still
read not removable. This is the 2000 case of roadmap EJRB.1: XP installed
the bridge's driver for the first time and rebuilt the stack without the
child. Windows 7 did not lose it. So revision 2 fixes what never attached
(XP and 7 on every plug, ACPI), and the first plug of a never-seen unit on
2000 and XP stays EJRB.1's. For those, the route is the tool-side
fallback the owner decided for 3.0.0.0: query-and-remove in XHCITR and
`XHCIINST /eject`.

**`pci.sys` on 7 x86** (first plug, no prompt open), with no restart
asked:

| | USP | DSP |
|---|---|---|
| Bus | 2-3 | 3-3 |
| Memory | `fe400000-fe7fffff` | the same |
| Prefetchable | `fe000000-fe3fffff`, inside `tb0`'s non-prefetchable window, which a prefetchable range may use | the same |
| IO | `c000-cfff` | the same |

Both bridges were started; the NVMes showed Code 28, no driver. The
re-plug's `info pci` matched the first plug's.

## The fallback for a unit not marked removable (record 24 revision 5, record 25 revision 11)

Read on 2026-10-16 on development host A, overlays under
`E:\xhci98-vm\tr-fallback` (QMP 47990 to 47995, each guest in its SDL
window), with the nt-bridge recipe above: emulator `2a893ea`,
`tb3-alpine-ridge`, the SHPC tunnel bridge with the `usb4-asm-pci-usp`
(`1B21:2421`) and `pci-bridge-seat` chain cold-plugged behind it and an
NVMe at its end, no NVMe driver, so no volume. The package was a dry x86
TEST cut of branch `tr-fallback` at `e9701bdb`'s tools; its drivers came
before the merge of `nt-childattach` (record 21 section 7.4's attachment
above bus filters), so its re-plug readings on XP and 7 are the earlier
drivers'. Each system: a fresh overlay, `INSTALL /yes /thunderbolt
/notray /nopause`, a shutdown, then the chain at first boot and the
enclosure added (`device_add usb4-device ... device-model=asm2464`).
Evidence: `E:\xhci98-vm\tr-fallback\shots\` (`w2k-a3-*`, `w2k-b1-*`,
`xp-x2-*`, `w7-s2-*`), `pids.txt` and each guest's `cmdline-<tag>.txt`.

| System | First plug, `INSTALL /devices` | The stop | After it |
|---|---|---|---|
| 2000 | `[2] PCI standard PCI-to-PCI bridge`, "not marked removable: Safely Remove Hardware may not list it" | `INSTALL /eject 2`, Y, as the administrator: log `CM_Request_Device_EjectA = 0x00000017, veto 8`, then `CM_Query_And_Remove_SubTreeW(flags 0x3) = 0x00000000, veto 0`; "Windows has stopped PCI standard PCI-to-PCI bridge.", exit 0 | the bridge at problem code 24; `device_del` with no Unsafe Removal dialog; the re-plug started it, "removable: yes" |
| 2000, second never-seen unit (the chain moved to `tb0` slot 4) | not marked removable | `XHCITR /eject id=...` run as a limited user (`runas /user:tuser`): the confirmation, then "Windows does not list this device for safe removal. Stopping it needs administrator rights." with "Stop as administrator..."; the Run As Other User dialog appeared. Cancel: "Nothing was stopped: administrator rights were not given. Try again and allow it, or unplug the device and plug it in again." A wrong password: the shell's own logon error and the dialog again. A second administrator account: "Windows has stopped PCI standard PCI-to-PCI bridge. ..." with no second confirmation | no dialog at the unplug; the re-plug started it, removable |
| XP SP3 | not marked removable | `XHCITR` as a limited user: the same dialog, then XP's Run As. "Current user" with its protection box: the elevated run answered "Windows does not list this device for safe removal. Run this as an administrator to stop it." through the window. "The following user", an administrator: stopped, no second confirmation | after the unplug and a re-plug, still not marked (earlier drivers) |
| XP SP3, `XHCIINST` | not marked removable | `INSTALL /eject 2`, Y: log `CM_Request_Device_EjectA = 0x00000000, veto 8` - success with the veto type, which before this change read as stopped with nothing stopped - then `CM_Query_And_Remove_SubTreeW(flags 0x3) = 0x00000000`; stopped, exit 0 | |
| 7 SP1 x86 | not marked removable | `XHCITR` as a standard user: the dialog with the shield; User Account Control asked for an administrator's password. Cancel: the "not given" text. Accepted: stopped, no second confirmation | after the unplug and a re-plug, still not marked (earlier drivers) |
| 7 SP1 x86, `XHCIINST` | not marked removable | `INSTALL /eject 2` from an elevated prompt: `CM_Request_Device_EjectA = 0x00000017, veto 8`, `CM_Query_And_Remove_SubTreeW(flags 0x3) = 0x00000000`; stopped, exit 0 | |

All three systems accepted `CM_REMOVE_UI_NOT_OK | CM_REMOVE_NO_RESTART`;
no `CR_INVALID_FLAG` retry was needed. Not read: a veto with a file open
(no volume in this topology: no NVMe driver was installed), Vista, the x64
tools, the window's and the tray's own route to the same dialog (the
one-shot `/eject id=` takes the same functions), and whether a rescan
restarts a unit after this stop.

### Start at login, per user (record 25 revision 10)

Read on the tool of `33f66b09`, on 98 SE and on 7 x86 as the standard
user, the tool in place of the installed one:

- The icon's right-click menu and the View menu showed "Start at login"
  ticked. Unticked from the icon, the View menu showed it unticked; after
  a restart no icon came (on 7 the hidden-icons area held none).
- Ticked again from the View menu: after a restart the icon was back, on
  both systems.
- 7: with the machine's Run value deleted by an administrator, the item
  read "Start at login (off for this computer)", grey; the value was put
  back afterwards.
- On the development host, `XHCITR64 /tray` with `TrayAtLogin` 0 ended in
  about a second with exit 0 and no icon.

`INSTALL /notray` already removed the machine's Run value on an existing
install, and an install without it writes the value again; XHCIINST is
unchanged.

### The tree's labels (record 25 revision 10, addendum)

Read on the tool of `d144edef` (the root hub's total was then taken from
its hub descriptor, `779b676a`), as the standard user:

- 98 SE and 7 x86: "xHCI98 USB 3.x eXtensible Host Controller (xhci98) -
  Red Hat 1B36:000D", "Root hub - 8 ports (4 USB 2.0, 4 USB 3.x)", "[Port
  3, USB 3] QEMU QEMU HARDDISK (G:) SuperSpeed" on 98 SE ("... USB Device
  (F:)" on 7), the ports of the full-speed hub behind it "USB 1.1".
- Development host A (Windows 11, `/report` as the user): a Thunderbolt
  chip's own xHCI read "USB xHCI Compliant Host Controller (Windows) -
  Thunderbolt controller's USB (8086:5782)", its NHI in the walk but not
  driven by `xhcicm.sys`, so with no generation; the AMD controller "-
  AMD 1022:1587"; "Root hub - 4 ports (2 USB 2.0, 2 USB 3.x)".
- Not read: the label of an emulated Thunderbolt chip's xHCI under
  `xhcicm.sys` ("Thunderbolt 3 controller's USB").

## Stor98 behind the Thunderbolt 3 controller (STOR.2)

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

## Logging level (record 25 revision 13, leg T.10)

2026-10-16, development host A. The package is a dry x86 TEST cut
("logging level legs") of branch `tr-loglevel` at fb6ce506, installed with
"Install Thunderbolt 3 / USB4 support?" answered Y on the TB3 guests of
`E:\xhci98-vm\tr-loglevel` (w98-tb, w7-tb: an Alpine Ridge host with its
xHCI and the tunnel bridge). The install writes `XhciLogVerbosity` 0 in
both drivers' keys, so the dialog's first reading is xhci98 "0 - off
(XhciLogVerbosity)" and xhcicm "0 - off (XhciLogVerbosity, old name)".

### Windows 7 x86, a standard user: pass

Installed as the administrator `test`, then logged on as `tuser` (Users
only). The tray icon started for `tuser`; View ends with "Logging
level...", and the right-click menu of each xhci98 controller and of the
Thunderbolt 3 host interface ends with it; each node's details carry its
"Logging level:" line. The dialog listed two xhci98 controllers and one
host interface at the readings above, "Now: 0 - off" for both, and the
button "Set as administrator..." with the shield.

- 3 for both, Set as administrator, the credentials of `test` at User
  Account Control: "xhci98: XhciLogVerbosity set to 3 - adds port status,
  for 2 controllers.", "xhcicm: XhciCmLogVerbosity set to 3 - adds port
  status, for 1 host interface." and the restart question; Yes restarted
  Windows from `tuser`'s session (`ExitWindowsEx` as a standard user).
- After the restart, as `tuser`: `reg query` read `XhciLogVerbosity` 0x3
  in both controllers' keys and `XhciCmLogVerbosity` 0x3 and
  `XhciLogVerbosity` 0x3 in the host interface's (the old value rewritten
  because it was there); `XHCITR /report` and the dialog read "3 - adds
  port status (XhciLogVerbosity)" twice and "3 - adds port status
  (XhciCmLogVerbosity)", "Now: 3" for both.
- A cancelled User Account Control prompt: "Nothing was changed:
  administrator rights were not given."; the dialog read the stored 3
  again.
- 2 for both through Set as administrator: the two "set to 2 - the log
  (default)" lines; No to the restart; the dialog and `reg query` read 2
  for all four values.

The dialog's text fits at 800 x 600; the list shows about five lines, so
the xhcicm reading is below a scroll. The list opened with its text
selected; Set takes the focus since ca233507 (not re-read on a guest).

Afterwards (the owner, 2026-10-16): `xhcicm.sys` has never been
released, so the tool reads and writes only `XhciCmLogVerbosity` for it;
the old-name reading and writing read in these legs were removed (record
25 section 7.5).

### Windows 98 SE: pass

Installed from `D:\PKG\INSTALL` with the Thunderbolt question answered Y
over the gold image's older `xhci98.sys`: INSTALL said no Thunderbolt or
USB4 controller was found, ended "not everything is done" (exit 4) and
opened Device Manager, and Update Driver from `C:\XHCI98` (the 98 SE CD
for `usbd.sys`) then installed `xhci98.inf` and `xhcicm.inf`; after the
restart the root hub installed silently and the tray started. The
dialog's first reading was the install's 0 for every device, the old
name for xhcicm, and the right-click menus of a controller and of the
host interface end with "Logging level...". The tree listed both
xhci98 controllers and the Thunderbolt 3 controller ("[ICM], active");
the dialog's button reads "Set", with no elevation, and the dialog fits a
640 x 480 screen whole.

- 3 for both, Set: the same two "set to 3 - adds port status" lines and
  the restart question; Yes restarted Windows.
- After the restart: `regedit /e` of `...\Services\Class` held
  `XhciLogVerbosity`=dword:00000003 in both controllers' keys and
  `XhciLogVerbosity` and `XhciCmLogVerbosity`=dword:00000003 in the
  Thunderbolt 3 controller's; `XHCITR /report` read "Logging level: 3 -
  adds port status" for each, `(XhciCmLogVerbosity)` for the host
  interface.
- 2 for both, Set: "xhci98: XhciLogVerbosity set to 2 - the log
  (default), for 2 controllers.", "xhcicm: XhciCmLogVerbosity set to 2 -
  the log (default), for 1 host interface."; the dialog then read "2 - the
  log (default)" for every device and "Now: 2" for both.

Evidence: `E:\xhci98-vm\tr-loglevel\` - `shots\lv7-*` and `lv98-*`,
`pids.txt`, `RECIPE.txt`; the cut's log `E:\xhci98-wt\tr-loglevel\out\cut-x86.txt`.

## Noted, not changed

- `INSTALL /devices` says "speed not known" where XHCITR shows the link
  rate: XHCIINST reads the registry record, which has no rate (record 24
  section 3.1); XHCITR reads the query device.
- An install whose only change was the tool says "Please restart
  Windows" (exit 3).
- On Vista the tray was once "Not Responding" in tasklist at the first
  login while devices were still installing, and "Running" at the next
  sample.
- In the `u4-preos` legs one Windows 7 x86 boot with the enclosure at
  power-on stopped the emulator itself about 18 s in: the QEMU process at
  0 CPU, its QMP socket silent, its trace last written during the
  connection manager's discovery reads. It was stopped; the next boots
  (after Windows 7's Startup Repair) ran normally with the same driver.
  Not reproduced; no cause read.
