# Design Record 25 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Status: revision 15, 2026-10-19 (revision 15, Windows 2000's USB disk
class and its stopped mark, roadmap-usb4 task 42-A.28: sections 6.5 and
14; revision 14, XHCISNAP's reading
moved here, below; revision 13, the drivers' logging level, below;
revision 12, PCI configuration and xhci98's query request, below).
Revision 11 adds the not-removable unit's stop (below).
Revision 10 adds a per-user "Start at login" choice
(section 7.4, the owner's request of 2026-10-16), with an addendum on the
tree's labels (end of section 7.4). Revision 9 adds the drivers' logs
(below). Revision 7 adds Safely Remove for plain USB
devices (section 6.5), by the owner's decision of 2026-10-16 that reverses
decision T6; sections 1.3, 3.1, 7.1, 7.3, 11, 12 and 14 change with it.
Revision 6 was reviewed and revised five times early on 2026-10-16
(`63cbfdbe`); the code is written against both. Revision 8, the same day, adds the install
directory (see below). Written for the owner's instructions of
2026-10-16: the command-line `INSTALL /devices` and `INSTALL /eject` of
design record 24 are not easy to use, so a separate graphical tool shows
every USB and Thunderbolt 3 / USB4 controller and device with its details,
ejects a tunnelled device as Windows' own Safely Remove Hardware does, and
sits in the notification area. It ships in `3.0.0.0`, which the owner held
for it, as roadmap Phase 41 (`roadmap-usb4.md`). Section 12 lists the
decisions taken in writing it, each an orchestrator decision for the owner
to confirm unless it names the owner; section 13 what is open.

Revision 8, 2026-10-16 (the owner, after running the installer on a real
machine): XHCIINST installs the tool in an install directory the user
names, `C:\XHCI98` on the system's drive by default, instead of
`%windir%\XHCI98\` (record 23 section 9.6); sections 2 and 8 follow. On
98 SE and ME the package's drivers share that directory with it, named
by XHCIINST's lists, which never name the tool. The tool names no
directory of its own, so nothing in it changes.

Revision 9, 2026-10-16 (the owner): the tool shows the drivers' logs, so
a user can send one from a release install with no debug build and no
DebugView. `xhcicm.sys`'s query device answers a second request with its
log ring and counters (section 4.5); `xhci98.sys`'s log and counters are
read as `XHCISNAP` reads them, with no change to that driver (section
4.6); each controller has a "Driver log" node, and Save Report and
`/report` carry both logs (section 3.5). Sections 1.2, 3.1, 4.2, 7.3, 10,
11 and 12 change with it.
Revision 11, 2026-10-16 (the owner, for `3.0.0.0`): a Thunderbolt unit
that Windows does not mark removable is stopped by record 24 section
7.2's query-and-remove route (its revision 5), and the tool offers the
administrator rights that route needs when it has none (section 6.6):
"Stop as administrator..." starts the tool elevated for that one stop,
and only then. Sections 3.3, 6.2, 6.4, 7.3 and 12 change with it, and
section 3.3 words a Thunderbolt 3 router's link behind a USB4 host as
Thunderbolt 3. The tray and the window never run elevated otherwise, and
the manifest stays `asInvoker`.

Revision 12, 2026-10-16 (the owner): the tool shows PCI details Windows
does not, read-only, through this package's drivers: a "PCI
configuration" node under each NHI and each xhci98 controller (section
3.6). `xhcicm.sys`'s query device answers a third request with the
configuration space of the NHI, of each tunnel port and, on 2000 and
later, of every function below a tunnel port (section 4.7). `xhci98.sys`
gains a query request of its own on `\\.\HCD<n>`, framed as `xhcicm.sys`'s
log request is, that serves its log ring, its counter block and its
controller's configuration space (section 4.8; the owner's
logging-consistency decision of 2026-10-16, which reopens decision T3),
and XHCITR reads xhci98's "Driver log" through it too. Both drivers'
logging level defaults to 2 from this revision (the owner, 2026-10-16;
design record 08 section 13.2, amended). Sections 1.2, 3.5, 4.6, 10, 11
and 12 change with it.

Revision 13, 2026-10-16 (the owner, for `3.0.0.0`): a "Logging level"
item shows and sets both drivers' log levels - `xhci98.sys`'s
`XhciLogVerbosity` and `xhcicm.sys`'s `XhciCmLogVerbosity` - and
replaces `XHCISNAP -verbosity` and `SNAPON.BAT` (XHCISNAP still ships in
`3.0.0.0`, unchanged, and is retired in the next release). It is the one
driver setting the tool writes (section 7.5). A standard user on 2000 to 7 sets
it through a second mode of section 6.6's elevated run, `/setlevel`.
Sections 1.2, 1.3, 7.3, 11 and 12 change with it.

Revision 14, 2026-10-19 (roadmap-usb4 42-A.4, the owner's 42-0.3
decision of 2026-10-18): XHCISNAP is retired in `3.1.0.0` and its
reading moves into the tool: `XHCITR /snapshot`, `/dump` and `/probe`,
File > Save Snapshot..., and a "Snapshot" part at the end of Save Report
and `/report` (section 15). XHCISNAP.EXE and its SNAPON.BAT, SNAPGET.BAT
and SNAPOFF.BAT leave the package. Sections 4.6, 4.8 and 12 change with
it.

Like record 24 it is not a Thunderbolt control panel: it approves nothing,
changes no security level, updates no firmware, and shows no secret or
authentication material.

## 1. What it is for

### 1.1 The problem

- `INSTALL /devices` and `INSTALL /eject` (record 24) are console
  commands. They are the only eject on Windows 98 SE and ME, and the owner
  finds them hard to use.
- No target system shows, in one place, what is connected to an xHCI
  controller or behind a Thunderbolt or USB4 tunnel, at what speed, with
  which driver and in which state. The tools that do part of it
  (Microsoft's usbview) do not run on every target, know nothing of the
  tunnels, and do not ship with this package.
- On Windows 2000 to 7 the user needs no administrator rights to stop a
  removable device with Windows' own applet; record 24's tool needs them
  on NT, because it asks `xhcicm.sys` for a fresh status record by writing
  a value under `HKLM\SYSTEM\...\Control\Class` (record 24 section 3.3).

### 1.2 Scope

- **XHCITR.EXE** (x86, every target) and **XHCITR64.EXE** (x64: XP x64,
  Vista x64, 7 x64), a Win32 GUI program separate from XHCIINST (owner).
  XHCIINST keeps `/devices` and `/eject`.
- One window, laid out like Microsoft's usbview (owner): a tree on the
  left, the selected node's details as text on the right, a status bar,
  and View menu check boxes that show or hide each category (owner:
  "everything, with check boxes").
- The tree shows every USB host controller Windows reports (xhci98's and,
  where present, Windows' own EHCI/UHCI/OHCI ones), their root hubs,
  ports, hubs and devices, and every Thunderbolt 3 / USB4 host interface
  (NHI) with its routers, ports, tunnels and the Windows devices behind
  each tunnel.
- Per USB device: speed, port, VID:PID, class, driver, problem code, power,
  and, behind a check box, the decoded device, configuration, interface,
  endpoint, BOS and string descriptors (owner).
- Eject of a tunnelled unit from the window and from the tray icon, with
  record 24's safety rules (section 6).
- A notification-area (tray) icon: a left click lists the units to eject,
  a double click opens the window, balloons announce plug, approval,
  disconnection and the outcome of an eject where the shell has balloons,
  the tooltip carries the last event where it has not; it starts at
  login (owner).
- Runs as the logged-on user, with no administrator rights and no UAC
  prompt at start (owner); an action that needs rights asks for them then.
- A read-only query device in `xhcicm.sys` (section 4), so the tool reads a
  current status without administrator rights and gets the details the
  registry record lacks (link rate and width, liveness). `xhci98.sys` is
  not changed (decision T3).
- Since revision 9, both drivers' logs and counters, read live, in the
  window and the report (sections 3.5, 4.5 and 4.6).
- Since revision 12, the PCI configuration of the NHI, its tunnel ports,
  the functions below them and each xhci98 controller, read-only, in the
  window and the report (sections 3.6, 4.7 and 4.8); `xhci98.sys` gains a
  read-only query request for it and for its log (decision T3).
- An application icon (owner): `xhcitr\xhcitr.ico`, drawn for this project
  (section 9).
- The two drivers' logging levels, shown and set (revision 13, section
  7.5).

### 1.3 Not in scope

- Approving, rejecting or authorising devices; security levels; firmware.
- Changing any driver setting from the tool but the two logging levels
  of section 7.5 (the other registry values stay `tuning.md`'s, set by
  hand or by XHCIINST).
- Forcing a removal Windows vetoes, ever, or stopping a Thunderbolt
  device that is not a unit (record 24 section 4.2).
- Stopping a USB hub, a root hub, a controller, an input device (HID) or a
  composite device with an input function, a radio, or a USB device that
  section 6.5 does not offer (one with no disk or volume on it). Until
  revision 7 plain USB devices got details only (decision T6); section 6.5
  is what replaced that.
- Writing `LowerFilters` to repair a missing child filter (roadmap-usb4
  42-0.4 and 42-A.5, was EJRB.1); the tool shows the condition (record 24 section 4.3) and says
  what to do.

## 2. Where it lives

- Source: `xhcitr\` (new): `xhcitr.c` (window, tray, OS calls),
  `tr_core.c` / `tr_core.h` (pure: query and record parsing, descriptor
  decoding, the event diff, keys, text), `xhcitr.rc`, `xhcitr.manifest`,
  `xhcitr.ico`, `build.cmd`, `selftest.cmd`, `check-imports.ps1`,
  `README.TXT`. It links `xhciinst\inst_core.c` for record 24's pure
  rules (unit selection, protection, eject answers and texts) and carries
  its own port of XHCIINST's devnode walk and post-eject assessment.
- Driver: `src\cm\cm_query.h` (the answer's layout), `cm_query.c` (pure
  builder), `cm_qryos.c` (the query device).
- Package: `tools\xhcitr\XHCITR.EXE`, `XHCITR64.EXE`, `readme.txt`.
- Installed copy: `<install directory>\XHCITR.EXE` (or `XHCITR64.EXE`;
  the install directory is record 23 section 9.6's, `C:\XHCI98` by
  default, `%windir%\XHCI98\` before revision 8),
  started at login from
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`, value
  `xHCI98 USB Tree`, data `"<path>" /tray` (section 8).
- Host tests: `test\test_tr.c`; the builder's in the CM suite.

## 3. The window

### 3.1 Layout

- Title "xHCI98 USB Tree". A resizable top-level window: tree view (left),
  read-only multi-line edit with both scroll bars (right), a splitter
  between them, a status bar ("N USB devices, M Thunderbolt/USB4
  devices"). Size, splitter position and the View check boxes persist
  per user under `HKCU\Software\xHCI98\XHCITR`.
- Menus:
  - **File**: Refresh (F5), Save Report... (the whole tree with every
    node's details as text, for a bug report, the driver logs of section
    3.5 included), Exit.
  - **Edit**: Copy, Select All (the details pane).
  - **View** (check boxes): Thunderbolt and USB4; USB controllers;
    Windows' own USB controllers (not xhci98's); Empty ports;
    Descriptors; Devices behind each tunnel; Auto Refresh.
  - **Action**: Safely Remove... (enabled on a unit or a node inside
    one, and on a USB device section 6.5 offers), Properties (Device Manager's properties of the selected devnode
    through `DeviceProperties_RunDLL` on 2000 to 7; disabled on 98 SE and
    ME).
  - **Help**: About (icon, version, the release it belongs to).
- A right click on a node opens the Action menu. Keyboard: arrows, F5,
  Enter = Properties, Delete = Safely Remove.

### 3.2 The tree

```
My Computer
  Thunderbolt and USB4
    <NHI description>  [ICM | USB4 software CM], <stage>
      Port 1 (bridge 00:1C.0): active
        <vendor> <model>  (Thunderbolt 3 | USB4 v1), approved
          PCIe tunnel: up  - <unit devnode description>
            <child devnodes: NVMe, USB controller, ...>
          USB3 tunnel: up
          DisplayPort tunnel: none
      Port 2: no device
  USB
    <controller description>  (xhci98 | Windows)
      Root hub
        [Port 1] <device description>  SuperSpeed
        [Port 2] <hub description>     High Speed
          [Port 1] <device>
        [Port 3] (empty)
```

A dock's own xHCI controller sits behind a PCIe tunnel; it appears under
the tunnel (as a Windows device) and, with its USB devices, in the USB
branch, marked "(behind <router name>)".

### 3.3 The details pane

- **NHI**: description, PCI ID, driver and version, connection manager
  (ICM or USB4 software CM), profile, stage and inert reason, security
  level and firmware version (ICM), liveness state, tunnel ports with
  their bridges, Device Manager status and problem code, and whether the
  shown state is current (section 4.3).
- **Router / device**: kind, vendor and model names, IDs, generation,
  link rate and width where the connection manager read them (USB4:
  10, 20 or 40 Gbit/s per lane, x1 or x2; under the ICM "not reported by
  the firmware"), route, approval, each tunnel's state, the unit's
  devnodes, whether Windows lists it in Safely Remove (record 24 section
  4.3) and the advice when not. Since revision 11 a unit whose capabilities
  say neither Removable nor SurpriseRemovalOK reads "Not marked
  removable", with section 6.6's route; and a Thunderbolt 3 router behind
  a USB4 host (generation Thunderbolt 3, or USB4 Version major 0) has its
  lane-adapter rate worded "Thunderbolt 3, 20 Gbit/s per lane, x2 (40
  Gbit/s)", never as a USB4 generation (`tr_link_text_gen`).
- **USB controller**: description, PCI ID, driver, `HCD<n>` name, root
  port count, problem code.
- **Hub**: hub descriptor (ports, power switching, compound, over-current),
  self/bus power, speed.
- **USB device**: port, connection status, speed (Low, Full, High,
  SuperSpeed, SuperSpeedPlus, or unknown), address, VID:PID, class,
  product and manufacturer strings, driver and driver key, problem code,
  current configuration, MaxPower; with Descriptors on, every descriptor
  decoded field by field, then in hex. The BOS's SuperSpeedPlus sublink
  speeds are labelled what the device supports; the negotiated lane rate
  and count are "not reported" (decision T3).
- Every node: the devnode instance ID, status flags and problem code.

### 3.4 Refresh

- F5, a device-change notification (`WM_DEVICECHANGE`,
  `DBT_DEVNODES_CHANGED`, coalesced 500 ms), or Auto Refresh (every 2 s
  for the Thunderbolt branch only) rebuilds the tree. The selection and
  expanded nodes are kept by key (section 7.2's router keys, instance
  IDs, controller + port path).
- A refresh reads only what the connection information carries (the
  device descriptor, speed, address, open pipes). xhci98 answers the
  device and first configuration descriptors from its cache (record 13
  section 8.11), but Windows' own stacks may send a configuration request
  to the device, so on every controller the configuration, string and BOS
  descriptors are read only for the selected node with Descriptors on,
  and for Save Report.

### 3.5 Driver logs (revision 9)

- **Where.** A "Driver log" node is the first child of each xhci98
  controller (label "Driver log (xhci98)") and of each NHI ("Driver log
  (xhcicm)"). No other controller has one. A refresh adds the node and
  reads nothing; selecting it reads the log then (sections 4.5 and 4.6),
  and so does Save Report and `/report`, once per node.
- **What it shows**, in this order: whether recording is on and at which
  `XhciLogVerbosity` (xhcicm: `XhciCmLogVerbosity`); when it is off, why and how to turn it on; the
  counters; the log, oldest record first, as the driver wrote it, one
  `label=XXXXXXXX` record a line.
- **Recording off.** Reading a log writes nothing; the levels are set in
  section 7.5's dialog (revision 13). The node's advice:
  - xhci98, level 0 (the driver does not answer) or 1 (counters only):
    "To record the log, set xhci98's level to 2 in View > Logging level,
    then restart Windows." On 2000 and later the text adds "(as an
    administrator, or with Set as administrator...)". Until revision 14
    (section 15) it named SNAPON.BAT in the package's tools\xhcisnap
    folder.
  - xhcicm, level below 2: "To record the log, set the DWORD value
    XhciCmLogVerbosity to 2 under HKEY_LOCAL_MACHINE\<the NHI's driver
    key> (as an administrator on 2000 and later), then restart
    Windows.", with the key the details pane already shows as "Driver
    key", spelled out as the full path (`...\Control\Class\...` on NT,
    `...\Services\Class\...` on 98 SE and ME).
  - A value the driver refused (out of range) is said as refused, with
    the same advice.
- **Addresses.** Neither node ever shows an internal address: xhcicm's
  answer carries none (section 4.5); xhci98's ring is shown only at
  levels 2 and 3 - at level 4 it can hold kernel addresses, so the tool
  withholds it and says to use level 2 - and its raw extension image is
  never shown or saved (section 4.6).
- **Text.** Every byte outside printable ASCII, CR and LF is shown as
  `.`. The details pane and the report hold the whole of both logs
  (16 KiB of ring each) with room to spare.

### 3.6 PCI configuration (revision 12)

- **Where.** A "PCI configuration" node follows the "Driver log" node
  under each NHI and each xhci98 controller. A refresh adds it and reads
  nothing; selecting it reads then (sections 4.7 and 4.8), and so does
  Save Report and `/report`, once per node. Kept apart from the Resources
  section (Windows' own allocations): different nodes, functions and
  files (`xhcitr\tr_pci.c`).
- **What it shows**, per function: vendor and device, revision,
  subsystem (a type 0 header's, or a bridge's subsystem capability),
  class, header type; Command and Status with their bits named; the
  interrupt pin and line ("Interrupt pin INTA, line 10"); the BARs as
  read, raw, with their encoding (I/O, 32- or 64-bit memory,
  prefetchable) - never sized, since sizing writes configuration space;
  for a type 1 header the buses ("primary 06, Bus 07-08") and the I/O,
  memory and prefetchable windows ("Window E0000000-E00FFFFF", or
  "none"); then the capability list: MSI with its enable state and vector
  count ("MSI: present, disabled (1 vector)"), MSI-X with its table size
  and enable state, PCI Express with its device or port type and the link
  ("PCIe link: Gen 3 x4 (capable Gen 3 x4)"), power management with its
  state, and any other capability by id; "not present" for MSI, MSI-X or
  PCI Express where the list has none.
- **The walk.** The capability list is walked only where the Status
  register's Capabilities List bit is set; each pointer is masked to a
  DWORD, below 40h ends the walk as invalid, a pointer seen before ends
  it as a loop, and it stops at 48 entries (the most the 192 bytes can
  hold); a capability whose registers lie past the 256 bytes read is
  named and not decoded. Each case is said in the text.
- **The NHI's node** lists the NHI, each tunnel port (with its source
  and whether it is active), and below each port the functions found
  there, indented by bridge depth; on 98 SE and ME "Not read on this
  system" stands for them (section 4.7). **An xhci98 controller's node**
  shows the controller.
- **Text.** Every line within 79 columns; the host suite holds it on a
  worst case. The constants are `tr_pci.c`'s own, each cited to the PCI
  Local Bus Specification 3.0, the PCI-to-PCI Bridge Architecture
  Specification 1.2, the PCI Bus Power Management Interface
  Specification 1.2 or the PCI Express Base Specification 4.0, by
  section; nothing is taken from a Microsoft header.
- **Older drivers.** A driver from before revision 12 is said as "Not
  served by this xhcicm.sys" or "Not served by this xhci98.sys: it is
  older than this tool". A configuration read the driver could not make
  reads "Unread".

## 4. The query device in `xhcicm.sys`

### 4.1 Why

The registry record (record 24 section 3) is readable by every user, but
only a fresh one, confirmed by an echo of a value written under
`HKLM\...\Control\Class`, is trusted for an eject, and that write needs an
administrator on NT. It also lacks link rate and width and the liveness
state. A device the tool opens with no access rights and asks with a
buffered, any-access IOCTL answers from the connection manager's own
published state, and works the same on 98 SE and on 7. Whether an
unelevated user can open it on every target is read by leg T.1 (section
13).

### 4.2 The device

- **A fourth role.** `xhcicm.sys` already serves the NHI FDO, the bridge
  filter and the child filter from one dispatch table, and passes every
  unrelated IRP of a filter down. The query device is a control device
  object with its own discriminator in the common extension; the dispatch
  routes every major function by object kind first, so the three existing
  roles behave exactly as before. The query device has no lower stack:
  `IRP_MJ_CREATE` succeeds only with an empty file name (anything after
  the device name is refused), `CLEANUP` and `CLOSE` succeed,
  `DEVICE_CONTROL` answers `CM_Q_IOCTL` alone (and, since revision 9,
  `CM_QL_IOCTL`, section 4.5; since revision 12, `CM_QP_IOCTL`, section
  4.7), everything else fails with
  `STATUS_INVALID_DEVICE_REQUEST`, and PnP and power never reach it.
- **Name.** `\Device\XhciCm<n>`, linked as `\DosDevices\XHCICM<n>`, `n`
  the first free of 0..15, created in the start IRP's system context (so
  the link is global on XP and later), with `FILE_DEVICE_SECURE_OPEN` on
  NT and the system's default security. The name is published as
  `XhciCmQueryName` (REG_SZ) in the NHI's hardware key after the link
  exists and written empty before the link goes (not deleted:
  `ZwDeleteValueKey` has no Windows 98 evidence, section 14). The tool
  reads an empty value as no query device, and trusts the mapping only
  when the answer's instance ID equals the devnode it read it from, so a
  stale or recycled name never stands for another NHI.
