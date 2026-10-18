# Design Record 24 - The Thunderbolt and USB4 Device Viewer and Eject

Status: revision 4, 2026-10-15, reviewed and revised the same day; the code is written against it. Written for the owner's instructions of 2026-10-15: Windows 98 SE and
ME have no way to safely remove a device behind a Thunderbolt or USB4
tunnel, and no target system has a way to see what is connected to one.
The answer is a viewer of the connected Thunderbolt 3 and USB4 devices,
with an eject, in the install tool of design record 23, on every target
this package supports. It ships in `3.0.0.0` with Phases 39 and 40
(`roadmap-usb4.md`). Section 13 lists the decisions taken in writing it,
each an orchestrator decision for the owner to confirm unless it names the
owner; section 14 what is open.

Revision 5, 2026-10-16 (the owner, for `3.0.0.0`; roadmap-usb4.md 42-0.4
and 42-A.5, formerly EJRB.1, keep the proper fix): a unit Windows does not mark removable, whatever the
cause - the first plug of a device Windows has not seen, read on 2000
with a bridge chain behind the tunnel port, or a stack rebuilt since - is
stopped by `CM_Query_And_Remove_SubTreeW` on 2000, XP, Vista and 7, as
section 7.2 now says. Windows 2000's `A` form of that routine is a stub
that answers `CR_CALL_NOT_IMPLEMENTED` (`legal-provenance.md` section
4), which is why the earlier revision found no way to stop such a unit
there. A rights refusal gets its own text; XP's answer of `CR_SUCCESS`
with a veto type to `CM_Request_Device_EjectA` is read as the veto it
is; `/devices` labels the unit "not marked removable" (section 6). The
legs are `runs/run-41.md`'s, "The fallback for a unit not marked
removable".

It is not a Thunderbolt control panel: it approves nothing, changes no
security level, updates no firmware, and publishes or shows no secret or
authentication material.

## 1. What it is for

### 1.1 The problem

- **Windows 98 SE and ME have no eject for a tunnelled device.** `xhcicm.sys`
  marks the devices behind an active tunnel port `DN_REMOVABLE` on these
  systems (record 21 section 7.4), and nothing offers an eject for them:
  98 SE has no general eject applet (`runs/run-39.md`, 39-A.8), and the
  "Unplug or Eject Hardware" applet NUSB installs lists only devices whose
  parent carries NUSB's private interface (section 1.3). The user's only
  route today is to close the device's files and unplug it.
- **On Windows 2000 to 7 the applet can miss a device.** Safely Remove
  Hardware (Unplug or Eject Hardware on 2000) lists a device whose
  capabilities say `Removable` (and not `SurpriseRemovalOK`), which on a
  tunnel port is the child filter's answer (record 21 section 7.4). A
  device tunnelled before the driver bound, or whose stack was rebuilt
  without the child filter above its PDO, is not removable in Windows'
  eyes and is missing from the applet until it is re-plugged.
- **No target shows what is connected.** These systems have no
  Thunderbolt control panel; what a device is, whether it was approved,
  which tunnels it has and which Windows devices sit behind them is known
  only to `xhcicm.sys` and, piecemeal, to Device Manager.

### 1.2 Scope

Two modes of `XHCIINST.EXE` and `XHCIIN64.EXE` (section 2), on Windows 98
SE, ME, 2000, XP, XP x64, Vista and 7:

- **`/devices`**: each Thunderbolt or USB4 host controller this package
  drives, each device router connected to it with its names, generation,
  link, approval state and tunnels as far as the driver knows them, and
  the PCI functions and Windows devices behind each tunnel port with their
  drivers and problem codes, each ejectable one numbered (section 6).
- **`/eject`**: the safe removal of one listed device (section 7), by the
  route each system documents: on 98 SE and ME the configuration manager's
  query-remove then remove; on 2000 and later the eject request Safely
  Remove Hardware itself makes.

The router details exist only inside the driver, so `xhcicm.sys` publishes
a status record the tool reads (section 3). Without a record the tool
still lists what it sees, and ejects nothing (section 4.3).

### 1.3 Why not NUSB's applet (owner, 2026-10-15)

NUSB's "Unplug or Eject Hardware" on 98 SE lists a device only when its
parent devnode carries NUSB's own private interface. Using it would mean
marking the tunnel port's parent with that interface: a bridge devnode
shared with the host's other functions answers the applet's query for
every child, 98 SE gives no way to take such a registration back once
made, and the applet's behaviour would follow whichever NUSB version is
installed. The owner decided on 2026-10-15 not to borrow that interface
and to give these systems an eject of this project's own.

## 2. Where it lives (decision E1)

The viewer and the eject are modes of the install tool, not a third
executable. The reasons:

- The tool already starts on every target from one 32-bit image (and one
  x64 image), looks up every configuration-manager routine at run time,
  walks the 9x devnode tree with the registry as the authority for its ids,
  and is built, import-gated, staged and documented (record 23 sections 3,
  6.3 and 11). The viewer needs exactly that machinery; a separate tool
  would duplicate it, its build, its gate and its packaging rows.
