# Design Record 25 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Status: revision 13, 2026-10-16 (revision 13, the drivers' logging
level, below; revision 11, the not-removable unit's stop). Revision 10 adds a per-user "Start at login" choice
(section 7.4, the owner's request of 2026-10-16), with an addendum on the
tree's labels (end of section 7.4). Revision 7 adds Safely Remove for plain USB
devices (section 6.5), by the owner's decision of 2026-10-16 that reverses
decision T6; sections 1.3, 3.1, 7.1, 7.3, 11, 12 and 14 change with it.
Revision 6 was reviewed and revised five times the night before; the code
is written against both. Revision 8, the same day, adds the install
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

Revision 11, 2026-10-16 (the owner, for `3.0.0.0`): a Thunderbolt unit
that Windows does not mark removable is stopped by record 24 section
7.2's query-and-remove route (its revision 5), and the tool offers the
administrator rights that route needs when it has none (section 6.6):
"Stop as administrator..." starts the tool elevated for that one stop,
and only then. Sections 3.3, 6.2, 6.4, 7.3 and 12 change with it, and
section 3.3 words a Thunderbolt 3 router's link behind a USB4 host as
Thunderbolt 3. The tray and the window never run elevated otherwise, and
the manifest stays `asInvoker`.

Revision 13, 2026-10-16 (the owner, for `3.0.0.0`): a "Logging level"
item shows and sets both drivers' log levels - `xhci98.sys`'s
`XhciLogVerbosity` and `xhcicm.sys`'s `XhciCmLogVerbosity` - and
replaces `XHCISNAP -verbosity` and `SNAPON.BAT` (XHCISNAP still ships in
`3.0.0.0`, unchanged, and is retired in the next release). It is the one
setting the tool writes (section 7.5). A standard user on 2000 to 7 sets
it through a second mode of section 6.6's elevated run, `/setlevel`.
Sections 1.2, 1.3, 7.3, 11 and 12 change with it. (Revision 12, the PCI
configuration, is written on its own branch.)

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
- Writing `LowerFilters` to repair a missing child filter (Planned
  EJRB.1); the tool shows the condition (record 24 section 4.3) and says
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
    node's details as text, for a bug report), Exit.
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
  `DEVICE_CONTROL` answers `CM_Q_IOCTL` alone, everything else fails with
  `STATUS_INVALID_DEVICE_REQUEST`, and PnP and power never reach it.
- **Name.** `\Device\XhciCm<n>`, linked as `\DosDevices\XHCICM<n>`, `n`
  the first free of 0..15, created in the start IRP's system context (so
  the link is global on XP and later), with `FILE_DEVICE_SECURE_OPEN` on
  NT and the system's default security. The name is published as
  `XhciCmQueryName` (REG_SZ) in the NHI's hardware key after the link
  exists and deleted before the link goes. The tool trusts the mapping
  only when the answer's instance ID equals the devnode it read it from,
  so a stale or recycled name never stands for another NHI.
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
XHCIINST's use of them stay exactly as record 24 has them.

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
   request and the dialog follows it). Nothing below waits on the user.
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
   both `UI_NOT_OK`. Never forced, never `DIF_REMOVE`, one unit at a
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

XHCIINST needs nothing new: it runs as an administrator (record 24 section
7.2).

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
them. Everything else connected gets details only, with the reason ("a
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
it]" (Code 47, held for eject, after an eject on XP and later); one with
another problem code is shown with the code, and is not offered.

**What it is called.** Its disks' names and their drive letters, as units
are (section 14, "A unit named by its drive" in run 41): "USB Flash Disk
(F:)"; without a disk, the device's own name. The letters come from the
same code: on NT the storage device number of each disk's interface
against each `\\.\X:`, on 98 SE and ME the disk's
`CurrentDriveLetterAssignment`.

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
     (Code 47) - "You can now unplug the device."; the device started
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
`/eject id=` names a unit or a USB device (section 6.5). Since revision
11 `/eject` also takes `/result <name>` and `/confirmed <digest>`, which
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

### 7.5 The drivers' logging levels (revision 13)

The owner's decision of 2026-10-16: the tool shows and sets the level of
each driver's log, in place of `XHCISNAP -verbosity` and `SNAPON.BAT`.