- **Lifetime.** Created after the NHI's start has succeeded (removed again
  if a later start step fails). On stop, surprise removal and remove the
  driver sets `CM_Q_F_STOPPING`, deletes the value and the link, marks the
  device disconnected under its lock, drops the NHI's pointer to it and
  deletes it; an open handle keeps the object alive harmlessly and gets
  `STATUS_DEVICE_REMOVED` (`STATUS_NO_SUCH_DEVICE` on 98 SE and ME) with no
  data. The device holds no pointer to the NHI: its answer lives in its
  own extension. A restart creates a new device with a new epoch.

### 4.3 `IOCTL_XHCICM_QUERY`

`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222900`, no input; the answer's layout is `src\cm\cm_query.h`
(Windows-type free, shared by the driver, the tool and the host tests):
a 64-byte header (magic `XCMQ`, version 1, size, the published-pass
counter, the pass's age in ms, the liveness state, flags inert /
truncated / stopping, the offsets, the start epoch, the NHI's bus,
device and function, the offset and length of the NHI's instance ID),
then the status record byte for byte as `CmStatusBuild` builds it (record
24 section 3.1), one 32-byte extension per router entry (link rate per
lane, width, the lane adapter's link state and speed code as read, the
IDs the connection manager holds for a router the record has none for,
flags), then the instance ID.

- **Publication.** After each pass the connection-manager thread builds
  the whole answer on its own thread and copies it into the query
  device's extension under a dedicated spin lock held for the copy only,
  then counts the pass and stamps its time from a running clock, all three
  under the lock. It keeps the answer's own content generation
  (`CM_Q_O_CONTENT_GEN`), bumped when anything but the pass counter, its
  age and the record's registry-only fields (generation, uptime, time,
  echo) changes: the record's generation is stamped only by the registry
  write, which skips an unchanged record. It does not depend on the
  registry write succeeding. The IOCTL copies out under
  the same lock and computes the age at answer time. Neither ever holds
  the lock across registry, hardware or a wait; the extension fields are
  read by the connection manager on its own path (the USB4 CM reads the
  lane speed and width when it sets up a USB3 tunnel's bandwidth,
  `tbt_u4cm.c`), never by the IOCTL.
- **Sizing.** Under 64 bytes: `STATUS_BUFFER_TOO_SMALL`, no data. Under
  the whole answer: success with the header alone and the size needed;
  the tool asks again with that size, at most 64 KiB. Otherwise the whole
  answer. The tool parses no more than the bytes returned. The largest
  version-1 answer is 64 + 128 + 24 x 160 + 200 bytes; the tool's first
  buffer is 8 KiB.
- **Current.** The window marks the Thunderbolt branch "not current" when
  the pass is 3 s old or more, the inert flag is set, or the liveness
  state is not ACTIVE. Age is for display only: an eject never relies on
  it (section 6.2).

### 4.4 What does not change