- It is already in every package at `tools\xhciinst\`, where the release
  notes and README send the user.

The costs, taken knowingly: on Vista and 7 the manifest asks for elevation
on every run, so the viewer too shows the elevation prompt; and on 64-bit
Windows the user runs `XHCIIN64.EXE` (or `INSTALL.BAT`, which picks it), as
for the install. `INSTALL /devices` and `INSTALL /eject` work from the
package's top directory; the modes read nothing of the package (section
8.1), so they also run from the local copy under `%windir%\XHCI98\` on 98
and ME (record 23 section 4.4).

A separate tool would be the better choice if the viewer grew a resident
part (a tray icon, a notice on plug) or had to run unelevated on Vista and
7. Neither is in scope.

## 3. The status record (driver side)

### 3.1 What is published

One record per NHI `xhcicm.sys` drives, a `REG_BINARY` value. Its layout is
`src\cm\cm_status.h`, shared byte for byte by the driver and the tool (the
header uses no DDK or Windows type), every field a little-endian 32-bit
word or a byte string at a fixed offset; readers take bytes, never a
structure overlay. A layout change is a new version, and a reader refuses a
version it does not know.

**The header** (128 bytes):

| Offset | Field | Unknown |
|---|---|---|
| 0 | magic `0x534D4358` ("XCMS") | - |
| 4 | layout version, 1 | - |
| 8 | the record's size: 128 + count x 128 | - |
| 12 | generation, bumped on every content change; 0 only in a start's first record (stage stopped, below), never 0 after it | - |
| 16 | the system's uptime at the write, seconds | - |
| 20 | the NHI's PCI vendor (15:0) and device (31:16) | - |
| 24 | the connection manager running: 0 none, 1 the ICM client, 2 the USB4 software connection manager | - |
| 28 | the driver's profile (`CM_PROFILE_*`) | - |
| 32 | the stage: 0 stopped, 1 inert, 2 active, 3 failed after activation | - |
| 36 | the inert reason (`CM_INERT_*`) | - |
| 40 | the ICM's security level | `0xFFFFFFFF` |
| 44 | the ICM's NVM version (Titan Ridge and later) or ROM version | `0xFFFFFFFF` |
| 48 | the tunnel ports in the domain's set, 0 to 4 | - |
| 52 | bit n: port n is an active member (record 21 section 6.3) | - |
| 56 | four words: each port's bus (15:8), device (7:3), function (2:0) | `0xFFFFFFFF` |
| 72 | the router entries that follow, 0 to 24 | - |
| 76 | the entry size, 128 | - |
| 80 | flags: bit 0, more routers than entries | - |
| 84 | four 8-byte paths: each port's Windows 98 instance path, a count (0 for unknown, at most 7) then its elements, leaf first (section 4.3) | count 0 |
| 116 | the system time at the write, as `KeQuerySystemTime` returned it, low word then high word at 120 (100 ns units since 1601; the driver does no arithmetic on it) | - |
| 124 | the echo: the last `XhciCmStatusAsk` the driver read (section 3.3) | 0 |

**A router entry** (128 bytes):

| Offset | Field | ICM device | USB4 router at depth 1 | Deeper USB4 router |
|---|---|---|---|---|
| 0 | kind: 1, 2 or 3 | 1 | 2 | 3 |
| 4 | port or ICM link (7:0), depth (15:8) | link, depth | port, 1 | port, unknown depth |
| 8 | route string, low and high words | where the event carried one, else unknown | from the router | the child's route |
| 16 | approval: 1 approved, 2 approved before the system started, 4 refused by the firmware, 5 never answered, 6 not approved (policy, or not yet), 7 held, failed or unsupported (USB4) | from the table entry | from the port state | 1, or 6 while its removal is pending |
| 20 | USB4 port state (7:0) and reason (15:8), record 22 section 7.2 | 0 | yes | 0 |
| 24 | ids | the Thunderbolt vendor and model ids where the DROM's Intel entry carried them | the router's PCI-style vendor and device | unknown |
| 28 | generation: 1 Thunderbolt 3, 2 USB4 (version in 15:8) | 1 | from the router | from the link record |
| 32 | lanes: 0 unknown, 1 single, 2 bonded | 0 | where configured or verified | 0 |
| 36 | tunnels, 4 bits each: PCIe (3:0), USB3 (7:4), DisplayPort (11:8); 0 none, 1 up, 2 found at start and kept, 3 failed, 4 torn down after an eject, 5 unknown | PCIe up while approved, else none; USB3 none on the Thunderbolt 3 rows (Falcon, Alpine and Titan Ridge: a Thunderbolt 3 device carries USB inside its PCIe tunnel, through its own controller), unknown on the integrated rows; DisplayPort unknown | from the port | all unknown |
| 40 | the tunnel port its PCIe tunnel reaches (bus/device/function) | the domain's single active member when it has exactly one, else unknown | the port-to-bridge correspondence (`XHCI_TBT_BINDS`, record 22 section 7.6), else unknown | unknown |
| 44 | the UUID's last 16 bits | yes | where read | where read |
| 48 | reserved, 0 | | | |
| 64 | vendor name, 32 bytes | where the DROM carried one | from the router's DROM | empty |
| 96 | model name, 32 bytes | the same | the same | empty |

Names are cut to 31 characters, a byte outside printable ASCII written as
`?`, NUL-padded. A device the ICM rejected is not in the client's table
(`tbt_icm.c` counts it and drops it), so the record cannot list it:
value 3 (`CM_ST_APPR_REJECTED`) is defined, and XHCITR decodes it, but
no path of the driver writes it. The link speed is kept by neither connection
manager and is not in this layout.

**What is never published**: the ICM's `Key`, any challenge, response or
other authentication field, and all but the last 16 bits of a UUID. The
16-bit suffix is a display hint to tell two identical devices apart at a
glance; it can collide, and nothing selects or authorises by it.

### 3.2 Where, and when

- **Where, on every target**: the value `XhciCmStatus` of the NHI's
  hardware key - its `Device Parameters` key on NT, `HKLM\Enum\<instance>`
  on 98 SE and ME - through `IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DEVICE)`
  and `ZwSetValueKey`, the pair `xhci98.sys`'s door and `xhciuas.sys`
  already use on every target. **Not a volatile key** (decision E2): on NT
  that would take `ZwCreateKey`, which would be the first import of
  `xhcicm.sys` that `xhci98.sys` does not take (the allowlist's rule), with
  only `ntkern.vxd`'s export name table as 98 SE evidence and none for ME,
  and an import that does not resolve stops the whole driver from loading
  on that system. The value therefore survives a restart on every target,
  and the stamp (section 3.3) is what tells this boot's record from an
  earlier one's.
- **The keys are opened in the start**, in the PnP start's own dispatch,
  right after `CmReadValues` opens the driver key there (`cm_nhi.c`'s
  `cmNhiStart`, one call, `CmStatusOpen`): the hardware key for writing
  and the driver key for reading the ask. That is the one place this
  driver already opens a key on 98 SE and ME, in the thread the
  configuration manager's start runs in. On 98 SE and ME
  `IoOpenDeviceRegistryKey` is a configuration-manager service, and one
  called from the connection-manager thread could wait for as long as
  the configuration manager is busy (an Add New Hardware wizard left open
  holds it, `cm_cfgwk.c`'s reading), stalling the ring; with the handles
  opened in the start, the thread only writes and reads values on them,
  which are registry services alone. The same dispatch writes a first
  record at once (stage stopped, no routers, generation 0), so a record
  of an earlier start is replaced before anything can read it as
  current. The handles are closed after the stop's last write.
- **When**: on the connection-manager thread at `PASSIVE_LEVEL`, once per
  pass, after the actions and the re-enumeration service (`cm_thr.c`): the
  ask is read (at most every 500 ms), the record is built, compared with
  the last one written (without the generation and the stamp) and written
  when it differs or when the last write failed (each failure counted as
  `cm.status.fail` and tried again at the next pass); and once after the
  stop's work (stage stopped, no routers). An inert instance's thread
  waits with no timeout, so its only pass is its start's: its record says
  inert, and it never answers an ask, which the viewer shows (section
  3.3).
- **The 98 path** of each port (offset 84) is computed by the driver from
  configuration space through the re-enumeration's own reader, by the walk
  `cm_reenum.c`'s `cmPciPath` makes, in a pure function of `cm_status.c`
  over that reader (host-tested with a fake bus), on 98 SE and ME only:
  once for each member location, and again at most every 5 s while it is
  unknown, so a transient read failure does not leave it unknown.
- **The files**: `src\cm\cm_status.c` (with `cm_statb.h`) builds the
  record from `TBT_ICM` or `TBT_U4CM`, the domain's set and the binds,
  pure and DDK-free, host-tested by `test\test_cm_status.c`;
  `src\cm\cm_statos.c` opens, writes and closes. The hooks are two calls
  in `cm_nhi.c` (`CmStatusOpen` in the start, `CmStatusClose` in the
  removal), two in `cm_thr.c` (each pass, and after the stop's work) and
  one field in `CM_NHI` (`cm.h`);
  nothing in `tbt_icm.c`, `cm_reenum.c` or `cm_cfgwk.c` changes. The
  import allowlists gain `ZwSetValueKey`, `RtlInitUnicodeString` and
  `KeQuerySystemTime` rows (each a pair `xhci98.sys` imports, with its
  evidence) with `SITES=cm_statos.obj`, and `cm_statos.obj` joins the
  `SITES` of `IoOpenDeviceRegistryKey`, `RtlQueryRegistryValues` and
  `ZwClose`.

### 3.3 How the tool reads it, and what it trusts it for

- The record is read only for an NHI devnode that is present, started and
  bound to `xhcicm` (`NTMPDriver` on 9x, the `Service` on NT), and is
  refused when its magic, version, size, entry size or counts do not check
  (size exactly 128 + count x 128, count at most 24, ports at most 4,
  each path count at most 7).
- **Confirmed** means the running driver answered this run: the tool
  writes a fresh nonzero `XhciCmStatusAsk` (`REG_DWORD`) to the NHI's
  driver key, and waits up to three seconds for a record whose echo
  (offset 124) equals it. Only the connection-manager thread of the
  driver now bound to that NHI can produce that echo, and it writes it
  into a record it builds after reading the ask, so a confirmed record is
  this session's and current at that moment: an earlier boot's, an
  earlier load's, or one whose writes have been failing is never
  confirmed. Each ask differs from the one before and from the echo the
  record already carries. Writing the ask needs write access to the driver
  key: on 98 SE and ME always; on NT an administrator (the elevated run on
  Vista and 7). The ask is a value the tool owns; the driver only reads
  it. One device-mode run at a time holds a named mutex for the whole run
  (`XhciinstTbAsk`, `Global\` on NT), so two runs never overwrite or
  delete each other's ask; the second waits ten seconds, then refuses
  (exit 1). The tool deletes its ask at the end of its run.
- **Generation**: after the confirmation the tool walks the devices and
  reads the record again (from an NHI devnode still present, started and
  bound to `xhcicm`); a different generation or echo means a change
  during the walk, and the whole reading is taken again, at most three
  times, then shown with "devices were changing while this was read" and
  with no eject offered. Generation numbers are this session's alone (the
  start resets them); the echo is what ties them to the session.
- **The stamp** (uptime and system time) is diagnostic only: an
  unconfirmed record is shown, labelled "not confirmed by the running
  driver" and, where the boot time it implies is more than five minutes
  from this boot's, "from an earlier start of Windows".
- An unconfirmed record (no answer, an inert driver, no rights to ask) or
  none at all: the viewer shows the PCI and Windows side and whatever
  record there is, labelled, marks every bridge "not verified", and
  `/eject` refuses (exit 1) with "The Thunderbolt driver has not confirmed
  which ports carry tunnels, so nothing can be ejected safely from here."

## 4. Finding the devices

### 4.1 The devnode tree

The tool walks the whole devnode tree (98 and ME: `CM_Locate_DevNodeA`,
`CM_Get_Child`, `CM_Get_Sibling`, ids from `HKLM\Enum` as record 23
section 6.3 does; NT: the same routines, ids through
`CM_Get_DevNode_Registry_PropertyA`), keeping each devnode's parent. A walk
that is incomplete (a link, an id, a hardware or compatible id list, the
driver or service that cannot be read - absent is not unreadable - the
depth or node bound reached) is said; the viewer shows what it read, and
`/eject` refuses.

### 4.2 Verified tunnel ports, and candidates

- A **verified tunnel port** is a bridge devnode whose location equals an
  **active** member of a confirmed record's set (section 3.3). Its location is
  `CM_DRP_BUSNUMBER` and `CM_DRP_ADDRESS` on NT; on 98 and ME its instance
  id, which names a function on bus 0 as `BUS_00&DEV_dd&FUNC_ff` and one
  behind a bridge by its devfn path (`runs/run-39.md`, "98 SE's instance
  ids"), compared with the member's bus, device and function or with the
  path the record carries. A member whose path is unknown on 98 is not
  verified there. Override and router-named members (record 21 section
  6.3, record 22 section 7.6) are verified the same way, whatever their
  ids.
- A **candidate** is a bridge whose hardware ids include one of the filter
  INF's model ids (`src\cm\xhcicmf.inf`: the Thunderbolt switch ports and
  integrated tunnel root ports, and the emulator's `1B36:0001` and
  `1B36:000C`), compiled into the tool's pure half as a table that the
  host suite checks against the INF both ways (section 10). The INF matches
  every port of a discrete switch, not only the tunnel ports, so a
  candidate is shown, with what is below it, and is never a ground for an
  eject.

### 4.3 Units

A **unit** is a child devnode of a verified tunnel port: the NVMe
controller of an enclosure, or the upstream port of a dock's switch with
its subtree. Units are the only things `/eject` acts on, and each is
numbered. Their descendants are listed beneath them, each with its
description, first hardware id, driver (the `Service` on NT; on 98 and ME
the driver key's `NTMPDriver` or `DevLoader`) and problem code.

**The host's own functions are protected** independently of which ports
are verified, because an override (`XhciCmTunnelBridge`) can name any
bridge:

- every NHI - a devnode bound to `xhcicm`, or of class `0880` or `0C0340` -
  and every ancestor of one, up to the root;
- every USB host controller (class `0C03`) whose grandparent is an NHI's
  grandparent: the xHCI of a discrete Thunderbolt controller sits on a
  downstream port of the same switch as its NHI, and an integrated
  controller's xHCI on the same root bus as its NHI. A dock's own xHCI
  sits below the dock's switch, two or more levels further down, and is
  not caught; a bare USB controller card tunnelled directly onto a host
  downstream port would be, and is then shown and not ejected - the
  conservative side of the rule.

A unit whose subtree contains a protected devnode is shown with "contains
the Thunderbolt controller's own functions" and is not numbered; a unit is
never a bridge in the record's set itself. The host suite covers an
override naming the host xHCI's port and a unit with an NHI nested in it.

On 98 and ME every unit is offered, marked or not, with "marked removable
by the Thunderbolt driver" or "not marked removable" beside it (the mark
is set by the worker for a new child; a unit tunnelled before the worker's
baseline is not marked). On NT the viewer shows the unit's `Removable`
and `SurpriseRemovalOK` capabilities (`CM_DRP_CAPABILITIES`) as they are,
and says the applet lists a device with `Removable` and without
`SurpriseRemovalOK`; it does not claim more.

### 4.4 Routers to units

A router is shown with the units of the tunnel port its entry names; a
router whose port is unknown is shown under its controller, and units
whose port no router names are shown under "behind this port". The tool
never guesses a pairing the record does not state.

## 5. Interplay

### 5.1 `xhcicm.sys` on 98 SE and ME (the worker of `cm_cfgwk.c`)

The tool's removal happens outside the connection manager. What the 9x
re-enumeration worker (`cm_cfgwk.c` on branch `p39-reenum`, read
2026-10-15) does afterwards:

- It makes CONFIGMG calls for requests the connection-manager thread posts
  (a tunnel up, a tunnel down, a resync) and for its own retries of a
  re-enumeration that failed. A removed unit stays removed until one of
  those re-enumerates its port - the next tunnel change, or a retry still
  owed - or until something else re-enumerates the bridge (Device
  Manager's Refresh, the "Scan for hardware changes" of the Add New
  Hardware wizard): the tunnel is still up and the function still answers
  on the bus, so any of those brings it back. The tool says so.
- At the unplug the tunnel goes down and the worker removes the port's
  vanished children: the removed devnode is no longer there, so nothing is
  queried twice, and `Reenumerate_DevNode` finds the bus empty.
- **The mark after a re-plug** (reported to the connection-manager work,
  not changed here): the worker's known-children set is replaced by the
  port's children at each marking walk (`cmWkMark`), and a devnode leaves
  it otherwise only when the worker itself removes it (`cmWkForget`). A
  unit the tool removed is still in the set when the tunnel goes down, the
  vanish pass removes nothing and walks no mark, and at the re-plug's
  marking walk a new devnode that CONFIGMG gave the freed handle is taken
  as known and not marked `DN_REMOVABLE`. The fix belongs in the vanish
  pass of `cmWkMember`, which does walk the port's children: keep only the
  known entries still among them. The worker's later revision on the same
  branch does this (the known set pruned at every complete walk, and a
  prune owed at every tunnel down, `CmCfgwkPrune`, read 2026-10-15). The
  tool offers the unit marked or not (section 4.3), so the eject is not
  lost either way. On the driver of this release's branch (the re-
  enumeration of `cm_reenum.c`, before the worker), leg E3 read the
  re-plugged unit marked removable.
- Under the USB4 software connection manager, a removal the tool makes on
  98 or ME does not reach the per-port tunnel teardown of record 22
  section 10.4 (the 9x half has no child filter to see it): the PCIe
  tunnel stays up until the unplug, as under the ICM. Record 22's text
  that promises that teardown on 9x is reported to the connection-manager
  work as inconsistent with this.

### 5.2 `xhcicm.sys` on the NT targets

`CM_Request_Device_EjectA` is the call the applet makes, so a unit with
the child filter sees the same query-remove and remove: the child filter
counts the orderly removal (`CmRegChildRemoved`), the NHI's thread picks
it up later in `CmReenumService`, and under the USB4 profile the per-port
teardown of record 22 section 10.4 runs as it does after the applet;
under the ICM the eject is logged and the tunnel left up (record 21 section
7.4). That work is not synchronised with the call's return. So the tool
separates the two: the eject's success is Windows' answer; then, under
the USB4 profile, the tool watches the record for up to ten seconds for
the router's PCIe tunnel to read "torn down after an eject" and says
which it saw ("The PCIe tunnel was closed." or "The Thunderbolt driver has
not confirmed that the PCIe tunnel was closed."). Neither changes what
Windows answered, and neither is the unplug advice of section 5.4. A unit
with no child filter reaches nothing in `xhcicm.sys`; its tunnel stays up
until the unplug.

Record 22 section 10.4 says the tunnel is down before the user is told the
device may be unplugged. For an eject this tool makes, that ordering is
not established: the PCIe tunnel line above reports what the record shows,
and this record supersedes record 22 section 10.4's ordering clause for
the tool's ejects; the clause itself is reported to the connection-manager
work to be narrowed to what the applet's eject can promise too.

### 5.3 `nvme98`

On 98 SE and ME `nvme98` is a SCSI miniport under IOS; the query-remove is
where IOS checks the volume for open files and refuses, and an accepted
query has already failed the volume's I/O, which cannot be resumed in
place (`nvme98`'s decision D2, its `docs/PLAN.md`, read 2026-10-15). Hence
the rule of section 7.1: an accepted query is always followed by the
removal. On 98 SE the remove gives `nvme98` its native stop; on ME
`nvme98` gets no stop callback at a CONFIGMG remove and relies on
write-through (its MD-5 and D3). The removal handshake `nvme98`'s MD-5
plans (a `REMOVE_PENDING` request and a clean acknowledgement before the
connection manager calls `Remove_SubTree`) is the connection manager's,
and a removal the tool makes does not run it; when that handshake exists,
the 9x eject should be asked of the connection manager (section 14). The
query goes to the unit - the NVMe controller's PCI devnode - so the IOS
devnodes of its disk and volume are inside the subtree; the tool never
climbs to a bridge. On NT `nvme98` receives the query-remove and remove of
any eject.

### 5.4 The rest of the device

A unit is one PCI subtree. A dock can carry more than one unit behind the
same port, and its USB devices may reach the host's own USB controller
outside every unit - through a USB4 USB3 tunnel, or over the cable's USB
2.0 pair, which no record describes. So after an eject the tool says
first what Windows did ("Windows has stopped <unit>."), then reads the
port again, and gives the unconditional advice only when everything it
needs is positively established:

- **"You can now unplug the device."** only when the walk after the eject
  was read whole, no other unit behind that port is still running (the
  ejected one, held by Windows until the unplug, does not count; if it
  has started again, nothing is established), the removed unit was a
  single storage function - of class `01`, not a bridge or a USB host
  controller, with no PCI function below it (its disk and volume entries
  are not PCI functions) - read from the walk made after the
  revalidation, and the confirmed record, read again after the eject,
  names that port among its active members and exactly one router on it,
  whose USB3 tunnel reads none. A USB 2.0 function the same physical
  device might reach over the cable's USB 2.0 pair is in no record; for a
  storage-only enclosure it is not expected, and this is the one thing
  the advice takes on trust (decision E10);
- another unit behind that port still running: "Other devices behind the
  same Thunderbolt port are still running: eject them too before you
  unplug it.";
- otherwise (a dock-shaped unit, a USB3 tunnel up or not known, the walk
  incomplete): "Before you unplug it, make sure none of its other devices
  (USB ports, card readers, network) is in use; stop them in Windows
  first."

The exit code is 0 in all three: the asked eject succeeded. The legs read
the first; the others have no vehicle here (section 12). If the ejected
unit itself is running again in the reading after the eject (re-enumerated
meanwhile), the line is instead "The device has started again, so it is
not safe to unplug. Close the programs using it and run INSTALL /eject
again." and the exit code 4. The router and port facts are taken from the
same record the stable walk was checked against, never from a later read.

## 6. What `/devices` prints

Per controller, for example:

```
Thunderbolt 3 controller: xHCI98 Thunderbolt 3 Host Interface
  PCI 8086:15BF, driver xhcicm, firmware connection manager, security 1
  information from the driver: confirmed now (changed 7 times since it started)

  Device: Lenovo ThinkPad Thunderbolt 3 Dock  (id ending 3a7c)
    Thunderbolt 3, link 1, depth 1, approved
    connections: PCIe up, USB3 not known, DisplayPort not known
    behind Thunderbolt port PCI 02:01.0:
      [1] NVM Express Controller         nvme98     working
          PCI\VEN_1B36&DEV_0010   marked removable by the Thunderbolt driver
          instance PCI\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02\0008083000

  Other Thunderbolt ports (not carrying a connection now): 02:00.0, 02:02.0

  To eject: INSTALL /eject 1   or, in a script:
            INSTALL /eject id=PCI\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02\0008083000 /yes
