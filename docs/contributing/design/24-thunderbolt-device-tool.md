# Design Record 24 - The Thunderbolt and USB4 Device Viewer and Eject

Status: revision 1, 2026-10-15, before code. Written for the owner's
instructions of 2026-10-15: Windows 98 SE and ME have no way to safely
remove a device behind a Thunderbolt or USB4 tunnel, and no target system
has a way to see what is connected to one. The answer is a viewer of the
connected Thunderbolt 3 and USB4 devices, with an eject, in the install
tool of design record 23, on every target this package supports. It ships
in `3.0.0.0` with Phases 39 and 40 (`roadmap-usb4.md`). Section 13 lists
the decisions taken in writing it, each an orchestrator decision for the
owner to confirm unless it names the owner; section 14 what is open.

It is not a Thunderbolt control panel: it approves nothing, changes no
security level, updates no firmware, and shows no key material.

## 1. What it is for

### 1.1 The problem

- **Windows 98 SE and ME have no eject for a tunnelled device.** `xhcicm.sys`
  marks the devices behind an active tunnel port `DN_REMOVABLE` on these
  systems (record 21 section 7.4), and nothing offers an eject for them:
  98 SE has no general eject applet (`runs/run-39.md`, 39-A.8), and the
  "Unplug or Eject Hardware" applet NUSB installs lists only devices whose
  parent carries NUSB's private interface (section 1.3). The user's only
  route today is to close the device's files and unplug it.
- **On Windows 2000 to 7 the eject can drop out.** Safely Remove Hardware
  (Unplug or Eject Hardware on 2000) lists a device whose capabilities say
  `Removable`, which on a tunnel port is the child filter's answer (record
  21 section 7.4). A device tunnelled before the driver bound, or whose
  stack was rebuilt without the child filter above its PDO (disabled and
  enabled, or its driver changed, on a bridge the filter had already
  snapshotted), is not removable in Windows' eyes and is missing from the
  applet until it is re-plugged; it can still be stopped safely.
- **No target shows what is connected.** These systems have no
  Thunderbolt control panel; what a device is, whether it was approved,
  which tunnels it has and which Windows devices sit behind them is known
  only to `xhcicm.sys` and, piecemeal, to Device Manager.

### 1.2 Scope

Two modes of `XHCIINST.EXE` and `XHCIIN64.EXE` (section 2):

- **`/devices`**, on Windows 98 SE, ME, 2000, XP, XP x64, Vista and 7:
  each Thunderbolt or USB4 host controller this package drives, each
  device router connected to it with its names, generation, link,
  approval state and tunnels, and the PCI functions and Windows devices
  behind each tunnel port with their drivers and problem codes, each
  numbered and marked whether it can be ejected (section 6).
- **`/eject [N]`**, on the same systems: the safe removal of one listed
  device (section 7), by the route each system documents: on 98 SE and ME
  the configuration manager's query-remove and remove; on 2000 and later
  the eject request Safely Remove Hardware itself makes. `/eject` with no
  number lists and asks which.

The router details exist only inside the driver, so `xhcicm.sys` publishes
a status record the tool reads (section 3). What the tool shows without the
record - a driver older than this release, or one that has not written it -
is the PCI and Windows side alone, said so.

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

The viewer and the eject are modes of the install tool, `XHCIINST.EXE`
(x86) and `XHCIIN64.EXE` (x64), not a third executable. The reasons:

- The tool already starts on every target from one 32-bit image (and one
  x64 image), looks up every configuration-manager routine at run time,
  walks the 9x devnode tree with the registry as the authority for its ids,
  and is built, import-gated, staged and documented (record 23 sections 3,
  6.3 and 11). The viewer needs exactly that machinery; a separate tool
  would duplicate it, its build, its gate and its packaging rows, for a few
  hundred lines of its own.