The registry record (`XhciCmStatus`, version 1), the ask and echo, and
XHCIINST's use of them stay exactly as record 24 has them. Revision 9
leaves the query's answer (`XCMQ`, version 1) as it is.

### 4.5 `IOCTL_XHCICM_LOG` (revision 9)

A second request on the same device, not a version 2 of `XCMQ`, so the
eject path's reader, its checks and XHCIINST are untouched, and a driver
from before revision 9 answers `STATUS_INVALID_DEVICE_REQUEST`, which the
tool shows as "this xhcicm.sys does not serve its log".

`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA41, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222904`, no input, the same device, the same access rules (opened
with no access rights; a standard user may read), the same removed-device
answer. The layout is `src\cm\cm_query.h`'s `CM_QL_*`: a 64-byte header
(magic `XCML`, version 1, size, flags, `XhciCmLogVerbosity` applied and as
read, the start epoch, the offset and length of the counters and of the
log, the records accepted since the start, the bytes the ring's wrap
overwrote, and the pass the counters belong to with its age), then the
counters as text, then the log as text. Both texts are ASCII lines
`label=XXXXXXXX` ending CR LF, the ring's own record format.

- **The log is copied, not drained.** The ring stays as it was, so the
  tray, the window and a second program read the same log, and the
  release flavour, which drains only under `XhciCmLogDebugView` 1, keeps
  the last 16 KiB. Where the thread hands the ring to DebugView after
  each pass - the `debug` and `qemu` flavours always, and any flavour
  with `XhciCmLogDebugView` 1 since the owner's decision of 2026-10-16
  (record 21 section 8) - the copy holds what has not been emitted yet;
  the answer's `CM_QL_F_EMITTED` flag says so. That emission withholds
  the same address values below level 4 (`CmQueryLogLine`).
- **Under the ring's own lock.** The IOCTL takes the query device's lock,
  and, while the device is connected, the NHI's `LogLock` inside it, and
  copies the ring, oldest byte first, straight into the system buffer;
  then releases both. No path takes them in the other order (`CmLogNote`
  holds `LogLock` alone; the thread's publication holds the device's lock
  alone). The copy is at most 16 KiB, so a producer on the thread or at
  `DISPATCH_LEVEL` waits microseconds. The device keeps the address of
  the NHI's log and its lock, set when it is created and cleared under its
  lock in `CmQueryClose`, which runs before the NHI's extension can go;
  this is the one exception to section 4.2's "no pointer to the NHI", and
  it is only ever followed with the device's lock held and `Connected`
  set.
- **The counters** are formatted by the connection-manager thread after
  each pass, into a buffer of its own, and published with the status
  answer under the device's lock (section 4.3), so they are the counters
  of pass `CM_QL_O_PASS`: the NHI's (`cm.*`), the ring's and the ICM's or
  the USB4 connection manager's, the re-enumeration's (`cm.reenum.*`) and
  the child filters' attached, skipped and refused (until now written only
  at stop), and the liveness state and refusals, in the stop dump's names;
  on 98 SE and ME the running CONFIGMG worker's counts (copied under its
  lock) are added to those its predecessors left, which the stop dump
  adds only at the stop. They are served at every verbosity: they hold no
  address and no user data. With no thread (its creation failed) the
  counters are formatted once when the device is created, pass 0; when
  the thread waits with no timeout the answer says so
  (`CM_QL_F_INERT`), and its pass and age say how old they are.
- **The ring's lifetime.** The ring lives in the NHI's extension from
  the device's addition to its removal and keeps its records across a
  stop and a start (the stop dump included), so the log can begin before
  the epoch it is served under; `CM_QL_O_APPENDS` counts from the
  addition.
- **No address.** `xhcicm.sys` never calls `XhciLogAppendAddress`, but
  three records carry an address as their value: `cm.live.bar-changed`
  (the BAR as read, a bus address) and, on 98 SE and ME,
  `cm.reenum.9x.gone.superseded` and `cm.reenum.9x.gone.id-changed` (a
  CONFIGMG devnode handle, a pointer). The builder writes their values as
  `--------` and sets `CM_QL_F_WITHHELD`. A record added later whose value
  is an address joins that list (`cm_query.c`), which the host suite
  holds.
- **Text.** A byte of the ring outside 0x20 to 0x7E, CR and LF is
  written `.`. When the oldest bytes can be the tail of a record - the
  wrap has overwritten bytes, or, in a checked flavour, a drain's chunk
  (`cm_log.c`, 128 bytes, not aligned to records) took its head - the
  text starts after the first line feed and `CM_QL_F_LOG_CUT` is set, so
  no value is ever shown without its label (and the withholding, which
  goes by the label, never misses one).
- **Bounds and sizing.** Counters at most 8 KiB (`CM_QL_CTR_MAX`, cut at a
  line with `CM_QL_F_CTR_CUT`), the log at most 16 KiB (the ring), the
  answer at most 64 + 8192 + 16384 bytes (`CM_QL_MAX_BYTES`). Sizing is
  `XCMQ`'s: under 64 bytes `STATUS_BUFFER_TOO_SMALL`; under the whole
  answer the header alone with the size needed; the tool's first buffer
  (32 KiB) holds the largest.
- **Matching.** The tool asks for the log only on a query device whose
  `XCMQ` answer names the NHI (section 4.2), and keeps the log only when
  its epoch equals that answer's.
- **Every target.** The same code on 98 SE, ME and NT; nothing in it is
  NT-only, and the 98 SE and ME open rule of section 14 applies.

### 4.6 `xhci98.sys`'s log, read as `XHCISNAP` reads it (revision 9)

Since revision 12 the tool reads xhci98's log through its query request
(section 4.8) and comes here only for a driver that refuses it.

No driver change (decision T3 holds). The tool opens the controller as it
already does (`\\.\HCD<n>`, or its interface path) and sends
`IOCTL_USB_USER_REQUEST` with `USBUSER_PASS_THRU` and the snapshot GUID,
schema 5 (design record 08, `docs\contributing\snapshot-reader.md`),
exactly as `XHCISNAP`'s `take_window` (now `xhcitr\tr_snap.c`'s): a
window of the extension region at offset 0 for the header (the ring's
offset), then one window at the ring's offset that must hold the whole ring,
and the HCD region (version 1) for the
counter block `XHCIHC_COUNTERS`. The ring is decoded with the header of
the window that carried it - the door fills header and payload together
under the controller lock (`hcd_door.c`) - and its verbosity, offset,
size, head and used count are checked there; the first window's header
only says where to ask. Every count and offset is checked against the
bytes returned, as `XHCISNAP` checks them; a schema or region version the
tool does not know is shown as such, not decoded.

- **Level 0**: the door answers status 6 and nothing else; the node says
  the log is off (or the controller is not an xhci98 one, which the tool
  already knows it is) and how to turn it on.
- **Level 1**: the counters; "Recording is off" and the advice.
- **Levels 2 and 3**: the counters and the ring.
- **Level 4**: the counters; the ring is withheld, because at level 4 it
  holds kernel addresses (`start.bar`, `start.ok`, `xhci_log.h`), and the
  node says to use level 2 for a log the tool can show (`XHCITR
  /snapshot`, `XHCISNAP`'s maintainer report up to `3.0.0.0`, is the
  level-4 reading; section 15).
- The counters are named from `XHCIHC_COUNTERS`'s own fields
  (`xhcitr\tr_hcctr.h`, one line a field, which the host suite holds to
  `src\xhci_counters.h` field by field by offset); a word past the table is
  "counter N". Nonzero counters only, as `XHCISNAP` prints them.
- Nothing of the extension image but the ring is kept, shown or saved.

### 4.7 `IOCTL_XHCICM_PCI` (revision 12)