```

The numbers in brackets are the `/eject` numbers, assigned in tree order on
each run (section 7.3); the full instance id is printed beside every
numbered unit for `/eject id=`.

## 7. The eject

### 7.1 Windows 98 SE and ME

1. `CM_Query_Remove_SubTree(unit, CM_QUERY_REMOVE_UI_NOT_OK)`: Windows' own
   dialogs are not shown; the tool says what happened.
2. Not accepted - nothing else is done:
   - `CR_REMOVE_VETOED`: "Windows refused: the device is in use (a program
     may have a file open on it, or its driver refused). Close any program
     using it and try again." The answer names no cause, so the text
     claims none. Exit 4.
   - `CR_INVALID_DEVNODE` or `CR_NO_SUCH_DEVNODE`: "The device list
     changed; nothing was ejected." Exit 1.
   - `CR_CALL_NOT_IMPLEMENTED`: "This Windows does not offer the removal
     this needs." Exit 1.
   - anything else: "Windows refused the removal (code 0x...)." Exit 4.
3. Accepted: `CM_Remove_SubTree(unit, CM_REMOVE_UI_NOT_OK)` at once, always
   (an accepted query cannot be cancelled on 9x, and a driver left
   query-removed stays quiesced: section 5.3). Then section 5.4's message,
   and "Until you unplug it, Device Manager's Refresh, or the Thunderbolt
   driver looking at the port again, can start it again." Exit 0.
4. A removal that fails after an accepted query: "Windows began to stop
   the device but could not finish (code 0x...). It is not safe to unplug
   it now. Shut down Windows, then unplug it." Exit 2.

`cfgmgr32.dll` on 98 SE (4.10.2222, 45,056 bytes) and ME (4.90.3000, 40,960
bytes) exports the same 179 names (read 2026-10-15 from each guest image's
`WINDOWS\SYSTEM\CFGMGR32.DLL` export table; static), among them
`CM_Query_Remove_SubTree`, `CM_Remove_SubTree` and the walk's routines;
neither exports `CM_Request_Device_EjectA`. Both routines are looked up at
run time, and a missing one refuses the eject (exit 1) with its name.
Whether the pair does from a Win32 program on each system what it does
from a VxD, with `nvme98` and IOS below, is the legs' reading; until a leg
has read it on a system, the eject is not described as working there
(section 12).

### 7.2 Windows 2000, XP, Vista and 7

1. `CM_Request_Device_EjectA(unit, &vetoType, vetoName, MAX_PATH, 0)`,
   resolved from `cfgmgr32.dll`, else `setupapi.dll`: Windows 2000's
   `cfgmgr32.dll` does not export it and its `setupapi.dll` does (read
   2026-10-15 from the 2000 guest image's `system32` export tables;
   static). The veto outputs are initialised (`PNP_VetoTypeUnknown`, an
   empty name) and read only when the call returns `CR_REMOVE_VETOED`.
2. Vetoed (`CR_REMOVE_VETOED`), by type, each with "nothing was changed"
   and exit 4:
   - `PNP_VetoOutstandingOpen`: "Windows refused: a program is using the
     device. Close it and try again.";
   - `PNP_VetoDevice`, `PNP_VetoDriver`: "Windows refused: <name> did not
     allow it.";
   - `PNP_VetoPendingClose`: "Windows refused: the device is still being
     closed. Try again in a moment.";
   - `PNP_VetoWindowsApp`, `PNP_VetoWindowsService`: "Windows refused: <name>
     is using the device.";
   - `PNP_VetoInsufficientRights`: "Windows did not allow this account to
     remove the device. Run it as an administrator." - exit 1, as an
     access refusal (section 8.2), not 4;
   - `PNP_VetoIllegalDeviceRequest`, `PNP_VetoNonDisableable`,
     `PNP_VetoLegacyDevice`, `PNP_VetoLegacyDriver`: "Windows does not allow
     this device to be removed.";
   - otherwise: "Windows refused (veto <n>: <name>)."
3. Other answers: `CR_ACCESS_DENIED` - the same rights text, exit 1;
   `CR_INVALID_DEVNODE` or `CR_NO_SUCH_DEVNODE` - "The device list changed;
   nothing was ejected.", exit 1; anything else - "Windows could not eject
   the device, and its state is not known. Do not unplug it until Windows
   has been shut down." with the code, exit 2, and that text is the last
   line.
4. Accepted: section 5.2's tunnel line, then section 5.4's message. Exit 0.

Rights: Microsoft's reference for the call names the privileges it needs
(`SeUndockPrivilege` for a dock, and `SeLoadDriverPrivilege` for some
callers); the device modes skip record 23 section 7.2's administrator
test, and report what the call answers. The confirmation of section 3.3
needs write access to the driver key, which on 2000 and XP is an
administrator's, so in practice an eject there runs as one. On Vista and
7 the manifest elevates every run. After an accepted eject Windows holds a
removable device as ejected until it is unplugged (Code 47 on XP and
later, "held for eject"); what it does with a device that is not
removable is leg E4's reading, and no rescan advice is printed on NT.

**A unit Windows does not mark removable** (the case of section 1.1; revision
5). Its capabilities were read and say neither `Removable` nor
`SurpriseRemovalOK` (`inst_unit_not_marked`), whatever the cause: a
stack rebuilt after a disable and enable or a driver update (leg E4,
2026-10-15), the first plug of a device Windows has not seen behind the
tunnel port, whose driver install rebuilds the stack (read on 2000 and XP
with a bridge chain, `runs/run-41.md`; the same legs on drivers without
record 21 section 7.4's attachment above bus filters also read it on XP
and 7 after a re-plug, which that attachment fixes). Safely Remove
Hardware does not list it, and
`CM_Request_Device_EjectA` refuses it as `PNP_VetoIllegalDeviceRequest`
(8): `CR_REMOVE_VETOED` on 2000 and 7, `CR_SUCCESS` with that veto type
on XP. Every answer of that routine is read through `inst_eject_cr`
first: a `CR_SUCCESS` that carries a veto type is the veto it names, on
every unit (XP answers a refused eject so, read for a volume by record 25's
T.7 leg and here for this refusal). `inst_eject_nt_step` decides what
follows, for a unit not marked removable only:

- the illegal-request refusal: the tool asks
  `CM_Query_And_Remove_SubTreeW(unit, &veto, name, MAX_PATH,
  CM_REMOVE_UI_NOT_OK | CM_REMOVE_NO_RESTART)` (from `setupapi.dll`, else
  `cfgmgr32.dll`), and asks again with `CM_REMOVE_UI_NOT_OK` alone if
  Windows answers `CR_INVALID_FLAG`. It is the same query-remove - which
  every driver of the subtree, the volume's included, may veto as for an
  eject - and then the remove. The `W` form is used everywhere because
  Windows 2000's `CM_Query_And_Remove_SubTree_ExA`, which the `A` form
  calls, is a stub answering `CR_CALL_NOT_IMPLEMENTED`; its `ExW` accepts
  both flags (static, `legal-provenance.md` section 4). 2000, XP and 7
  accepted `0x3` and stopped the unit (`runs/run-41.md`). Microsoft's
  reference limits the routine to devices with neither capability, and it
  needs `SeLoadDriverPrivilege`, an administrator's;
- a rights refusal (`CR_ACCESS_DENIED` or `PNP_VetoInsufficientRights`,
  from either routine), or, in XHCITR, a process known to lack the right:
  "Windows does not list this device for safe removal. Run this as an
  administrator to stop it." - exit 1, as an access refusal (section
  8.2). The text promises nothing of a re-plug;
- `CR_CALL_NOT_IMPLEMENTED` from the fallback (no system read answers so
  since the `W` form): the earlier step text - a restart with the device
  connected, or an unplug with the computer off - exit 4, nothing changed;
- anything else: the table of items 2 and 3; nothing overrides a veto.

Afterwards Windows does not hold the device as ejected: it may start it
again the next time it looks for new hardware while the device is
connected, and the tool says so ("Until you unplug it, Windows may start
it again when it next looks for new hardware."). `CM_REMOVE_NO_RESTART`
is asked so that it does not, where Windows honours it; whether a rescan
restarts the unit after this stop is not read. An unplug after it showed
no Unsafe Removal dialog on 2000, and a re-plug brought the unit back
started (`runs/run-41.md`). XHCIINST runs as an administrator on every NT
target (record 23 section 7.2; on Vista and 7 its manifest elevates), so
its route is the one above; XHCITR's, which offers the rights when it
lacks them, is record 25 section 6.6.

### 7.3 Choosing the unit

- **`/eject`** alone lists, asks for a number (Enter cancels), then shows
  the unit's description and instance id and asks once (Y).
- **`/eject N`** walks, shows what N is now - description and instance id
  - and asks once (Y). `/yes` is refused with a number, because numbers are
  assigned on each run and a device unplugged since the listing the user
  read shifts them.
- **`/eject id=<instance id>`** names the unit by its device instance id,
  as `/devices` prints it, compared without case; it may take `/yes`, for
  scripts.
- Immediately before the call (after the confirmation) the tool writes a
  fresh ask to every confirmed NHI and waits for its echo (so a record
  whose writes stopped while the question waited cannot pass), then reads
  the chosen devnode again: its instance id must be the one chosen, its parent
  the same verified port, its subtree still free of protected devnodes,
  and the record still confirmed with the echo and generation it was
  chosen under; otherwise "The device list changed; nothing was ejected."
  (exit 1).
- One unit per run; never a forced removal: no `CM_Remove_SubTree` without
  an accepted query, no veto overridden (`CM_Query_And_Remove_SubTreeA`
  only as section 7.2 says, and never with a flag that skips the query), no
  `DIF_REMOVE`.

## 8. The command line and exit codes

### 8.1 Switches

| Switch | Meaning |
|---|---|
| `/devices` | the viewer (section 6); reads only, writes no log unless `/log` names one, no pause |
| `/eject` | the viewer, a number, a confirmation, the eject |
| `/eject N` | unit N of a fresh walk, after a confirmation; `/yes` refused |
| `/eject id=<instance id>` | the unit with that instance id; `/yes` allowed |
| `/yes`, `/nopause`, `/log path` | as record 23 section 7.1 |

`/devices` and `/eject` read nothing of the package: they skip record 23
section 4.2's checks, and the `/dir` that `INSTALL.BAT` passes is accepted
and ignored. Either with an install switch (`/list`, `/defaults`, `/set`,
`/force`, `/restage`, `/uninstall`), with each other, or `/eject` with an
argument that is neither a positive decimal nor `id=` and an id, is
refused with exit 1 before anything is read. On 98 and ME
`INSTALL.BAT`'s nine-argument limit is not reached by these.

### 8.2 Exit codes and the last line

Record 23 section 7.4's codes, with the device modes' exception written
there:

One outcome table, shared by both systems' routes and by record 23's
exception:

| Outcome | Code | Last line |
|---|---|---|
| listed (`/devices`) | 0 | "No changes were made." |
| ejected | 0 | section 5.4's message |
| refused before any request (a bad switch, a routine missing, cancelled, no confirmed record, an incomplete walk, the list changed at revalidation) | 1 | "No changes were made." |
| rejected by the call (the device list changed, the call not offered, access denied, `PNP_VetoInsufficientRights`) | 1 | "No changes were made." |
| vetoed (the device in use, a driver or service refused) | 4 | "No changes were made." |
| uncertain (9x: the removal failed after an accepted query; NT: an answer that leaves the state unknown) | 2 | the failure text of section 7.1 item 4, or 7.2 item 3, whole |

Code 3 never occurs: an eject needs no restart.

## 9. Messages

Plain and practical, in the install tool's voice, with no word about
virtual machines, emulators or what was tested (the owner's rule):

- "No Thunderbolt or USB4 controller driven by this package was found."
- "Nothing is connected to the Thunderbolt or USB4 ports."
- "Windows refused: the device is in use ..." (98 SE and ME) and the veto
  texts of section 7.2 (NT).
- "You can now unplug the device." and the qualified forms of section 5.4.
- "Until you unplug it, Device Manager's Refresh, or the Thunderbolt
  driver looking at the port again, can start it again." (98 SE and ME,
  after an eject)
- "not marked removable: Safely Remove Hardware may not list it; /eject
  stops it when run as an administrator" (NT, `/devices`, a unit
  without `Removable`; revision 5)
- "Windows does not list this device for safe removal. Run this as an
  administrator to stop it." (NT, such a unit, a rights refusal; revision
  5)

`README.TXT`, `INSTALL.BAT`'s comment, README's Thunderbolt text and the
release notes carry: "To remove a Thunderbolt or USB4 device safely, run
INSTALL /eject (or XHCIINST /eject) on Windows 98 SE and ME; on Windows
2000 and later Safely Remove Hardware works too, and INSTALL /eject also
asks Windows to stop a device Safely Remove does not list. INSTALL
/devices shows what is connected." - narrowed to what the legs read.

## 10. The pure half and the host suites

In `xhciinst\inst_core.c` (host suite `test\test_inst.c`):

- the switch parser's new modes and their refusals;
- the candidate table and `inst_tb_candidate`, checked against
  `src\cm\xhcicmf.inf`'s model lines both ways;
- the 98 instance-id location parser (the `BUS_` form and the path form)
  and the location compare with a record's member;
- the unit selection and protection over a mock tree with parents: the
  enclosure and the dock shapes, the host switch with its NHI and xHCI
  under unverified candidates, an override that verifies the host xHCI's
  port, an NHI nested inside a unit (an incomplete walk is not expressible
  in the pure API's tree; section 5.4's message for one is checked below);
- the record's decoder and checks (magic, version, sizes, counts, path
  counts), the confirmation (an echo that matches, one that does not, a
  record of an earlier session with the same generation), and the
  diagnostic boot-time label (the same boot, and two boots minutes
  apart) - against records the suite builds itself to `cm_status.h`'s
  layout (`test_inst` links `inst_core.c` alone, so the driver's
  `cm_status.c` builder is held by `test_cm_status`, below); a wrapped
  tick count has no vector;
- the eject sequences with injected answers: a 9x query refused, accepted
  then removed, accepted then the removal failing (the removal always
  attempted after an accepted query, never after a refused one); each NT
  answer and veto type; the exit code and last line per outcome; section
  5.4's message for each input, an unknown USB3 state and an incomplete
  walk among them;
- the value rows of section 11 against `xhcicm.inf`'s defaults, and `/set`
  accepting each allowed value and refusing the next one up.

In `src\cm\cm_status.c` (host suite `test\test_cm_status.c`): the record
from an ICM table and from a USB4 port set and its links - names cut and
filtered, the UUID cut to 16 bits and no `Key` byte anywhere in the record,
each approval state the builder writes (approved, before boot, refused,
unanswered, pending, held) and each tunnel state, the port mapping (one active member, a
bind, a conflicted bind), the change compare that ignores the stamp, the
echo, the entry limit and its flag - and the 98 path over a fake
configuration space (bus 0, a bridge chain, an unreadable bus, a read
that fails once and then succeeds).

`xhciinst\selftest.cmd` adds `/devices /list`, `/eject x` and `/eject 1
/yes` to the refusals it checks.

## 11. Record 23's value table (owner, 2026-10-15)

Four `xhcicm.inf` values join record 23 section 8.1's table as never-asked
rows that `/set Name=Value` can set, each allowed set taken from
`cm_vals.c`'s rule table and records 21 section 8 and 22 section 12:

| Value | Allowed (`/set`) | Why not asked |
|---|---|---|
| `XhciCmReenum` | 0, 1 | 0 exists to diagnose a machine where the re-enumeration misbehaves |
| `XhciCmUsb3Tunnel` | 0, 1 | a USB4 diagnostic whose default is the point |
| `XhciCmLaneBonding` | 0, 1 | a USB4 diagnostic whose default is the point |
| `XhciCmPreOsCleanup` | 0, 1, 2 | an override of record 22 section 9's tunnel preservation |

The deferred check of record 23 section 8.1 is done: `XhciCmMode` 0 to 3,
`XhciCmApprove` 0, 1 and `XhciCmSafeEject` 0, 1 match `cm_vals.c` and both
records, and the four above match too.

## 12. The legs

On development host A, overlays only, under the admission and lock rules
of the Phase 39 brief, the emulator's `tb3-alpine-ridge` profile with
`nvme98` (its published `0.1.1.0`, read-only) behind the tunnel unless a
leg says otherwise; recorded in `runs/run-39.md` as a 39-A.8 follow-up.
Every eject leg writes a file of known content to the NVMe volume first
and, after the re-plug, reads it back and compares it.

| Leg | Systems | What it reads |
|---|---|---|
| D, viewer | 98 SE, 2000; XP or 7 where a slot allows | the controller, the router with its names, approval and tunnels from the record, the unit with its driver; the confirmation; the generation moving at a re-plug; after a restart, the first record a new start wrote, never the earlier boot's |
| E1, eject idle | 98 SE, 2000 | the eject accepted, the last line, the unit gone (98) or held (2000), the unplug with no dialog and no fault |
| E2, refused | 98 SE, 2000 | a file held open on the NVMe volume: the refusal text, exit 4, the unit still started and the file still readable and writable; closed, then the eject accepted |
| E3, re-plug | 98 SE | after an eject and the unplug, the device back at the next plug with its driver started, the file compared, and whether it is marked `DN_REMOVABLE` (section 5.1) |
| E4, not removable | 2000 | the device disabled and enabled in Device Manager: missing from Unplug or Eject Hardware, its capabilities as shown, what `CM_Request_Device_EjectA` answers, and if accepted whether the unit stays removed until the unplug |
| E5, a stale choice | 98 SE | `/eject 1`, the unit unplugged while the confirmation waits, then Y: "The device list changed", exit 1 |
| E6, USB4 | 2000, the `usb4-generic` profile | the viewer from the USB4 record, the eject and section 5.2's PCIe tunnel line |
| E7, ME | ME, if an image with `xhcicm.sys` installed is available | D, E1 and E2: ME's IOS and the missing stop callback are the reason it is not covered by 98 SE |

**Run on 2026-10-15** (`runs/run-39.md`, "39-A.8 follow-up"): D, E1, E2
and E3 on 98 SE and D, E1, E2 and E4 on 2000 passed, the 2000 E4 reading
being that the not-removable case cannot be stopped there (section 7.2).
E5, E6 and E7 were not run: E5 is covered by the host suite's injected
answers, and no ME image with `xhcicm.sys` installed and no USB4-profile
guest with `nvme98` were at hand.

What a leg does not read stays unclaimed: the eject on a system with no
passing E1 and E2 is described in the README and release notes as not
read there; ME without E7 is said to be untested. A failed removal after
an accepted query, a failed record write, a stale session and a dock with
two units are covered by the host suites' injected answers (section 10)
and have no live vehicle here; they are owed to the bench.

## 13. Decisions taken in writing this

- **E1** The viewer and the eject are modes of the install tool, not a
  third executable (section 2).
- **E2** The driver publishes a status record per NHI as a value of its
  hardware key on every target, not a volatile key, so that `xhcicm.sys`
  takes no import `xhci98.sys` does not; currency comes from the ask and
  its echo, the stamp being diagnostic (sections 3.2 and 3.3). The owner's
  instruction asked for a volatile key on NT; this is the deviation, for
  the owner to confirm.
- **E3** The UUID is shown as its last 16 bits, a display hint only; no
  authentication material is published (section 3.1).
- **E4** Only units of verified, active tunnel ports in a confirmed record
  are ejected, and never one holding the host's own functions; candidates
  are shown only (sections 4.2 and 4.3).
- **E5** On 98 SE and ME a unit not marked `DN_REMOVABLE` is still offered,
  with a note (section 4.3).
- **E6** On NT the eject is `CM_Request_Device_EjectA`, the applet's own
  route, for every unit; where Windows refuses it as an illegal request for
  a unit that is not removable (read on 2000), `CM_Query_And_Remove_SubTreeA`
  makes the same query-remove and remove, vetoes honoured (section 7.2).
- **E7** One unit per run; `/eject N` always confirms; `/yes` only with
  `id=`; never forced (section 7.3).
- **E8** The worker change of section 5.1 is reported to the
  connection-manager work, not made here; for the tool's ejects this record
  supersedes record 22 section 10.4's ordering clause (section 5.2), and
  the clause is reported for narrowing.
- **E9** The status keys are opened in the PnP start's dispatch and the
  thread only reads and writes values on them (section 3.2): `cm_nhi.c`'s
  two calls, `CmStatusOpen` in the start and `CmStatusClose` in the
  removal (for a start that failed before the thread ran).
- **E10** "You can now unplug the device." - the owner's words - is
  printed for a single storage function whose port and router the
  confirmed record establishes, taking on trust only that a storage-only
  enclosure reaches no USB 2.0 function over the cable (section 5.4);
  everything else gets the qualified advice.

## 14. Open

- Whether `CM_Query_Remove_SubTree` and `CM_Remove_SubTree` from a Win32
  program work on 98 SE and on ME with `nvme98` below (legs E1, E2, E7).
- How soon Windows starts a unit stopped by the query-and-remove route
  again while it stays connected, with `CM_REMOVE_NO_RESTART` asked (the
  route itself was read on 2000, XP and 7, revision 5).
- The link speed is kept by neither connection manager.
- The `nvme98` removal handshake (its MD-5): when it exists, the 9x eject
  should be asked of the connection manager, which can run it, rather than
  made by the tool.
- A dock with several units, and its USB devices under USB4: section 5.4's
  messages are written from the design, with no vehicle to read them here.
- Titan Ridge and the integrated hosts: the record's fields are filled from
  the same tables, untested beyond what records 21 and 22 say.

## 15. Files

| File | What |
|---|---|
| `src\cm\cm_status.h` | the record's layout, shared with the tool |
| `src\cm\cm_statb.h`, `cm_status.c` | the record built from the connection manager's state, and the 98 path (pure) |
| `src\cm\cm_statos.c` | the keys, the ask, the writes |
| `src\cm\cm_nhi.c`, `cm_thr.c`, `cm.h`, `sources` | the four calls, the field, the source lines |
| `scripts\import-gate\xhcicm-imports*.allow` | the registry and time rows for `cm_statos.obj` |
| `test\test_cm_status.c`, `test\run-host-tests.cmd` | the record's host suite |
| `xhciinst\xhciinst.c`, `inst_core.c`, `inst_core.h` | the modes |
| `test\test_inst.c`, `xhciinst\selftest.cmd` | their checks |
| `xhciinst\README.TXT`, `INSTALL.BAT`, `README.md`, `docs\using\release-notes.md` | the lines of section 9 |
| `docs\contributing\legal-provenance.md` section 4 | the two static export readings of sections 7.1 and 7.2 |
| design records 21 (section 7.4) and 23 (sections 7.1, 7.4 and 8.1) | pointers here, and the exit-code exception |