1. **Where.** "Logging level..." in the View menu, and at the end of the
   right-click menu of an `xhci98.sys` controller and of an NHI. A
   "Driver log" node (on its own branch) opens the same dialog: the entry
   point is `level_dialog(owner, driver)`, and `lv_driver_of_node` names a
   node's driver, so a Driver log node is one more case there, answering
   its parent's driver.
2. **What is read.** xhci98: the REG_DWORD `XhciLogVerbosity` in the
   driver (software) key of every devnode the walk finds with service
   `xhci98` and a `PCI\` instance ID - every controller, not a root hub
   or a hub. xhcicm: `XhciCmLogVerbosity` in every NHI's driver key
   (service `xhcicm`, `PCI\`); for one release, when that name is absent,
   `XhciLogVerbosity` in the same key, and the tool says which name it
   found. No value reads as the default, 2; a DWORD outside 0 to 4 is
   shown as it is, "out of range"; a value of another type is shown as
   not a REG_DWORD. The decision is `tr_level_pick`, host-tested.
3. **The dialog.** A list of each device and its reading (the same block
   as the report's), then the two drivers side by side, each with five
   radio buttons, one line per level: 0 off; 1 counters only; 2 the log
   (default); 3 adds port status; 4 adds memory addresses (for
   developers). A driver's buttons start at the level every one of its
   devices reads, and none is checked when they differ or a reading is
   out of range. Set writes a driver's level to every one of its devices
   when the checked level is not what they all read, or a device read it
   from xhcicm's old name, or a key's old name is there and differs from
   its new name's DWORD; and nothing else.
4. **The write.** `REG_DWORD` 0 to 4 under the value's name, read back;
   a key that still holds xhcicm's old name gets the level under it too,
   so a driver of either generation follows. A driver key is written only
   when it has the configuration manager's form (`tr_driver_key_ok`), and
   a value out of range is never written: the command line refuses it
   before anything is read.
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

## 11. Legs

| Leg | Systems | What it reads |
|---|---|---|
| T.1 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64 | the tool starts as a normal user (no UAC prompt), opens `\\.\HCD<n>` and `\\.\XHCICM<n>` with no rights, the window shows the USB tree with speeds and descriptors |
| T.2 | the same, with the TB3 and USB4 emulator devices | the Thunderbolt branch, the NVMe unit behind its tunnel, link details |
| T.3 | 98 SE, 2000, XP, 7 x64 | Safely Remove from the window and from the tray; the answer text; the unit gone; re-plug |
| T.4 | 2000, ME, XP, 7 | balloons on plug, approval, eject, disconnection; 98 SE tooltip |
| T.5 | 98 SE, 7 x64 | install puts the tool and the Run value; login starts the tray; upgrade with the tray running; uninstall closes it and removes both |
| T.6 | every target | the record 24 legs of XHCIINST still pass with the changed `xhcicm.sys` |
| T.10 | 98 SE, 7 x86 | revision 13, 7 x86 as a standard user: each driver's level set to 3 (on 7 through "Set as administrator..."), the restart, the registry and the dialog read 3 for each; then set back to 2 |
| T.7 | 98 SE, ME, XP, 7 x86 | revision 7, as a standard user: a Bulk-Only stick at a USB 3 and a USB 2 port and a UAS disk (98 SE, XP, 7) - the label with its letter, Safely Remove from the window, a right click, Delete and the tray, the result, the device gone or held, unplug and re-plug with the drive back, a refusal with a file open on it; a tablet, a keyboard, a hub and a device with no storage never offered |

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
  whole regression for that one detail.
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

- **T19** (owner, 2026-10-16, revision 13) The tool sets the two
  drivers' logging levels and nothing else (section 7.5). A standard user
  on NT does it through a second mode of decision T12's elevated run,
  whose arguments are checked whole before anything is read; the
  elevated run finds the keys itself. (T13 to T18 are revisions 9 and
  12's.)

## 13. Open

- Revision 7: whether a USB disk on 98 SE (NUSB's `usbstor.sys` and
  `USBNTMAP`, or `xhciuas.sys`) and on ME (its own `USBSTOR`) is a devnode
  below the USB device in the configuration manager's tree, as an
  `nvme98` disk is below its controller; whether a query-remove of the USB
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
- **An inert start's first answer** can lack `CM_Q_F_INERT` for the moment
  between the device's creation and the thread's first pass. An eject
  refuses it anyway (the record's stage is inert); planned work.
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
- **Code 47 after an eject** (XP and 7): the disks go with the eject, so a
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