A third request on the same device, as section 4.5's was a second:
`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA42, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222908`, no input, the same access rules and removed-device answer,
and `XCMQ`'s sizing rules (under 64 bytes `STATUS_BUFFER_TOO_SMALL`; under
the whole answer the header alone with the size needed). A driver from
before revision 12 answers `STATUS_INVALID_DEVICE_REQUEST`. The layout is
`src\cm\cm_query.h`'s `CM_QP_*`: a 64-byte header (magic `XCMP`, version
1, size, flags, the start epoch, the entries' offset and shape, the NHI's
location, the members listed, the reads made and refused), then one
288-byte entry per function: a 32-byte head (location, role - NHI, tunnel
port, below a port - state - read, not read on this system, refused,
absent - the port it is below, the bridge it is behind, its depth, a
port's source and flags) and the function's configuration space 00h to
FFh as read. The tool decodes it; the driver interprets nothing but the
header type and bus numbers its walk needs.

- **Read at the request**, on the caller's thread at `PASSIVE_LEVEL`,
  through `HalGetBusData` from offset 0 - the reads `cm_reenum.c` and
  `tbt_topo.c` already make of the same functions on every target - and
  never written. The functions to read are what the thread published with
  its last answer (the NHI's location and its domain's tunnel-port set,
  copied under the device's lock in `CmQueryPublish`); the reads hold no
  lock and follow no pointer to the NHI, so section 4.2's rule holds for
  this request. One request at a time builds into a buffer in the
  device's extension (a synchronization event); the answer is copied out
  from it.
- **One DWORD at a time, and the NHI's location on 98.** Each function's
  256 bytes are assembled from prefixes of offset + 4 bytes, the reads the
  topology makes: read on 98 SE, its HAL answers those and not one
  256-byte read, which left the tunnel port reading as absent. A DWORD
  past the HAL's answer ends the assembly: the entry keeps how many bytes
  were read (`CM_QP_E_VALID`, the rest zero), the walk enters a bridge's
  bus only where its bus numbers were read, and the tool decodes nothing
  past the bytes read and says so ("Only the first N bytes ..."; a
  capability list going on past them, with no "not present" claimed; a
  port not walked). And 98 SE's PCI bus answers no bus number or address for
  the NHI's PDO, so the topology's own reading of the NHI's location
  (`CM_REENUM.Nhi`) stands in. Read in the emulator on 98 SE (run 41):
  while the tunnel bridge's devnode had no driver (its Add New Hardware
  wizard cancelled), the query's reads of it answered "no function"; once
  Windows had installed its standard PCI-to-PCI bridge driver, the bridge
  was read whole. A real machine installs that driver at setup.
- **The walk below a port** (2000 and later): the port's secondary bus,
  then the secondary bus of each bridge found there whose numbers lie
  inside the port's secondary-to-subordinate range and above its own bus,
  each bus once; 32 devices a bus, eight functions where function 0 says
  multi-function. Bounds: 32 functions below all ports, 16 buses a port,
  8 bridges deep, 4 ports; a bound reached sets `CM_QP_F_TRUNCATED`, so
  the largest answer is 64 + 37 x 288 = 10,720 bytes and the tool's first
  buffer (16 KiB) holds it. A port with no bus numbers has nothing below.
- **Windows 98 SE and ME** (roadmap-usb4 task 42-A.14, 2026-10-19): the
  NHI and the tunnel ports are read as above; the walk below them is
  handed to the 98 CONFIGMG worker (`cm_cfgwk.c`, `CmCfgwkPciAsk`) and
  waited for 3 s at most. The worker reads below each member through
  `PCI.VXD`'s guarded `_PCI_Read_Config` (`CmCfgmgPciRead` behind
  `CmCfgmgPciOpen`, record 21 section 6.1.1), between its other
  operations and never alongside a numbering, and enters a bridge's bus
  only where `PCI.VXD` reads that bridge with the same bus numbers itself
  (the HAL, which on 98 SE answers only functions `PCI.VXD` knows) or
  where its own numbering gave them; a member whose controller was lost
  (`CONTROLLER_UNAVAILABLE`'s removal or no-scan re-enumeration owed) is
  not read below. The query device reaches the worker through a pointer
  the thread publishes and withdraws before it stops the worker, under a
  rundown count that covers the posting alone; the request is a block of
  its own that the asker and the worker share, freed by whichever lets go
  last, so a worker inside a blocked CONFIGMG call answers late or never
  and touches nothing of the device. Where the walk is not made, each
  member keeps one `CM_QP_S_NOT_READ_OS` entry with its reason in
  `CM_QP_E_INFO` (`CM_QP_NR_*`: no worker, the worker busy, `PCI.VXD`'s
  guard refused, the controller lost; 0 from a driver before it), and
  the tool says which.
- **No kernel address.** The answer holds configuration space and the
  driver's own counts. The BARs and bridge windows are bus addresses,
  shown by the owner's request; they are the ranges Device Manager's
  Resources tab shows any user.
- **Why `HalGetBusData` and not `IRP_MN_READ_CONFIG`** from the bridge
  and child filters: the filters exist on NT only, a child filter only
  for a device plugged after the bridge became active (record 21 section
  7.4), and reaching one from the query device would need a reference on
  a filter's lifetime; `HalGetBusData` reaches every function the walk
  names on every target, with no stack.

### 4.8 `xhci98.sys`'s query request (revision 12)

The owner's logging-consistency decision of 2026-10-16: `xhci98.sys` is
read as `xhcicm.sys` is, through a request of its own, not the USBUSER
pass-through. `IOCTL_XHCI98_QUERY` =
`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA48, METHOD_BUFFERED, FILE_ANY_ACCESS)` =
`0x00222920`, no input, on the controller's own `\\.\HCD<n>`, answered by
the HCD's dispatch at `PASSIVE_LEVEL`. Record 13 section 8.12 specifies
it; the layout is `src\xhci_qry.h`'s `XHQ_*`: a 64-byte header (magic
`XHCQ`, version 1, size, flags, the level applied and as read, the
offsets and lengths of the counter block, the log and the configuration
space, the ring's appends and overwritten bytes, the configuration
read's state, the build flavour), then the parts. Sizing is `XCMQ`'s.

- **What it serves follows the level**, as `xhcicm.sys`'s log answer
  does, and it is not gated by the door's enable step: the counter block
  (`XHCIHC_COUNTERS`, little-endian words, the one word the door computes
  computed the same way) from level 1; the log ring as text, oldest first,
  at levels 2 and 3 - at level 4 it can hold kernel addresses and is
  withheld (`XHQ_F_WITHHELD`), as section 4.6 withholds it; and the
  controller's configuration space 00h to FFh at every level, 0 included,
  because it is not the log.
- **The configuration space** is read on demand, one DWORD at a time
  through `HcdSvcConfigSpace`'s `IRP_MN_READ_CONFIG` to the PCI PDO, at
  `PASSIVE_LEVEL` under the door gate, before the controller lock; never
  written, read in no interrupt, transfer or power path. A refused read is
  `XHQ_PCI_FAILED`, a first DWORD of all ones `XHQ_PCI_ABSENT`; the tool
  says "unread" for both.
- **The ring and the counters** are copied under the controller lock,
  the lock the door's snapshot copies them under, straight into the
  request's buffer.
- **No address below level 4, in either driver.** With level 2 the
  default, every log record site of both drivers was read for values
  that are addresses (physical, bus, DMA, MMIO, kernel virtual, a
  pointer or a handle). Four were found below level 4 and became address
  records, which the shared ring takes at level 4 alone: `start.common.pa`
  and `pirq.rcba` (`hcd_ctl.c`), `qemu.inj.bulk.lost` (`hcd_inj.c`, the
  `qemu` flavour), and in `xhcicm.sys` - which had no address-gated
  append - `cm.live.bar-changed` and the two 98 devnode-handle records,
  through a new `CmLogNoteAddress`. `start.bar` and `start.ok` already
  were. Section 4.5's label filter stays as a second guard.
- **Compatibility.** The USBUSER pass-through, its clause table and its
  regions are untouched, so `usbui.dll` and `XHCISNAP` read the driver as
  before. An older `xhci98.sys` answers the request with
  `STATUS_INVALID_DEVICE_REQUEST`: the tool reads its log through the
  door as section 4.6 has it, and says "not served by this xhci98.sys"
  for its configuration space. `XHCISNAP` stayed in `3.0.0.0` unchanged;
  it was retired in `3.1.0.0` (roadmap-usb4 tasks 42-0.3 and 42-A.4,
  were SNAP.1 to SNAP.3), its reading moved into this tool (section 15),
  and the pass-through stays: Windows' own property tabs use it, and the
  tool's snapshot reads through it.

## 5. The USB branch

- Controllers: `\\.\HCD0` to `\\.\HCD63` on every target (usbview's own
  route reaches only HCD0 to HCD9, and xhci98 can take a higher number,
  record 13 section 8.1), opened with no access rights (every IOCTL below
  is `FILE_ANY_ACCESS`); on 2000 to 7 also every interface of
  `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`, de-duplicated by driver key.
- `IOCTL_GET_HCD_DRIVERKEY_NAME` (the controller's devnode, so its
  description and whether xhci98 drives it), `IOCTL_USB_GET_ROOT_HUB_NAME`,
  then per hub `IOCTL_USB_GET_NODE_INFORMATION` and per port
  `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`, falling back to the
  original `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION` where `_EX` is
  refused (older stacks; the legacy speed field cannot tell Full from
  High, so that is shown unknown), `_EX_V2` with its input initialised
  (the connection index, length and protocols, record 13 section 8.4) for
  the SuperSpeed and SuperSpeedPlus flags (xhci98 reports speed 2 in `_EX`
  on NT), `_DRIVERKEY_NAME`, `_NAME` for hubs (recursion goes by
  `DeviceIsHub` and the name, whichever form answered), and
  `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` for the configuration,
  strings and BOS.
- The structures the MSVC 6.0 headers lack (`_EX`, `_EX_V2`, the
  descriptor request) are declared in `tr_core.h` from their documented
  layouts, packed as the DDK packs them, each with a size check.
- A controller that is not xhci98's is shown with whatever its driver
  answers; an IOCTL it refuses leaves the field "not reported".

## 6. Eject

### 6.1 What can be ejected

Exactly record 24's units (section 4.2 there): a devnode directly behind
an active, verified tunnel port, not protected (NHIs, their ancestors, and
the USB controllers that share the NHI's grandparent), selected by its
instance ID within one NHI start (epoch), never by route. The rules are
`inst_core.c`'s (`inst_tb_units`, `inst_tb_protect`,
`inst_tb_unit_blocked`), fed from the query instead of the registry
record.

### 6.2 The transaction

1. Confirm with the user: a dialog like Windows' "Stop a Hardware
   device", the unit and the devnodes that will stop as the tree last
   showed them, OK / Cancel (from the tray menu the selection is the
   request and the dialog follows it), with record 24 section 5.4's
   reminder where it gives one (its revision 7, roadmap-usb4 task 43.8).
   Nothing below waits on the user.
2. Take the shared eject mutex (section 6.3). Query (Q1). An answer is
   eligible when the record's stage is active, the inert and stopping
   flags are clear and the liveness state is ACTIVE; refuse unless Q1 is.
3. Wait, polling every 100 ms for at most 3 s, until the published-pass
   counter has moved at least two past Q1's (wrap-safe) with the same
   epoch (Q2), and Q2 is eligible: one move could be a snapshot built
   before Q1 and published after it, two prove one built after the eject
   began. If none comes, refuse ("the controller is not answering"). This
   replaces record 24's ask and echo for this tool: like the echo, it
   proves a pass after the request rather than a recent one.
4. Walk the devnodes completely; query again (Q3). Q3 must be eligible
   and have Q2's epoch and content generation; otherwise walk once more,
   then refuse.
5. The selected unit must still be a unit, by instance ID, with the same
   devnodes under it as the user confirmed; otherwise refuse and show the
   new state. Immediately before the call, one more query must be
   eligible with Q3's epoch and content generation.
6. The call, record 24 section 7: `CM_Request_Device_EjectA` on NT
   (setupapi on 2000), with the not-removable fallback
   `CM_Query_And_Remove_SubTreeA`; on 98 SE and ME
   `CM_Query_Remove_SubTree` then, if accepted, always `CM_Remove_SubTree`,
   both `UI_NOT_OK` - since 2026-10-18 (record 24 section 7.1's revision;
   record 21 section 7.2, the 9x half; the code is 42-A.13) a unit's
   eject on 98 SE and ME is asked of the connection manager through
   record 24's ask and result values instead, the two calls staying for
   the plain USB devices of section 6.5. Never forced, never
   `DIF_REMOVE`, one unit at a
   time. Since revision 11 the not-removable fallback is
   `CM_Query_And_Remove_SubTreeW`, decided by `inst_eject_nt_step` as
   record 24 section 7.2 has it, and a unit not marked removable in a
   process without administrator rights goes to section 6.6 before this
   step.
7. The post-eject assessment ported from XHCIINST (the restarted-unit
   check, the USB4 tunnel watch, the unplug advice of record 24 section
   5.4 with every fact it needs), captured as text; on a veto, the vetoing
   device and the reason.
8. Release the mutex, then show the result: a message box from the
   window or a balloon (tooltip on 98 SE) from the tray.

### 6.3 The shared mutex

`Global\XhciinstTbAsk` on NT, `XhciinstTbAsk` on 98 SE and ME, the name
XHCIINST already uses. Both programs open it first with
`SYNCHRONIZE | MUTEX_MODIFY_STATE`; whichever creates it gives it a
security descriptor that grants those two rights to everyone and full
access to SYSTEM and Administrators, so an unelevated tray and an
elevated XHCIINST can both take it. An abandoned mutex counts as taken
(the transaction revalidates everything anyway). A wait longer than 10 s
refuses ("another program is removing a device"). It is held only for
one transaction, never while waiting for elevation.

### 6.4 Rights

On NT, `CM_Request_Device_EjectA` needs no administrator for a removable
device in the console session (Windows' applet runs as the user); a dock
can also need the undock right. If the call answers with a rights
failure, the tool says so and offers to run the eject elevated: it
releases the mutex and starts itself with `runas` (Vista and 7) and
`/eject id=<instance id>`, a one-shot mode (section 7.3) that runs the
whole transaction itself and exits. Since revision 11 a unit's elevated
run is section 6.6's, waited for and answered; a USB device's is as
before.

### 6.5 Plain USB devices (revision 7)

The owner's decision of 2026-10-16: the tool stops a plain USB device too,
on every target, as Windows' own Safely Remove does. Sections 6.1 to 6.4
stay the Thunderbolt units'; this section is the USB devices'. Nothing of
`xhci98.sys`, `xhciuas.sys` or `xhcicm.sys` changes for it.

**The device and what stops with it.** A USB device is the devnode a
port of section 5 names (`IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME`,
found in the walk by its driver key), on any controller the tool lists -
xhci98's or Windows' own, a dock's behind a tunnel included. Its **set**
is everything Windows stops with it:

- on 98 SE and ME, the device and every devnode below it in the walk (a
  composite device's functions, a storage device's disks): the removal of
  a subtree there is the subtree;
- on NT, the same, and then, until nothing new is added, the removal and
  ejection relations of every member (`CM_Get_Device_ID_ListA` with
  `CM_GETIDLIST_FILTER_REMOVALRELATIONS` and `_EJECTRELATIONS`, from
  `cfgmgr32.dll`, else `setupapi.dll`, looked up at run time) and every
  devnode below each: a disk's volumes are its removal relations, outside
  its subtree. A relation that cannot be read, or that names a devnode
  the walk does not hold, leaves the set unknown.

The port scan runs on every refresh, in the tray too, whatever the View
check boxes show, so the tray and `/eject` see the same devices as the
window.

**Listed.** A device is listed (in the tree with its state, in the tray's
menu, for `/eject`) when its set holds none of:

- a hub (the port says so, or the hardware or compatible IDs carry
  `USB\Class_09`), a root hub, or a host controller;
- an input device: a devnode of setup class `HIDClass`, `Keyboard` or
  `Mouse`, bound to `hidusb`, or with an instance, hardware or compatible
  ID beginning `USB\Class_03` or `HID\` - so a keyboard, a mouse, a
  tablet and a composite device with an input function are never listed;
- a wireless radio (device class `E0`, a Bluetooth adapter);

its devnode was found in the walk (a device with no driver has none on
98 SE and ME, and no disk anywhere), and its set holds a disk (setup
class `DiskDrive`), or, on NT, a volume (setup class `Volume`); on 98 SE
and ME the disks are found as section 14's drive labels already find
them. On NT the setup class is `CM_DRP_CLASS`; where that is empty, the
`Class` value of the class key its `CM_DRP_CLASSGUID` names (revision
15: Windows 2000 writes a USB disk's `ClassGUID` alone, so without it
the disk was no disk). Everything else connected gets details only, with the reason ("a
hub", "an input device", "a wireless radio", "no disk on it").

**Why not "removable" on NT.** The owner's instruction allowed a device
Windows reports removable. `xhci98.sys` reports `Removable` on every
device it presents (`src\hcd_pdo.c`), internal ports included: it reads
no ACPI `_UPC` or `_PLD`. The owner's ThinkPad P14s Gen 1 under 98 SE
has, on xhci98's root ports, a touchscreen, an integrated camera (a
composite device), a fingerprint reader and a Bluetooth radio, all
internal; under the capability rule the camera's parent is
`SurpriseRemovalOK` and drops out, but a fingerprint reader with a driver
or the radio would be offered, as Windows' own applet would list them.
So on every target a disk or a volume decides, and a USB device with no
storage (a camera, a network adapter, an audio device) is stopped by
unplugging it, as before. Windows' own applet still lists what it lists.

**Offered.** A listed device is offered when it is started and has no
problem code, its set is known, holds no more than 64 devnodes, and each
of them was read whole. One that is stopped is shown "[stopped - unplug
it]" (held for eject, `tr_held_for_eject`: Code 47 after an eject on XP
and later; on Windows 2000, which has no Code 47, Code 24, revision 15,
below); one with another problem code is shown with the code, and is not
offered.

On 2000 Code 24 is also a failed start, so it reads as held for eject
only for a devnode this process stopped (step 5's reading at Code 24, or
a unit's accepted stop), kept until a walk finds it started or a whole
walk no longer finds it, and, for the device at a USB port, only while
the port still reports a configuration (`tr_w2k_code24`): the stopped
device keeps its configuration until it is unplugged, and one plugged in
again starts unconfigured. A device stopped elsewhere (Windows' own
Unplug or Eject Hardware, or an elevated child of this tool) reads as
any Code 24 does. The tray and the window are one process, so a stop
from either marks both.

**What it is called.** Its disks' names and their drive letters, as units
are (section 14, "A unit named by its drive" in run 41): "USB Flash Disk
(F:)"; without a disk, the device's own name. The letters come from the
same code: on NT the storage device number of each disk's interface
against each `\\.\X:`, on 98 SE and ME the disk's
`CurrentDriveLetterAssignment`. On NT a disk is named by its friendly
name and found by its setup class, so on 2000 both depend on the class
read above (revision 15); the interface and device-number reads answer
on 2000 as on XP and 7 (`runs/run-42.md`, "XHCITR on Windows 2000").

**The transaction.** No query device, no epoch and no passes:

1. Confirm, as section 6.2 step 1 and with the same layout: "This stops
   <device> and everything on it:", each disk with its volumes, then every
   devnode of the set. Nothing below waits on the user. A thread of the
   tool's own, with a hidden top-level window and a message loop of its
   own, in every mode (`/eject` included), counts the device-change
   notifications Windows broadcasts (`WM_DEVICECHANGE`); it never waits on
   anything else, so no wait of the eject's thread - the question, the
   mutex, a walk - can make it miss one. The request carries the count read
   just before the model it was built from was read.
2. Take the shared mutex of section 6.3, so that no XHCIINST and no other
   XHCITR eject runs beside it.
3. Walk the devnodes again and find the device by its instance ID: its
   parent must be the devnode it was under when confirmed, its set (read
   again, relations included) exactly the devnodes confirmed, and it must
   still be offered by the rules above. Then, immediately before the call,
   read the count again: any notification since the request's count -
   while the question was open, the mutex awaited or the walk read -
   refuses, and the request's count is never moved forward. Otherwise
   nothing is called and the reason is shown ("The device list changed;
   nothing was stopped. Look at the tree again."). An instance ID alone
   cannot tell a device from an identical one plugged into the same port
   (a location ID, without a serial number); the count is what catches a
   re-plug. It narrows that case rather than closing it: a USB device has
   no attachment-lifetime token, and a re-plug faster than Windows'
   broadcast of it, between the last count and the call, is not seen.
4. The call on the device's devnode, never on a disk or a function below
   it:
   - NT: `CM_Request_Device_EjectA` (setupapi on 2000, as section 6.2),
     taking the veto type and name. No not-removable fallback: a refusal
     is shown as it is.
   - 98 SE and ME: `CM_Query_Remove_SubTree` with `UI_NOT_OK`, then, only
     if accepted, always `CM_Remove_SubTree`, as section 6.2 step 6 does
     for a unit.
5. Release the mutex, then show the result:
   - stopped: "Windows has stopped <name>.", then what a walk right
     after reads: the walk whole and the device gone, or held for eject
     (Code 47, or Code 24 on 2000, the addendum below) - "You can now
     unplug the device."; the device started
     again - "The device has started again, so it is not safe to
     unplug." (exit 4, as record 24 section 8.2 has it); anything else,
     the walk incomplete included - "Its state could not be read again:
     check that it is not running before you unplug it." On 98 SE and ME
     also "Until you unplug it, Device Manager's Refresh can start it
     again.";
   - refused: in plain words, with the drive letters where Windows'
     answer allows it. NT `PNP_VetoOutstandingOpen`: "Windows refused: a
     program is using F:. Close it and try again." (the device's name in
     place of the letters when it has none); `PNP_VetoWindowsApp` and
     `PNP_VetoWindowsService`: "<name> is using F:"; the other veto types
     and answers as record 24 section 7.2. 98 SE and ME
     `CR_REMOVE_VETOED`, which names no cause: "Windows refused: F: is in
     use. A program may have a file open on it, or a DOS box may be using
     it. Close it and try again." Nothing was changed by any refusal.
   - exit codes as record 24 section 8.2: stopped 0, refused or started
     again 4, the list changed or the call not offered 1, a removal that
     failed after an accepted query 2.
6. From the tray, a balloon with the outcome (a message box on 98 SE, and
   for an outcome longer than a balloon), as section 14 has it for units.

Never forced: no `CM_Remove_SubTree` without an accepted query, no veto
overridden, no `CM_Query_And_Remove_SubTreeA`, no `DIF_REMOVE`, one device
at a time.

Addendum (2026-10-17, branch `tr-w2kmsg`): Windows 2000 has no Code 47
(its DDK's `cfg.h` ends at `NUM_CM_PROB` 0x25), so a stopped USB device
left in its tree read "could not be read again" (the release matrix's 2000
stick). Step 5 now reads Code 24 on 2000, and on 2000 alone, as stopped:
"You can now unplug the device." (`tr_usb_after_kind`). Code 24 is run 41's
2000 bridge after `CM_Query_And_Remove_SubTreeW`; for a USB device after
`CM_Request_Device_EjectA` it is inferred. Host-tested only. Revision 15
read it: a Bulk-Only stick and a UAS disk after
`CM_Request_Device_EjectA` on 2000 read status `0x01806400`, Code 24,
and this reading now also records the stop that lets the tree and the
tray mark the device ("Offered", above).

**Rights.** On NT a removable device needs no administrator (Windows'
own applet runs as the user); a rights refusal is offered elevated on
Vista and 7 as section 6.4 says, with `/eject id=<instance id>`.

**Where it shows.**

- The window: the USB branch's label of an offered device ends "[can be
  stopped]"; its details say "Safely Remove: can be stopped here" or why
  not. Action > Safely Remove..., Delete and the right-click menu act on a
  USB port node whose device is offered, and on a node of the
  Thunderbolt branch that is that device's devnode; a USB device behind a
  dock's controller is stopped by itself, its dock only from the dock's
  unit.
- The tray's left-click menu has two groups, each under a grey heading
  and only when it has an entry: "USB devices" and "Thunderbolt and USB4
  devices", each item "Safely remove <name>" (grey with the reason when
  listed but not offered); "No device to stop" when both are empty.
- `/eject id=<instance id>` takes a USB device's instance ID as it takes
  a unit's; `/report` writes each device's "Safely Remove:" line.
- A one-shot answer written to a redirected output (`/eject`, an error
  of `/report`) is console text, and is wrapped at 79 columns.

### 6.6 A unit not marked removable (revision 11)

NT only. A unit whose capabilities say neither `Removable` nor
`SurpriseRemovalOK` (`inst_unit_not_marked`) can be stopped only by the
query-and-remove route of record 24 section 7.2, which needs an
administrator's rights. The owner's decision of 2026-10-16: the tool asks
for those rights for this stop only, when it lacks them.

1. **When.** `tr_elev_needed`: NT, the unit not marked removable, and
   this process no administrator - on Vista and 7 its token not elevated
   (`TokenElevation`), on 2000 and XP not a member of Administrators
   (`CheckTokenMembership`), both looked up at run time. An administrator
   stops it in place, as section 6.2 does.
2. **The question.** After the usual confirmation, a dialog with the unit
   and "Windows does not list this device for safe removal. Stopping it
   needs administrator rights.", and two buttons: "Stop as
   administrator..." (with the shield on Vista and 7) and Cancel.
3. **The elevated run.** `ShellExecuteEx` with the verb `runas` starts the
   same executable with `/eject id=<instance id> /result <name>
   /confirmed <digest>`: User Account Control on Vista and 7, the Run As
   dialog on 2000 and XP (read on both). It runs section 6.2 whole - the
   shared mutex, every re-check, the route and its vetoes - as a one-shot
   run, and never offers elevation itself.
4. **What the elevated run trusts.** Nothing it reads from a place a user
   can write. `/confirmed` is a digest of the devnode list the user
   confirmed (`tr_res_digest`: two 32-bit FNV-1a hashes over the count
   and the upper-cased instance IDs), on a command line nothing can change
   after the start; the run reads its own list and asks its own
   confirmation unless that list has the same digest, so the digest can
   spare a question and never add a devnode. Its answer goes into a named
   section the window created before the start (`Local\Xhci98TrAnswer-
   <process id>-<tick>`, refused if it exists already, writable by SYSTEM
   and the Administrators - who include another account under Run As);
   the run opens it by a name `tr_res_name_ok` checks, writes at most
   `TR_RES_BYTES` ("XHCITR ANSWER 1", the code, the text), and no file is
   written or read.
5. **The wait.** The window waits on the run's process for up to five
   minutes, answering its messages; another stop is refused meanwhile, and
   a quit (Exit, or an installer's) ends the wait at once and is posted
   again. Then `tr_elev_result`: the run's own text when its code is the
   process's exit code; a cancelled elevation (`ERROR_CANCELLED`) -
   "Nothing was stopped: administrator rights were not given. Try again
   and allow it, or unplug the device and plug it in again." (exit 1); a
   failure to start - the rights text of record 24 section 7.2 with the
   error; no end within the wait, or an end with no answer - the state
   not known, look before unplugging (exit 2). The answer is shown as any
   stop's, in a message box or the tray's balloon.
6. `/eject id=` started by a user with no administrator rights takes the
   same route for such a unit.

Since 2026-10-18 the mark itself survives a rebuild on Windows 2000 to 7
where the mark service of record 21 section 7.4, revision 3, runs (task
42-0.4; the code is 42-A.5): this section's route stays for a unit the
service has not reached (its restart vetoed, the service stopped, a value
not yet in effect), and the window shows the mark's state beside the
unit as that revision says. As built (42-A.5's tool part, 2026-10-19): on NT a unit's
details read "Safe-removal mark: listed by Windows", "mark written; takes
effect at the next start of the device", "mark written, but Windows does
not list it", "not marked", or "not read" with the reason, from its NHI's
marks answer (`IOCTL_XHCICM_MARKS`, read at each refresh with the query and
held to its epoch) and the unit's `LowerFilters`; the NHI's details add
"Mark service: running", "stopped", "not installed" or "not read"
(`QueryServiceStatus`, looked up at run time). After the tool's own
successful `CM_Request_Device_EjectA` of a unit, and only when it runs as
an administrator (an elevated run on Vista and 7), it waits up to 3 s for
the unit's entry with a departure pending and writes
`XhciCmEjectConfirm` (`CM_QC_*`, 28 bytes) to the NHI's driver key while
the value is absent or empty, under the shared eject mutex it already
holds, and says whether the eject was confirmed; the query-and-remove
fallback confirms nothing (record 21 section 7.4). The pure half
(`tr_mark_state`, `tr_mark_find`, `tr_msz_has`, `tr_mark_member`) is in
`test\test_tr.c`; the marks answer is parsed by `xhcimark\mark_core.c`,
which XHCITR links.

XHCIINST needs nothing new: it runs as an administrator (record 24 section
7.2).

## 7. The tray icon

### 7.1 Behaviour

- `XHCITR /tray` starts with only the icon; `XHCITR` with no argument
  opens the window and also keeps the icon. One tray per user session (a
  named mutex `Local\Xhci98TrTray` on 2000 and later, `Xhci98TrTray` on
  98 SE and ME); a second plain start asks the first to show its window
  (a registered window message carrying no request but "show") and exits.
  The one-shot modes `/eject` and `/report` never hand over: they run
  and exit with a status. No window message can start an eject.
- **Left click**: a menu "Safely remove <unit>" per unit (grayed with the
  reason when it cannot be stopped now), and since revision 7 per listed
  USB device, in two groups (section 6.5), a separator, "Open xHCI98 USB
  Tree". **Double click**: the window. **Right click**: Open, Refresh,
  About, Exit.
- **Tooltip**: "xHCI98 USB Tree: N Thunderbolt/USB4 devices" and, where
  the shell has no balloons, the last event (at most 63 characters).
- A hidden top-level window (class `Xhci98TrTray`) owns the icon, so the
  `TaskbarCreated` broadcast reaches it and the icon is re-added after an
  Explorer restart; `WM_CLOSE` to it ends the program cleanly.
- **Quit event** (for the installer, section 8): a manual-reset event
  `Global\Xhci98TrQuit` on NT, `Xhci98TrQuit` on 98 SE and ME, created
  only by XHCIINST, set, and closed when its run ends (so it exists only
  during an install or uninstall). The tray never creates it: once a
  second, with its query poll, it opens it with `SYNCHRONIZE` alone; when
  it exists and is set, the tray finishes an eject transaction in
  progress, removes the icon and exits. On NT XHCIINST gives it a security
  descriptor granting everyone `SYNCHRONIZE` and SYSTEM and
  Administrators full access. A local user who creates the name first
  could close other sessions' trays; that is the whole of the exposure,
  and XHCIINST's `WM_CLOSE` and its "run again" answer (section 8) still
  cover a tray that misses the event.

### 7.2 Events and keys

The tray polls the query of every NHI every 1 s (one IOCTL each, no
device traffic) and diffs the router entries (pure, `tr_core.c`). A
router's key is its NHI's epoch plus, for USB4 entries (kinds 2 and 3),
the route, and for ICM entries (kind 1, whose route is not known) the
link and depth, IDs and UUID suffix; two entries with one key have no
identity, and their events use generic wording ("a Thunderbolt device").
A new epoch is a new set of devices.

| Event | Text |
|---|---|
| a router entry appears | "<vendor> <model> connected (Thunderbolt 3 / USB4)." |
| an ICM device's approval becomes pending or unanswered | "<name> is waiting to be approved." |
| an ICM device's approval becomes failed | "<name> could not be approved." |
| a router entry leaves | "<name> was disconnected." |
| the liveness state leaves ACTIVE | "The Thunderbolt/USB4 controller stopped answering." |

The approval events read the ICM's approval states only: a USB4 entry's
approval word folds held, failed and unsupported together and an eject
sets failed (`cm_status.c`, `tbt_u4cm.c`), so USB4 entries announce
connection and disconnection alone.

These are descriptive. A 1 s sample cannot prove how a device left: the
ICM keeps a tunnel up after an eject until the unplug (record 24 section
5.2), the USB4 teardown is asynchronous and absent on 98 SE and ME and
without the child filter (record 21 section 7.4, record 24 section 5.2),
and another program can stop a unit and the user unplug it between two
samples. So the tray says "disconnected", never "unplugged while in use"
(decision T9). The outcome of an eject comes from the transaction
(section 6.2 step 7), not from this diff.

**Balloons.** Where `shell32.dll` is version 5.0 or later (2000, ME, XP
and later) the tray uses the version 2 `NOTIFYICONDATAA` size with
`NIF_INFO` balloons and `NIM_SETVERSION` 3; on 98 SE (4.72) the version 1
size and the tooltip. The structures are declared in `tr_core.h` for the
MSVC 6.0 build with size checks (88 and 488 bytes, ANSI, x86); the x64
build uses the WDK's header.

### 7.3 Command line

`XHCITR [/tray] [/eject id=<instance id>] [/setlevel xhci98=<n>
xhcicm=<n>] [/report <file>]`. `/report`
writes the Save Report text and exits (for scripts and bug reports).
Exit codes follow XHCIINST's (record 23 section 10). Since revision 7
`/eject id=` names a unit or a USB device (section 6.5). Since revision 9
the report carries each "Driver log" node's text (section 3.5). Since
revision 11 `/eject` also takes `/result <name>` and `/confirmed <digest>`, which
the tool passes to its own elevated run (section 6.6) and which nothing
else needs. Since revision 13 `/setlevel` (section 7.5) writes one or
both drivers' levels and exits; with `/result <name>` it is the dialog's
elevated run.

### 7.4 Start at login, per user (revision 10)

- The machine's start is the Run value of section 8, written by XHCIINST
  and removed by `INSTALL /notray`; that is machine-wide and needs an
  administrator, as before.
- Each user can turn it off without one: "Start at login", a checkable
  item in the View menu and in the icon's right-click menu, writes the
  DWORD `TrayAtLogin` under `HKCU\Software\xHCI98\XHCITR` - 0 off, 1 on.
  The item shows the user's choice.
- `XHCITR /tray`, the start the Run value makes, reads it first: a DWORD 0
  ends the run at once, with exit 0, before any window, icon or thread.
  An absent value, another type or any other number starts as before; a
  plain start (no `/tray`) always opens. The decision is
  `tr_tray_login_start`, host-tested.
- Without the machine's Run value nothing starts at login whatever the
  user chooses, so the item is grey and reads "Start at login (off for
  this computer)".
- 98 SE and ME read the same key, from the user's profile or the default
  user's.

Addendum to revision 10, the tree's labels (the owner, 2026-10-16, after
two controllers of one name each showed a "Root hub" on the ThinkPad P14s
Gen 1):

- A controller's label ends with its vendor and PCI id from its instance
  ID ("- Intel 8086:02ED"; "- vendor XXXX" for a vendor outside the
  tool's short table). A Thunderbolt chip's own xHCI reads "- Thunderbolt
  3 controller's USB (8086:15C1)" ("USB4" or "Thunderbolt" by its NHI's
  published connection manager): an NHI in the walk on the same bus and
  device (an integrated controller), or behind a bridge beside the
  controller's under one switch (a discrete one); never a guess, so a
  PCH xHCI on the root bus is not one.
- The root hub reads "Root hub - N ports (a USB 2.0, b USB 3.x)" from
  each port's `_EX_V2` protocols, without the split when a port did not
  report them; each port label carries its protocol ("[Port 15, USB 3]").
  No pairing of a connector's USB 2 and USB 3 ports is shown: the tool
  reads none.
- The same labels are in `/report` and Save Report, a file whose tree
  lines can be wider than 79 columns at depth, as before.

A second addendum to revision 10, resources (the owner, 2026-10-16):
every devnode's details - controllers, hubs, USB devices, an NHI, a
tunnel port, a unit with its bridges and functions - end with its PCI
location (bus:device.function, from `CM_DRP_BUSNUMBER` and `_ADDRESS` on
NT, from the instance ID on 98 SE and ME for a function on a root bus)
and a "Resources" list: what the configuration manager has allocated
(`CM_Get_First_Log_Conf` with `ALLOC_LOG_CONF`, then each descriptor's
data) - memory and I/O ranges, an IRQ (level or edge, shared or
exclusive; "message-signalled" for the negative number Vista and 7 give
an MSI), a DMA channel, a bus-number range. With a problem code it also
lists what the device asked for: the boot configuration, then the
filtered requirement list or the first basic one, so a device that wants
an IRQ and has none shows it. The routines are looked up at run time
from `cfgmgr32.dll` and need no rights; the descriptor layouts are
`cfgmgr32.h`'s packed ones, the same in the Windows 98 DDK and in WDK
7.1, except the x64 IRQ descriptor's 8-byte affinity. The decoding is
`tr_res_text`, host-tested. Physical ranges only; no kernel address is
shown. On 98 SE and ME the configuration manager arbitrates memory, I/O,
DMA and IRQ only, so no bus-number range is listed there; a type that only
a private arbitrator knows (67 on a PCI bridge, with no data) is listed by
its number as "Resource type 67 (no data)".

### 7.5 The drivers' logging levels (revision 13)

The owner's decision of 2026-10-16: the tool shows and sets the level of
each driver's log, in place of `XHCISNAP -verbosity` and `SNAPON.BAT`.

1. **Where.** "Logging level..." in the View menu, and at the end of the
   right-click menu of an `xhci98.sys` controller, of an NHI and of each
   "Driver log" node (section 3.5), which answers its parent's driver:
   `level_dialog(owner, driver)` is the one entry point, and
   `lv_driver_of_node` names a node's driver.
2. **What is read.** xhci98: the REG_DWORD `XhciLogVerbosity` in the
   driver (software) key of every devnode the walk finds with service
   `xhci98` and a `PCI\` instance ID - every controller, not a root hub
   or a hub. xhcicm: `XhciCmLogVerbosity` in every NHI's driver key
   (service `xhcicm`, `PCI\`), and no other name: `xhcicm.sys` has never
   been released, so no older value is there to honour (the owner,
   2026-10-16). No value reads as the default, 2; a DWORD outside 0 to 4 is
   shown as it is, "out of range"; a value of another type is shown as
   not a REG_DWORD. The decision is `tr_level_pick`, host-tested.
3. **The dialog.** A list of each device and its reading (the same block
   as the report's), then the two drivers side by side, each with five
   radio buttons, one line per level: 0 off; 1 counters only; 2 the log
   (default); 3 adds port status; 4 adds memory addresses (for
   developers). A driver's buttons start at the level every one of its
   devices reads, and none is checked when they differ or a reading is
   out of range. Set writes a driver's level to every one of its devices
   when the checked level is not what they all read, and nothing else.
4. **The write.** `REG_DWORD` 0 to 4 under the driver's one value name,
   read back. A driver key is written only when it has the configuration
   manager's form (`tr_driver_key_ok`), and a value out of range is never
   written: the command line refuses it before anything is read.
5. **Rights.** On 98 SE and ME, and for an administrator on NT (section
   6.6's test), the window writes directly. A standard user on 2000 to 7
   sees "Set as administrator..." (the shield on Vista and 7), and Set
   starts section 6.6's elevated run with `/setlevel xhci98=<n>
   xhcicm=<n> /result <name>` in place of `/eject`: the same `runas`, the
   same section for the answer, the same wait. The elevated run takes
   only its arguments - each `xhci98=<d>` or `xhcicm=<d>` exactly, one
   digit 0 to 4, each driver once (`tr_level_arg`, `tr_parse_args`) -
   walks the device tree itself, writes those values into the keys it
   found and nothing else, answers and exits. A cancelled elevation
   changes nothing (`tr_level_elev_result`).
6. **The restart.** Each driver reads its level only at start, so after a
   write the writer itself confirmed, the tool offers to restart Windows
   (`ExitWindowsEx`, with the shutdown right enabled on NT). A run that
   did not answer is not offered one.
7. **The report.** `/report` and Save Report carry a "Logging levels"
   block before the tree, and a controller's and an NHI's details carry
   its own line.

## 8. Install, upgrade and uninstall

- XHCIINST owns the tool's file (and its `.TMP`, `.BAK` and `.DEL`
  forms) and the Run value, outside the per-package `XHCIINST.LST` lists;
  its removal of the install directory (record 23 section 9.6) counts
  them as its own. Nothing is
  ever left pending for a restart on 98 SE or ME, and XHCIINST never
  edits `WININIT.INI`.
- **One at a time.** XHCIINST holds a machine-wide maintenance mutex
  (`Global\XhciinstMaint` on NT, `XhciinstMaint` on 98 SE and ME, with the
  shared mutex's security) for everything in this section. A second
  XHCIINST waits up to 60 s for it, then refuses; it reads every version
  again once it holds it.
- **Leftovers first.** A `.TMP` is deleted. A `.BAK` with no tool beside
  it is an interrupted replacement: it is renamed back, and if that fails
  the run stops before any version is compared ("steps left"). A `.BAK`
  beside a tool is deleted.
- **Copy.** On an install or upgrade run, the tool for the machine's
  architecture goes to `<install directory>\XHCITR.EXE` (`XHCITR64.EXE` on
  x64) when the file is missing or the package's file version is newer
  than the installed file's; otherwise it is left alone. The new file is
  first copied to `.TMP` and compared with the package's; then NT
  replaces the tool with `MoveFileEx` (replace existing), and 98 SE and ME
  rename the tool to `.BAK`, `.TMP` to the tool, and delete `.BAK`,
  renaming `.BAK` back on a failure. The Run value is written only after
  the file is in place. `/notray` writes no Run value and removes an
  existing one. There is no new question (owner: start at login).
- **Closing running copies.** Before replacing or deleting the file,
  XHCIINST sets the quit event (section 7.1), which every session's tray
  sees, and posts `WM_CLOSE` to the `Xhci98TrTray` window in its own
  session, then waits up to 15 s for the file to become replaceable. It
  never ends a process.
- **Still in use.** If the file still cannot be replaced, XHCIINST leaves
  the installed tool as it is, deletes its `.TMP`, says to close the
  xHCI98 USB Tree (or restart) and run it again, and ends with "steps
  left"; the drivers' part of the run is unaffected. No replacement is
  deferred to a restart, so no older copy can ever land on a newer one.
- **Uninstall.** Closes it as above, deletes the Run value and the file.
  On NT a file still in use is renamed to `.DEL` and deleted at the next
  restart (`MoveFileEx`, delay until reboot), and the next install or
  uninstall deletes a leftover `.DEL`. On 98 SE and ME a file still in use
  is left, reported, and deleted by the next uninstall; the Run value is
  gone, so it no longer starts. A failure to delete the Run value is
  reported and fails the run.
- The packager carries both executables under `tools\xhcitr\` with
  XHCIINST's freshness and version checks; `test-package.ps1` checks the
  layout.

## 9. Build, imports, manifest and icon

- x86: MSVC 6.0, `/SUBSYSTEM:WINDOWS,4.0`; x64: WDK 7.1,
  `/SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64`. ANSI C89 as XHCIINST.
- Static imports: KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32,
  COMDLG32 (all present on 98 SE); setupapi, cfgmgr32 and version.dll
  resolved at run time as XHCIINST does, and any API absent from 98 SE's
  system DLLs likewise. `xhcitr\check-imports.ps1` allows exactly the
  seven.
- Manifest: `asInvoker`, the Common Controls 6 dependency (XP and later
  draw themed controls; 98 SE to 2000 ignore the manifest).
- Icon `xhcitr.ico`, drawn for this project: a controller box with a
  tree line to two devices and an eject arrow, in the 16 VGA colours with
  no partial transparency, at 16, 32 and 48 pixels, 4 and 32 bits per
  pixel, so it reads the same on 98 SE's grey taskbar, XP's blue and 7's
  dark one.
- Version resource from `src\xhci_version.h`.

## 10. The pure half and its host tests

`tr_core.c` holds no Windows call: query and record parsing and checking
(every offset and count bounded by the bytes returned), the extension
decode, descriptor decoding to text (device, configuration, interface,
endpoint, SuperSpeed companion, BOS with USB 2.0 extension, SuperSpeed and
SuperSpeedPlus capabilities, strings), speed naming, router keys and the
tray event diff, the eject transaction's decisions, report formatting.
`test\test_tr.c` covers each with golden vectors, including truncated and
malformed descriptors, a query whose size disagrees with its counts, a
sizing answer, duplicate ICM keys and an epoch change. The driver's
builder (`cm_query.c`) is host-tested in the CM suite.

Revision 9: the log answer's builder (`cm_query.c`: the counter lines,
the ring's text with the cut, the withheld value and the replaced bytes,
the sizing answers) in the CM suite; the tool's readers of both logs
(`xhcitr\tr_log.c`, pure: the `XCML` parse, the snapshot window's header,
the ring by its head and used count, the HCD region's shape, the counter
names held to `src\xhci_counters.h`, the level-4 withholding, the
recording-off advice) in `test\test_trlog.c`.

Revision 12: the PCI answer's builder (`src\cm\cm_qpci.c`, over modelled
buses: a dock's chain, a loop, a bus outside the port's range, a chain
deeper than the bound, more functions and buses than the answer holds, a
refused read, an absent function, 98's "not read", the sizing answers)
and the tool's reader and decoders (`xhcitr\tr_pci.c`: the answer's bad
shapes, pin and line, MSI, MSI-X, the link, BARs, windows, the capability
walk with a clear Status bit, a loop, a self loop, a bad and an unaligned
pointer, a cut and a capability past the 256 bytes, 79 columns on a worst
case) in `test\test_trpci.c`; xhci98's query answer (`src\xhci_qry.c`:
what each level serves, the ring's text and cut, the configuration read's
states, the sizing) and the tool's reader and texts of it in
`test\test_xhq.c`, with the default level.

## 11. Legs

| Leg | Systems | What it reads |
|---|---|---|
| T.1 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64 | the tool starts as a normal user (no UAC prompt), opens `\\.\HCD<n>` and `\\.\XHCICM<n>` with no rights, the window shows the USB tree with speeds and descriptors |
| T.2 | the same, with the TB3 and USB4 emulator devices | the Thunderbolt branch, the NVMe unit behind its tunnel, link details |
| T.3 | 98 SE, 2000, XP, 7 x64 | Safely Remove from the window and from the tray; the answer text; the unit gone; re-plug |
| T.4 | 2000, ME, XP, 7 | balloons on plug, approval, eject, disconnection; 98 SE tooltip |
| T.5 | 98 SE, 7 x64 | install puts the tool and the Run value; login starts the tray; upgrade with the tray running; uninstall closes it and removes both |
| T.6 | every target | the record 24 legs of XHCIINST still pass with the changed `xhcicm.sys` |
| T.7 | 98 SE, ME, XP, 7 x86 | revision 7, as a standard user: a Bulk-Only stick at a USB 3 and a USB 2 port and a UAS disk (98 SE, XP, 7) - the label with its letter, Safely Remove from the window, a right click, Delete and the tray, the result, the device gone or held, unplug and re-plug with the drive back, a refusal with a file open on it; a tablet, a keyboard, a hub and a device with no storage never offered |
| T.8 | 98 SE, 2000, 7 x86 (as a standard user) | revision 9: `XhciCmLogVerbosity` 2 on xhcicm's NHI and `SNAPON.BAT` for xhci98, restart; both "Driver log" nodes show the counters and the log live, after a plug and after an eject; Save Report holds both with no address; with recording off, the advice |
| T.9 | 98 SE (TB3 enclosure), 7 x86 as a standard user; xhci98's nodes on 98 SE, ME, 2000, XP, 7 x86 and 7 x64 | revision 12: the NHI's node shows the NHI and the tunnel port with pin and line and capabilities; below the port, the enclosure's functions on 7 and "not read on this system" on 98 SE; xhci98's PCI node and Driver log node read through the query request at the new default level 2; `/report` holds them; with the HCD regression (enumeration, a mass-storage copy, an unplug and re-plug) on each of the six |
| T.10 | 98 SE, 7 x86 | revision 13, 7 x86 as a standard user: each driver's level set to 3 (on 7 through "Set as administrator..."), the restart, the registry and the dialog read 3 for each; then set back to 2 |

## 12. Decisions

- **T1** (owner) A separate GUI tool, XHCITR / XHCITR64, not XHCIINST;
  3.0.0.0 waits for it; no admin at start; tray with eject menu, balloons
  and start at login; everything shown with check boxes; descriptors;
  driver changes allowed in both drivers.
- **T2** The tool reads the Thunderbolt state from a new query device in
  `xhcicm.sys` (section 4), not the registry record, because the ask needs
  an administrator on NT and the record lacks the details.
- **T3** `xhci98.sys` is not changed. Its existing hub IOCTLs (record 13
  section 8) give the USB branch everything but the negotiated SuperSpeed
  lane rate and count; the BOS gives what a device supports, and the tool
  labels it so. A change to the HCD this close to the cut would reopen its
  whole regression for that one detail. **Revised by revision 12** (the
  owner's PCI request and logging-consistency decision, 2026-10-16): the
  controller's configuration space is in nothing the door serves and no
  user-mode route reads it, and the owner chose to read `xhci98.sys` as
  `xhcicm.sys` is read, so `xhci98.sys` gains one read-only query request
  (section 4.8) serving its log, its counters and its configuration space,
  with the USBUSER pass-through unchanged; its default logging level moves
  to 2 in the same revision. The HCD's regression is run again for both.
  The negotiated lane rate and count stay "not reported".
- **T4** No new install question; `/notray` opts out of the Run value.
- **T5** The tool links `inst_core.c` for record 24's rules and texts, and
  shares XHCIINST's mutex, with a security descriptor both can open.
- **T6** (owner, 2026-10-16, revision 7; it reversed the first T6, "plain
  USB devices get no eject in the tool") Plain USB devices are stopped
  too, under section 6.5: one with a disk (or, on NT, a volume) in what
  stops with it; never a hub, an input device, a radio or the controller;
  on any controller the tool lists. The owner allowed "removable" on NT
  as well; it is not used, because `xhci98.sys` reports every device
  removable, internal ones too (section 6.5, "Why not removable").
- **T7** An eject waits for a connection-manager snapshot built after it
  began (two publications past its first query, section 6.2), in place of
  record 24's ask and echo, and checks eligibility on every answer up to
  the call; the pass's age only marks the display current or not.
- **T8** The tray's balloons from polling are descriptive; only an eject
  transaction reports an eject's outcome (section 7.2).
- **T9** No "unplugged while in use" balloon in this release, although
  the owner asked for a notice on an unsafe unplug: polling cannot prove
  one (section 7.2). The authoritative source is the child filter, which
  sees a unit's surprise removal on NT; publishing a count of those in the
  query is planned work for the owner to open.
- **T10** A USB device's eject is section 6.5's short transaction - the
  shared mutex, the device found again by instance ID and its eligibility
  read again just before the call - not section 6.2's: no query device
  stands behind a USB device, and the configuration manager's own walk is
  the current state.
- **T11** The call goes to the USB device's devnode, the one Windows' own
  applet stops, never to a disk or a function below it, and on NT without
  record 24's not-removable fallback: a refusal is shown as it is.

- **T12** (owner, 2026-10-16, revision 11) A unit not marked removable is
  stopped by the query-and-remove route, and the tool asks for the
  administrator rights it needs for that one stop, by `runas`, waiting
  for the elevated run's answer (section 6.6). The elevated run trusts
  nothing a user can write: the confirmed list travels as a digest on its
  command line, its answer in a section the window created.
- **T13** (owner, revision 9) The tool shows both drivers' logs and
  counters, in the window and the report, from a release install, with
  no address in them and without writing the registry.
- **T14** `xhcicm.sys`'s log is a second request on the query device
  (section 4.5), copied from the ring under its own lock at the request,
  not drained and not snapshotted by the thread: a snapshot per pass would
  copy 16 KiB each pass whether anyone reads it or not, and a drain would
  take the log from every other reader. The counters, which the thread
  owns, are the thread's per pass.
- **T15** `xhci98.sys`'s log is read through the existing door, as
  `XHCISNAP` reads it (section 4.6); `xhci98.sys` stays unchanged
  (decision T3). At level 4 the tool withholds the ring rather than pick
  the address records out of it by label: the level-4 guarantee is the
  driver's, and a label list in the tool would drift from it.
  **Superseded by revision 12** (decision T3 revised): the tool reads
  `xhci98.sys` through its query request first (section 4.8), whose
  driver withholds the level-4 ring itself, and through the door only
  for a driver that refuses the request.

- **T16** (owner, revision 12) A "PCI configuration" view, read-only,
  through this package's own drivers: the NHI, its tunnel ports and the
  functions below them through `xhcicm.sys`'s query device, each xhci98
  controller through its own query request (section 4.8); the BARs raw,
  never sized.
- **T17** The PCI answer is read at the request, not snapshotted by the
  thread (as decision T14 chose for the log): a pass would read every
  function's 256 bytes whether anyone asked or not. It is configuration
  space as read, decoded by the tool, so the driver carries no decoder
  that could drift from the tool's.
- **T18** (owner) `xhci98.sys`'s query request serves the configuration
  space at every level and is not gated by the door's enable step; what it
  serves of the log follows the level as `xhcicm.sys`'s does. Level 4's
  ring is withheld by the driver, as the tool already withheld it.
- **T19** (owner, 2026-10-16) Both drivers' logging level defaults to 2:
  the ring records, with no addresses (`XhciLogVerbosity` for xhci98.sys,
  `XhciCmLogVerbosity` for xhcicm.sys). The owner's P14s reading put the
  cost at about 1 to 2% for large transfers and up to about 5% for small
  ones (design record 08 section 13.2, amended).

- **T20** (owner, 2026-10-16, revision 13) The tool sets the two
  drivers' logging levels and nothing else (section 7.5). A standard user
  on NT does it through a second mode of decision T12's elevated run,
  whose arguments are checked whole before anything is read; the
  elevated run finds the keys itself. (T13 to T19 are revisions 9 and
  12's.)

- **T21** (owner, 2026-10-18, roadmap-usb4 42-0.3; revision 14)
  XHCISNAP is retired in `3.1.0.0` and its reading moves into XHCITR
  (section 15): its engine is linked into the tool, its `-report`,
  `-dump` and `-probe` become `/snapshot`, `/dump` and `/probe`, and Save
  Report carries the snapshot, so one file is the bug report. Its level
  switches do not move: section 7.5 already replaces them.

## 13. Open

- Revision 7: whether a USB disk on 98 SE (NUSB's `usbstor.sys` and
  `USBNTMAP`, or `xhciuas.sys`) and on ME (its own `USBSTOR`) is a devnode
  below the USB device in the configuration manager's tree, as an
  Stor98 disk is below its controller; whether a query-remove of the USB
  device is refused while a file is open on its disk; and what each NT
  target leaves of a USB device after its eject (Code 47, or gone). T.7
  read them (section 14).
- Whether every target lets an unelevated user open `\\.\HCD<n>` and
  `\\.\XHCICM<n>` with no access rights (record 13 section 8.10 left this
  open for the hub names); T.1 reads it.
- 98 SE's and ME's `\\.\` resolution of `\DosDevices\XHCICM<n>` (xhci98's
  `HCD<n>` names resolve there, record 13 section 8.2), and what NTKERN
  returns to `DeviceIoControl` for the removed-device answer.
- Whether the ICM's firmware messages carry a link rate the client could
  keep; until read, "not reported by the firmware".

## 14. As built

Where the code of `3.0.0.0` differs from the sections above, or settles
what they left open:

- **The ICM's link rate.** The ICM's device-connected event carries the
  link's speed and width (`tbt_icm.h`), so a Thunderbolt 3 router's
  extension has a rate and width with `CM_Q_XF_RATE_READ` clear, shown
  "reported by the firmware"; section 13's question is answered.
- **The name value at stop** is written empty, not deleted: there is no
  evidence that `ZwDeleteValueKey` works on Windows 98 (record 13
  Appendix A). The tool reads an empty `XhciCmQueryName` as no device.
- **The NHI's instance ID** comes from the link name of a private device
  interface registered on the NHI's PDO (never enabled), because
  Windows 2000's PCI bus answers `BusQueryInstanceID` with the last
  element only; `IRP_MN_QUERY_ID` is the fallback.
- **An empty file name on create** is required on NT only; what NTKERN
  leaves in the file name for a `\\.\` open is unread.
- **An inert start's first answer** carries `CM_Q_F_INERT` (task
  42-A.7). The answer the device holds until the thread's first pass is
  seeded from the thread's actual wait, which the thread records before
  each wait and clears after it (`CM_NHI.ThreadWait`,
  `CmQuerySeedInert`): no thread, or a thread already in its wait with no
  timeout, is inert; one in a timed wait is not; a thread between waits is
  judged by the wait it goes to, as its own publication judges it
  (`CmThrInertNext`). Until 42-A.7 the seed was set only with no thread,
  so the moment between the device's creation and that pass read as not
  inert; an eject refused it all the same (the record's stage is inert).
- **The confirmation** is a message box listing the unit and every
  devnode that will stop (at most 64; a larger unit is refused), not a
  dialog of its own.
- **Auto Refresh** rebuilds the whole tree when the 1 s poll sees a
  change. Without it, the window keeps the model its tree was built from
  and the status bar says the state changed; the details and an eject
  request use only that model.
- **Opening on 98 SE and ME.** A device whose open with no access rights
  is refused is opened for read and write, as XHCISNAP does; the IOCTLs
  stay any-access.
- **An eject's outcome on 98 SE** goes to a message box from the tray,
  there being no balloon; elsewhere an outcome longer than a balloon
  holds also goes to a message box.
- **`/selftest`** exists for `selftest.cmd`; with output redirected the
  one-shot modes write to it instead of a message box.
- **Install files.** XHCIINST stages through `.TMP` and, on 98 SE and ME,
  replaces through `.BAK`; on x64 the forms are named after
  `XHCITR64.EXE`. Record 23 section 9.5 has the details.

Revision 7, from leg T.7 (`runs/run-41.md`, "Plain USB devices"):

- **The disks on 98 SE and ME** are below the USB device, two devnodes
  down ("USB Mass Storage Device", "USB Disk", the disk), for NUSB's
  `usbstor.sys`, for `xhciuas.sys` and for ME's own `USBSTOR`; the removal
  of the subtree removes them, and Device Manager no longer lists the
  device.
- **The refusal on 98 SE** comes for a file held open on the drive. An
  MS-DOS Prompt whose current directory is the drive does not refuse, so
  the 9x text names an open file first.
- **Windows XP answers a vetoed eject with success.**
  `CM_Request_Device_EjectA` returned `CR_SUCCESS` with the veto type set;
  `tr_eject_cr` turns a success with a veto type into `CR_REMOVE_VETOED`,
  on the USB path and the Thunderbolt unit path alike. The veto came from
  the stick's own volume (`PNP_VetoDevice`, a `STORAGE\RemovableMedia`
  devnode); a device or driver veto from a devnode of the device's own set
  reads "F: is in use (a program has a file or folder open on it)".
- **Code 47 after an eject** (XP and 7; Code 24 on 2000 after a stop made
  here, revision 15): the disks go with the eject, so a
  device held for eject is listed, "[stopped - unplug it]", before the
  disk rule is applied. Any other problem code reads "not started -
  problem code N", never "unplug it", for a unit too.
- **Windows 7** answers `CR_NO_SUCH_VALUE` for a devnode with no relations
  of the kind; that is none. A set that cannot be read says which relation
  failed, in the details and `/report`.
- **Property reads.** Every configuration-manager property is read into a
  scratch buffer larger than any field and copied when it fits; the
  registry-only drive-letter value is no longer asked of the configuration
  manager. A page fault on ME, after the setup class moved from the stack
  into the node, led to both.
- **The walk's completeness** is kept per node: a node whose children could
  not all be read makes any set holding it unread, and a failed setup-class
  read is a node not read.
- **The UAS disk's re-plug** was not read: the emulated UAS device does not
  attach when added to a running machine.

Windows 2000, from the `3.1.0.0` matrix (`runs/run-42.md`, "XHCITR on
Windows 2000: the disk names, the letters and the stopped mark"):

- **A USB disk's setup class** is in its Enum key as `ClassGUID` alone,
  with no `Class` value, so the class read empty and the disk was no
  disk: the device was offered through its volume, under its own name,
  with no letter. An empty class name is now taken from the class key
  the `ClassGUID` names.
- **2000 has no Code 47.** A USB device stopped here stays attached at
  Code 24 (status `0x01806400`), which is also 2000's failed start. A
  devnode reads as held for eject at Code 24 only when this process
  stopped it, until it starts again or leaves the tree; a stop made
  elsewhere still reads "not started - problem code 24".

## 15. XHCISNAP's reading, moved here (revision 14)

The owner's 42-0.3 decision of 2026-10-18 (decision T21): `3.1.0.0`
ships no `XHCISNAP.EXE`, `SNAPON.BAT`, `SNAPGET.BAT` or `SNAPOFF.BAT`,
and `XHCITR.EXE` and `XHCITR64.EXE` take over exactly XHCISNAP's
reading. `docs\contributing\snapshot-reader.md` (XHCISNAP's guide,
moved) stays the reference for the route and the files.

1. **The source.** `xhcisnap\xhcisnap.c` is `xhcitr\tr_snap.c`, its
   `main` renamed `tr_snap_main` and its command line still spelled with
   dashes inside; `xhcitr\xhcitr.c` maps the user's words onto it.
   `xhcisnap\selftest.cmd` is `xhcitr\snaptest.cmd`, and
   `xhcisnap\check-counter-names.ps1` is
   `scripts\check-counter-names.ps1`. The `xhcisnap\` directory, its
   `build.cmd` and its three batch files are gone.
2. **`XHCITR /snapshot [NAME] [/c N] [/nosysinfo]`** is XHCISNAP's
   `-report`, the field report. It writes `BASE.DEV` first - every PCI
   xHCI controller's and this package's devnodes with their Device
   Manager status, problem code and allocated resources, the installed
   driver files' size, version and SHA-256, and the "captured on" system
   block - then the dump: `BASE.TXT` (the decoded header, note ring,
   slots region, root port enumeration and HCD region, PORTSC at level
   3), `BASE.BIN` (the raw extension image) and `BASE.PSC` (the raw
   PORTSC array). The files go in the current directory under the 8.3
   NAME given, else a name from the clock (`mDDhhmmS`); a set is never
   overwritten. The exit codes are XHCISNAP's, 0 and 2 to 8.
3. **`XHCITR /dump [/c N] [/o BASE] [/nosysinfo]`** is `-dump`:
   `BASE.TXT`, `.BIN` and `.PSC`, replacing an earlier set of that BASE.
4. **`XHCITR /probe`** is `-probe`: the route's four controls, nothing
   dumped.
5. **`XHCITR /snapshot /?`** is the long help. With its output
   redirected each of these modes writes its text there; otherwise a
   message box shows the text with the current directory.
6. **What is refused.** A word beginning with `-` after `/snapshot`,
   `/dump` or `/probe` is refused before anything is read, so no switch
   of the old tool is reached by its old spelling. XHCISNAP's level
   switches (`-verbosity`, `-on`, `-disable`, `-force`) did not move:
   section 7.5's Logging level and `/setlevel xhci98=<0-4>
   xhcicm=<0-4>` replace them and SNAPON.BAT and SNAPOFF.BAT, and the
   engine's answer to one names Logging level and `/setlevel`.
7. **`/snaptest`** is `snaptest.cmd`'s entry, not for users: it hands the
   rest of the command line to the engine unmapped, so the self-test
   reaches the canned decodes (`-selftest-*`) as XHCISNAP's did. The
   gated build runs `xhcitr\snaptest.cmd` after `xhcitr\build.cmd`, and
   `scripts\check-counter-names.ps1` checks the counter names that
   `XHCITR /snaptest -selftest-counters` prints against
   `src\xhci_counters.h`, as it checked XHCISNAP's.
8. **File > Save Snapshot...** picks a folder (`SHBrowseForFolder`),
   runs `/snapshot` there for the first xhci98 controller and shows the
   text, with the folder, in a message box.
9. **Save Report and `/report <file>`** end with a "Snapshot" part,
   always with the `.DEV`, once (it is the machine's): from the run for
   the first xhci98 controller, or, when none is listed, from a run for
   controller 0, which may take no dump, the `.DEV` being written before
   any controller is opened and reporting that case. Each xhci98
   controller's dump `.TXT` follows, whole, slots and enumeration regions
   included; a dump whose header says level 4, where it can hold memory
   addresses, or says no level at all is withheld, as the "Driver log"
   node withholds the ring (section 3.5). Save Report, or
   `/report`, alone is therefore the bug report; the raw `.BIN` and
   `.PSC` come only from `/snapshot` or `/dump`, when the maintainer
   asks for them.
10. **A child process.** The window and `/report` do not call the engine
    in their own process: they run the tool itself with `/snapshot` in a
    folder - for Save Snapshot the one picked, for `/report` a directory
    of its own under `%TEMP%`, removed afterwards with its files - and
    read the files and the text back, waiting up to two minutes. The
    moved engine keeps XHCISNAP's process-wide state, so it runs once per
    process, as it did in XHCISNAP.
11. **The door.** `xhci98.sys`'s USBUSER pass-through is kept unchanged
    (section 4.8): Windows' own property tabs use it, and the snapshot
    reads through it.