- It is already in every package at `tools\xhciinst\`, where the release
  notes and README send the user.

The costs, taken knowingly: on Vista and 7 the manifest asks for elevation
on every run, so the viewer too shows the elevation prompt (the eject needs
an administrator there anyway, section 7.2); and on 64-bit Windows the user
runs `XHCIIN64.EXE`, as for the install - the 32-bit image keeps refusing
under WOW64 (record 23 section 3). `INSTALL.BAT` passes the switches on, so
`INSTALL /devices` and `INSTALL /eject` work from the package's top
directory; the modes also run from the local copy under
`%windir%\XHCI98\` on 98 and ME (record 23 section 4.4), since neither
reads the package (section 8.1).

A separate tool (`XHCITB.EXE`) would be the better choice if the viewer
grew a resident part (a tray icon, a notification on plug) or needed to run
unelevated on Vista and 7. Neither is in scope.

## 3. The status record (driver side)

### 3.1 What is published

One record per NHI `xhcicm.sys` drives, a `REG_BINARY` value
`XhciCmStatus`, written by that NHI's connection-manager thread. Its layout
is `src\cm\cm_status.h`, shared byte for byte by the driver and the tool
(the header uses no DDK or Windows type), all fields little-endian
`ULONG`s or byte strings:

- **Header** (64 bytes): magic `XCMS` (`0x534D4358`), layout version 1,
  total size, the **generation** (incremented on every content change, never
  0 after the first write), the system uptime in ms at the write
  (`KeQueryTickCount` times the increment), the NHI's vendor and device,
  the connection manager running (none, the ICM client, the USB4 software
  connection manager), the start's stage and inert reason (record 21
  section 1.4.2), the ICM's security level and firmware versions where the
  ICM row reports them, the domain's tunnel ports (up to four `XHCI_TBT_BDF`
  locations with their active bit, record 21 section 6.3), the router
  count and the router entry size.
- **One entry per router** (up to 24, 128 bytes each):
  - kind: an ICM-announced device, a USB4 router at depth 1 on a host port,
    or a deeper router of a predecessor's configuration (record 22 section
    7.1);
  - where: the host port or ICM link, the depth, and the route string;
  - names: vendor and model, as the ICM announcement's DROM names carry
    them (`TBT_ICM_NAMES`) or as the USB4 router's DROM was read
    (`VendorName`, `ModelName`), each cut to 31 characters of printable
    ASCII; the Thunderbolt vendor and model ids where known;
  - generation: Thunderbolt 3 or USB4 (with the router's USB4 version),
    and the PCI vendor and device of a USB4 router;
  - link: lanes (single, bonded or unknown) where the USB4 connection
    manager configured or found them; the link speed is unknown in this
    revision (neither half keeps it) and the record says so;
  - approval: approved, approved before boot, rejected, approval refused by
    the firmware, approval unanswered, or not approved (policy);
    under the USB4 profile the port state and its reason (record 22
    section 7.2);
  - tunnels: PCIe, USB3 and DisplayPort, each none, up, found at start
    (preserved), failed, or torn down after an eject;
  - the tunnel port the router's PCIe tunnel reaches, where known: the USB4
    profile's port-to-bridge correspondence (`XHCI_TBT_BINDS`, record 22
    section 7.6); under the ICM, the domain's single active member when it
    has exactly one, else unknown;
  - a short identifier: the last four hexadecimal digits of the router's
    UUID, enough to tell two identical devices apart and not enough to
    stand for the UUID. The ICM's `Key` and every approval field are never
    copied.

### 3.2 Where, and when

- **NT targets**: a volatile subkey `XhciCmStatus` of the NHI's hardware
  key (`IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DEVICE)`, the `Device
  Parameters` key), created with `REG_OPTION_VOLATILE`, so nothing survives
  a restart; the value `Record` inside it.
- **Windows 98 SE and ME**: the value `XhciCmStatus` on the NHI's hardware
  key itself (`HKLM\Enum\PCI\...\<instance>`), through the same
  `IoOpenDeviceRegistryKey` and `ZwSetValueKey` that `xhci98.sys`'s door and
  `xhciuas.sys` already use on 98 SE. No volatile key is assumed on 9x (the
  `nvme98` channel's reading: the `HKEY_DYN_DATA` root refused a static key
  on 98 SE), so the value persists across a restart and section 3.3's trust
  rule covers that. `ZwCreateKey` is in `ntkern.vxd`'s export name table on
  98 SE but is not needed there.
- **When**: from the connection-manager thread at `PASSIVE_LEVEL`, once
  per pass after the actions and the re-enumeration service (`cm_thr.c`),
  only when the content differs from the last record written (compared
  without the generation and the uptime), and once at the stop (stage
  stopped, no routers). On 98 SE and ME the first write waits until 2 s
  after the start's work finished, so no registry service is called while
  the NHI's own start may still be pending in the configuration manager
  (the rule of `cm_cfgmg.c`'s route). A write that fails is counted
  (`cm.status.fail`) and retried at the next change; it never affects the
  connection manager.
- **The files**: `src\cm\cm_status.c` builds the record from `TBT_ICM` or
  `TBT_U4CM` and the domain's set, pure and DDK-free, host-tested by
  `test\test_cm_status.c`; `src\cm\cm_statos.c` holds the publication
  (the key, the volatile subkey, the write, the 98 delay). The hooks are
  two calls in `cm_thr.c` (the pass and after the stop) and one field in
  `CM_NHI` (`cm.h`); nothing in `cm_nhi.c`, `tbt_icm.c`, `cm_reenum.c` or
  `cm_cfgwk.c` changes. The import allowlists gain `ZwCreateKey` and
  `ZwSetValueKey` rows with `SITES=cm_statos.obj` and `cm_statos.obj`
  joins `IoOpenDeviceRegistryKey`'s and `ZwClose`'s `SITES`; `ZwCreateKey`
  is NT-only at run time and its row says so.

### 3.3 How the tool reads it

- The record is trusted only for an NHI devnode that is present, started
  and bound to `xhcicm` (`NTMPDriver` on 9x, the `Service` on NT), and only
  when its magic, version and sizes check. On 98 and ME it is also
  refused when its uptime is greater than the system's now
  (`GetTickCount`): a record from an earlier boot. A record written at an
  earlier boot at a smaller uptime cannot be told apart, but the driver
  rewrites it within seconds of every start, so only a viewer run in those
  first seconds can meet one; the viewer prints the record's age.
- **Generation**: the tool reads the record, walks the Windows devices,
  and reads the record again; a different generation means a change
  during the walk, and the whole reading is taken again, at most three
  times, then shown with "devices were changing while this was read".
- No record: "The Thunderbolt driver has not published its device list
  (it is older than 3.0.0.0, not started, or still starting)." The PCI
  and Windows side is still shown.

## 4. Finding the devices

### 4.1 The tunnel ports

A tunnel port is a PCI-to-PCI bridge whose hardware ids include one of the
filter INF's model ids (`src\cm\xhcicmf.inf`: the Thunderbolt switch ports
and integrated tunnel root ports, and the emulator's `1B36:0001` and
`1B36:000C` bridges), compiled into the tool's pure half as a table. The
host suite reads `xhcicmf.inf` and fails if the table and its model lines
differ (section 10). The table is the candidate set; the status record's
tunnel ports, where present, are the verified ones (record 21 section
6.3), and the viewer marks which bridges are verified.

### 4.2 The devices behind them

The tool walks the whole devnode tree (98 and ME: `CM_Locate_DevNodeA`,
`CM_Get_Child`, `CM_Get_Sibling`, ids from `HKLM\Enum` as record 23
section 6.3 does; NT: the same routines, ids through
`CM_Get_DevNode_Registry_PropertyA`) keeping each devnode's parent. A
**tunnelled device** is a devnode below a tunnel port that is not itself a
tunnel port and not one of the host controller's own functions - an NHI
(class `0880` or `0C0340`, or bound to `xhcicm`) or a USB host controller
(class `0C03`) whose parent is a tunnel port of the host's own switch.
The viewer lists the outermost such devnode of each subtree as one
**ejectable unit** - the NVMe controller of an enclosure, or the upstream
port of a dock's switch with its subtree - and its descendants beneath it,
each with its description, first hardware id, driver (the `Service` on NT,
`NTMPDriver` or the class driver on 98 and ME) and problem code.

Which units are offered for eject:

- **98 SE and ME**: every unit. One that carries `DN_REMOVABLE` was marked by
  `xhcicm.sys` (record 21 section 7.4); one that does not (tunnelled before
  the driver's worker took its baseline, `XhciCmSafeEject` 0, or a mark that
  failed) is offered too, with the note "not marked removable by the
  Thunderbolt driver", because the query-remove protects it the same way.
- **2000 to 7**: every unit; the viewer says which ones Windows lists in
  Safely Remove Hardware (`CM_DRP_CAPABILITIES` has `CM_DEVCAP_REMOVABLE`)
  and which it does not (the drop-out case of section 1.1).

### 4.3 Routers to ports

A router is shown under the tunnel port its record entry names (section
3.1), with the units behind that port; a router whose port is unknown is
shown under its controller, and the units behind an unclaimed port under
"behind a Thunderbolt or USB4 port". The port's location comes from
`CM_DRP_BUSNUMBER` and `CM_DRP_ADDRESS` on NT. On 98 and ME a bridge's
instance id names it by its devfn path (`runs/run-39.md`, "98 SE's
instance ids"), which carries no bus number, so the tool matches a router
to a port there only when the controller has exactly one verified tunnel
port and exactly one candidate bridge with units under it; otherwise the
routers and the units are shown side by side under the controller. A
mismatch is shown, never guessed past.

## 5. Interplay

### 5.1 `xhcicm.sys` on 98 SE and ME (the worker of `cm_cfgwk.c`)

The tool's removal happens outside the connection manager. What the worker
does afterwards (`cm_cfgwk.c` on branch `p39-reenum`, read 2026-10-15):

- It makes CONFIGMG calls only for a request the connection-manager thread
  posts - a tunnel up, a tunnel down, a resync - so a removed child is not
  re-enumerated, and so not brought back, until the next tunnel change of
  that port. A re-enumeration of the same bridge for another reason
  (Device Manager's Refresh, another device's tunnel change on the same
  port) does bring it back, since the tunnel is still up and the function
  still answers on the bus; the tool says so.
- At the unplug the tunnel goes down and the worker removes the port's
  vanished children: the removed devnode is no longer there, so nothing is
  queried twice, and `Reenumerate_DevNode` finds the bus empty.
- **One thing to change in the worker** (reported, not changed here): the
  worker's set of known children forgets a devnode only when the worker
  itself removed it. After a user removal the stale handle stays known;
  CONFIGMG reuses a freed devnode's handle, so a re-plugged device that
  receives the same handle is taken as known and not marked `DN_REMOVABLE`.
  The tool still offers it (section 4.2), so nothing is lost here, but the
  mark is. The fix is to prune the known set to the bridge's present
  children at every pass that walks them. The legs read whether a re-plug
  after an eject is marked.
- Under the USB4 software connection manager, a removal the tool makes on
  98 or ME does not reach the per-port tunnel teardown (record 22 section
  10.4): the 9x half has no child filter to see it. The tunnel stays up
  until the unplug, as under the ICM, and the device is still stopped
  safely.

### 5.2 `xhcicm.sys` on the NT targets

`CM_Request_Device_EjectA` is the call Safely Remove Hardware makes, so the
child filter sees the same query-remove and remove: it posts the port's
eject (`CmRegChildRemoved`), and under the USB4 profile the per-port
teardown of record 22 section 10.4 runs as it does for the applet; under
the ICM the eject is logged and the tunnel left up (record 21 section 7.4).
A unit with no child filter (the drop-out case) is ejected all the same;
nothing reaches `xhcicm.sys`, and the tunnel stays up until the unplug.

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
the 9x eject should move into the driver (section 14). On NT `nvme98`
receives the query-remove and remove of any eject.

## 6. What `/devices` prints

Per controller:

```
Thunderbolt 3 controller: xHCI98 Thunderbolt 3 Host Interface
  PCI 8086:15BF, driver xhcicm, firmware connection manager, security user
  record generation 7, written 12 s ago

  Device 1: Lenovo ThinkPad Thunderbolt 3 Dock (id ...3a7c)
    Thunderbolt 3, link 1, depth 1, approved
    tunnels: PCIe up; USB3 -; DisplayPort -
    behind tunnel port PCI 02:01.0 (verified):
      [1] NVM Express Controller   PCI\VEN_1B36&DEV_0010   nvme98   working
          removable: yes (marked by the Thunderbolt driver)
```

On NT the removable line reads "listed in Safely Remove Hardware" or "not
listed in Safely Remove Hardware; XHCIINST /eject can still remove it".
A unit's descendants are indented beneath it with their own driver and
problem code. The numbers in brackets are the `/eject` numbers; they are
assigned in tree order on each run and checked again before the eject
(section 7.3).

## 7. The eject

### 7.1 Windows 98 SE and ME

1. `CM_Query_Remove_SubTree(unit, CM_QUERY_REMOVE_UI_NOT_OK)`. Windows'
   own dialogs are not shown; the tool says what happened.
2. Refused (`CR_REMOVE_VETOED` or any answer but `CR_SUCCESS`): "Windows
   refused: a program is using the device. Close it and try again." -
   with the code in the log - and nothing else is done. Exit 4.
3. Accepted: `CM_Remove_SubTree(unit, CM_REMOVE_UI_NOT_OK)` at once, always
   (an accepted query cannot be cancelled on 9x, and a driver left
   query-removed stays quiesced: section 5.3). Then "You can now unplug the
   device." Exit 0. A removal that fails after an accepted query is
   reported with its code - "Windows accepted the request but could not
   finish it. Unplug the device only after closing every program, or restart
   Windows." - exit 2.
4. The tool reads the unit's devnode again: gone, or still there with a
   problem, is reported.

`cfgmgr32.dll` on 98 SE (4.10.2222, 45,056 bytes) and ME (4.90.3000, 40,960
bytes) exports the same 179 names (read 2026-10-15 from each guest image's
`WINDOWS\SYSTEM\CFGMGR32.DLL` export table, static), among them
`CM_Query_Remove_SubTree`, `CM_Remove_SubTree`, `CM_Reenumerate_DevNode`,
`CM_Get_DevNode_Status` and the walk's routines, and neither exports
`CM_Request_Device_EjectA`. Both routines are looked up at run time; a
missing one refuses the eject (exit 1) with its name. Whether each does
what its name says on each system is the legs' reading.

### 7.2 Windows 2000, XP, Vista and 7

1. Rights: an administrator is needed (the tool's existing service manager
   test, record 23 section 7.2); without one: "Ejecting needs an
   administrator. Run it as an administrator." Exit 1. A
   `CR_ACCESS_DENIED` from the call says the same.
2. `CM_Request_Device_EjectA(unit, &vetoType, vetoName, MAX_PATH, 0)`,
   resolved from `cfgmgr32.dll`, else `setupapi.dll`: Windows 2000's
   `cfgmgr32.dll` does not export it and its `setupapi.dll` does (read
   2026-10-15 from the 2000 guest image's `system32` export tables,
   static); XP onward export it from `cfgmgr32.dll`.
3. Vetoed: "Windows refused: <reason>." with the vetoing device's name where
   Windows gives one ("a program is using the device" for an open handle,
   `PNP_VetoOutstandingOpen`; "the device's driver refused" for
   `PNP_VetoDevice` or `PNP_VetoDriver`, with the name; "Windows does not
   allow this device to be removed" for `PNP_VetoIllegalDeviceRequest` and
   `PNP_VetoNonDisableable`; otherwise the veto number), "Close it and try
   again." Exit 4.
4. Accepted: "You can now unplug the device." Exit 0. A unit with the child
   filter is then held by Windows as removed until the unplug (Code 47 on
   XP and later); one without it may come back at the next bus
   enumeration if it is not unplugged, which the message says ("Unplug it
   now: Windows may start it again if it stays connected"), and the legs
   read.

### 7.3 Common rules

- `/eject N` walks again and refuses a number that no longer names a unit,
  or names a different one than the listing the user saw (compared by
  instance id when the listing was in the same run, or by a fresh listing
  printed before the question): "The device list changed; nothing was
  ejected." Exit 1.
- One unit per run. No `/eject all`.
- Never a forced removal: no `CM_Remove_SubTree` without an accepted query,
  no `CM_Query_And_Remove_SubTree` with a veto override, no
  `DIF_REMOVE`.
- A unit is never a tunnel port, an NHI or a host controller of the host's
  own switch (section 4.2); `/eject` on such a number cannot occur because
  they are never numbered.

## 8. The command line and exit codes

### 8.1 Switches

| Switch | Meaning |
|---|---|
| `/devices` | the viewer (section 6); reads only, writes no log unless `/log` names one, no pause |
| `/eject` | the viewer, then a question: which number (Enter cancels), then the eject |
| `/eject N` | the eject of unit N, after showing what N is and one confirmation (Y) unless `/yes` |
| `/yes`, `/nopause`, `/log path` | as record 23 section 7.1 |

`/devices` and `/eject` take no package: they do not read the flavour
directory, so they skip record 23 section 4.2's package checks and run from
the local copy too. Either with an install switch (`/list`, `/defaults`,
`/set`, `/force`, `/restage`, `/uninstall`), with each other, or `/eject`
with a number that is not a positive decimal, is refused with exit 1
before anything is read. On 98 and ME `INSTALL.BAT`'s argument limit is
not reached by these.

### 8.2 Exit codes (record 23 section 7.4's table, read for these modes)

| Code | `/devices` | `/eject` |
|---|---|---|
| 0 | listed | ejected: unplug now |
| 1 | refused (bad switch, a routine missing, no rights on NT for the eject, the list changed, cancelled) | the same; nothing was changed |
| 2 | the device tree could not be read whole (listed as far as read) | the removal failed after Windows accepted the query (9x), or the call failed in a way that leaves the state unknown |
| 3 | - | - (an eject never needs a restart) |
| 4 | - | Windows refused (a veto): nothing changed, steps remain for the user (close the program, try again) |

The last line follows record 23 section 7.4: an eject that succeeded is a
change but needs no restart, so the line is "You can now unplug the
device." and not the restart line; a refusal or a viewer run ends "No
changes were made."

## 9. Messages

Plain, practical, in the voice of the install tool (owner's rule: no word
about virtual machines, emulators or what was tested):

- "No Thunderbolt or USB4 controller driven by this package was found."
- "Nothing is connected to the Thunderbolt or USB4 ports."
- "Windows refused: a program is using the device. Close it and try
  again."
- "You can now unplug the device."
- "Windows may start the device again if it stays connected; unplug it
  now." (NT, a unit without the child filter; 98 and ME, after an eject,
  with "until you unplug it, Device Manager's Refresh brings it back")
- "This device was connected before the Thunderbolt driver started, so
  Windows does not list it in Safely Remove Hardware. XHCIINST /eject can
  remove it safely." (NT, the drop-out case)

`README.TXT`, `INSTALL.BAT`'s comment, README's Thunderbolt section and
the release notes carry one line each: "To remove a Thunderbolt or USB4
device safely on Windows 98 SE and ME, run XHCIINST /eject (INSTALL
/eject); on Windows 2000 and later Safely Remove Hardware works, and
XHCIINST /eject also removes a device that Safely Remove does not list.
XHCIINST /devices shows what is connected."

## 10. The pure half and the host suites

In `xhciinst\inst_core.c` (host suite `test\test_inst.c`):

- the switch parser's new modes and their refusals;
- the tunnel-port id table and `inst_tb_port_id`, checked against
  `src\cm\xhcicmf.inf`'s model lines both ways;
- `inst_tb_classify`: a devnode's role (tunnel port, host function, unit,
  inside a unit, unrelated) from its ids, class, driver and its parent's
  role, over a mock tree (`INST_TREE_OPS` extended with the parent), the
  dock and the enclosure shapes, the host switch with its NHI and xHCI;
- the status record's decoder and its checks (magic, version, sizes, the
  generation compare, the uptime rule on 9x), against records built by
  the driver's own `cm_status.c` (the suite links it);
- the veto text per `PNP_VETO_TYPE`, the 9x refusal and success texts, the
  exit code per outcome;
- the value table rows of section 11 against `xhcicm.inf`'s defaults and
  `/set`'s accept and refuse.

In `src\cm\cm_status.c` (host suite `test\test_cm_status.c`): the record
from an ICM table and from a USB4 port set - names cut and filtered to
printable ASCII, the UUID cut to four hexadecimal digits and the key never
copied, the tunnel states, the port mapping, the change compare that
ignores the generation and the uptime, the entry limit.

`xhciinst\selftest.cmd` adds `/devices /list` and `/eject x` to the
refusals it checks.

## 11. Record 23's value table (owner, 2026-10-15)

Four `xhcicm.inf` values join record 23 section 8.1's table as never-asked
rows that `/set Name=Value` can set, each allowed set taken from
`cm_vals.c`'s rule table and records 21 section 8 and 22 section 12:

| Value | Allowed (`/set`) | Why not asked |
|---|---|---|
| `XhciCmReenum` | 0, 1 | 0 exists to diagnose a machine where the re-enumeration misbehaves |
| `XhciCmUsb3Tunnel` | 0, 1 | a USB4 diagnostic whose default is the point |
| `XhciCmLaneBonding` | 0, 1 | a USB4 diagnostic whose default is the point |
| `XhciCmPreOsCleanup` | 0, 1, 2 | an override of the tunnel preservation rule |

The deferred check of record 23 section 8.1 is done: `XhciCmMode` 0 to 3,
`XhciCmApprove` 0, 1 and `XhciCmSafeEject` 0, 1 match `cm_vals.c` and both
records, and the four above match too.

## 12. The legs

On development host A, overlays only, under the admission and lock rules
of the Phase 39 brief, the `tb3-alpine-ridge` profile of the emulator with
`nvme98` (its published `0.1.1.0`, read-only) behind the tunnel; recorded
in `runs/run-39.md` as a 39-A.8 follow-up.

| Leg | Systems | What it reads |
|---|---|---|
| D, viewer | 98 SE, 2000 (and XP or 7 where a slot allows) | the controller, the router with its names, approval and tunnels from the record, the unit behind the port with its driver; the record's generation moving at a re-plug |
| E1, eject idle | 98 SE, 2000 | the eject accepted, "You can now unplug the device.", the unit gone or held, then the unplug with no dialog and no fault |
| E2, refused | 98 SE, 2000 | a file held open on the NVMe volume: the refusal text, the unit still started and the volume still usable; the file closed, the eject accepted |
| E3, re-plug | 98 SE | after an eject and unplug, the device back at the next plug, its driver started, and whether it is marked `DN_REMOVABLE` (section 5.1) |
| E4, drop-out | 2000 | the device disabled and enabled in Device Manager: missing from Unplug or Eject Hardware, listed by the viewer as not listed there, ejected by the tool |
| E5, ME | ME, if an ME image with `xhcicm.sys` installed is available | the viewer and E1 |

## 13. Decisions taken in writing this

- **E1** The viewer and the eject are modes of the install tool, not a
  third executable (section 2).
- **E2** The driver publishes a status record per NHI, volatile on NT and
  a value of the hardware key on 9x, written from the connection-manager
  thread on change, with a generation (section 3).
- **E3** The UUID is shown as its last four hexadecimal digits; no key or
  approval material is ever published (section 3.1).
- **E4** On 98 SE and ME a unit not marked `DN_REMOVABLE` is still offered,
  with a note (section 4.2).
- **E5** On NT the eject is `CM_Request_Device_EjectA`, the applet's own
  route, for every unit, removable or not (section 7.2).
- **E6** One unit per run, never forced (section 7.3).
- **E7** The worker change of section 5.1 and the 9x per-port teardown
  under USB4 are reported to the connection-manager work, not made here.

## 14. Open

- Whether `CM_Query_Remove_SubTree` and `CM_Remove_SubTree` from a Win32
  program do on 98 SE and on ME what they do from a VxD (the legs).
- Whether `CM_Request_Device_EjectA` on a unit whose capabilities do not
  say `Removable` holds it removed until the unplug, or lets the next bus
  enumeration start it again (leg E4).
- The link speed is not kept by either connection manager; showing it is a
  driver change for a later revision.
- The `nvme98` removal handshake (its MD-5): when it exists, the 9x eject
  should be asked of the connection manager, which can run it, rather than
  made by the tool.
- Titan Ridge and the integrated hosts: the record's fields are filled from
  the same tables, untested beyond what records 21 and 22 say.

## 15. Files

| File | What |
|---|---|
| `src\cm\cm_status.h` | the record's layout, shared with the tool |
| `src\cm\cm_status.c` | the record built from the connection manager's state (pure) |
| `src\cm\cm_statos.c` | its publication: the key, the write, the 98 delay |
| `src\cm\cm_thr.c`, `cm.h`, `sources` | the two calls, the field, the source lines |
| `scripts\import-gate\xhcicm-imports*.allow` | the registry rows for `cm_statos.obj` |
| `test\test_cm_status.c`, `test\run-host-tests.cmd` | the record's host suite |
| `xhciinst\xhciinst.c`, `inst_core.c`, `inst_core.h` | the modes |
| `test\test_inst.c`, `xhciinst\selftest.cmd` | their checks |
| `xhciinst\README.TXT`, `INSTALL.BAT`, `README.md`, `docs\using\release-notes.md` | the lines of section 9 |
| design records 21 (section 7.4) and 23 (sections 7 and 8.1) | pointers here |
