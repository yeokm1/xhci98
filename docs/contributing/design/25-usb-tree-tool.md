# Design Record 25 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Status: revision 2, 2026-10-16, reviewed and revised once the same night;
the code is written against it. Written for the owner's instructions of
2026-10-16: the command-line `INSTALL /devices` and `INSTALL /eject` of
design record 24 are not easy to use, so a separate graphical tool shows
every USB and Thunderbolt 3 / USB4 controller and device with its details,
ejects a tunnelled device as Windows' own Safely Remove Hardware does, and
sits in the notification area. It ships in `3.0.0.0`, which the owner held
for it, as roadmap Phase 41 (`roadmap-usb4.md`). Section 12 lists the
decisions taken in writing it, each an orchestrator decision for the owner
to confirm unless it names the owner; section 13 what is open.

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

### 1.3 Not in scope

- Approving, rejecting or authorising devices; security levels; firmware.
- Changing any driver setting from the tool (the registry values stay
  `tuning.md`'s, set by hand or by XHCIINST).
- Forcing a removal Windows vetoes, or stopping a device that is not a
  unit (record 24 section 4.2). Plain USB devices get details only: on
  2000 to 7 Windows' own applet stops them; on 98 SE and ME the advice
  stays what the release notes give (close the files, wait for the
  activity to end, unplug).
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
- Installed copy: `%windir%\XHCI98\XHCITR.EXE` (or `XHCITR64.EXE`),
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
    one), Properties (Device Manager's properties of the selected devnode
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
  4.3) and the advice when not.
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
- A refresh sends a device no request the driver does not answer from
  cache, except strings and the BOS when Descriptors is on and the node
  is selected (record 13 section 8.11).

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
  then counts the pass and stamps its time from a running clock. It does
  not depend on the registry write succeeding. The IOCTL copies out under
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
2. Take the shared eject mutex (section 6.3). Query (Q1). Refuse unless the record's stage is active, the inert and
   stopping flags are clear and the liveness state is ACTIVE.
3. Wait, polling every 100 ms for at most 3 s, until the published-pass
   counter moves past Q1's with the same epoch (Q2): an observation the
   connection manager completed after the eject began. If none comes,
   refuse ("the controller is not answering"). This replaces record 24's
   ask and echo for this tool: like the echo, it proves a pass after the
   request rather than a recent one.
4. Walk the devnodes completely; query again (Q3). Q3 must have Q2's
   epoch and record generation; otherwise walk once more, then refuse.
5. The selected unit must still be a unit, by instance ID, with the same
   devnodes under it as the user confirmed; otherwise refuse and show the
   new state.
6. The call, record 24 section 7: `CM_Request_Device_EjectA` on NT
   (setupapi on 2000), with the not-removable fallback
   `CM_Query_And_Remove_SubTreeA`; on 98 SE and ME
   `CM_Query_Remove_SubTree` then, if accepted, always `CM_Remove_SubTree`,
   both `UI_NOT_OK`. Never forced, never `DIF_REMOVE`, one unit at a
   time.
7. The post-eject assessment ported from XHCIINST (the restarted-unit
   check, the USB4 tunnel watch, the unplug advice of record 24 section
   5.4 with every fact it needs), shown as a message box from the window
   or a balloon (tooltip on 98 SE) from the tray; on a veto, the vetoing
   device and the reason.
8. Release the mutex.

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
whole transaction itself and exits.

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
  reason when it cannot be stopped now), a separator, "Open xHCI98 USB
  Tree". **Double click**: the window. **Right click**: Open, Refresh,
  About, Exit.
- **Tooltip**: "xHCI98 USB Tree: N Thunderbolt/USB4 devices" and, where
  the shell has no balloons, the last event (at most 63 characters).
- A hidden top-level window (class `Xhci98TrTray`) owns the icon, so the
  `TaskbarCreated` broadcast reaches it and the icon is re-added after an
  Explorer restart; `WM_CLOSE` to it ends the program cleanly.
- **Quit event** (for the installer, section 8): a manual-reset event
  `Global\Xhci98TrQuit` on NT, `Xhci98TrQuit` on 98 SE and ME. The tray
  waits on it in its message loop; when it is set, the tray finishes an
  eject transaction in progress, removes the icon and exits. A tray that
  starts while it is set waits up to 20 s for it to clear before it
  shows the icon.

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
| approval becomes pending, unanswered or held | "<name> is waiting to be approved." |
| approval becomes failed | "<name> could not be approved." |
| a router entry leaves | "<name> was disconnected." |
| a USB4 router leaves whose PCIe tunnel was up and whose unit was started at the last sample | "<name> was unplugged while in use. Stop it with Safely Remove first next time." |
| the liveness state leaves ACTIVE | "The Thunderbolt/USB4 controller stopped answering." |

These are descriptive: a 1 s sample cannot prove how a device left (the
ICM keeps a tunnel up after an eject until the unplug, record 24 section
5.2, and another program can stop a unit between samples), so only the
USB4 case above, where the connection manager tears the PCIe tunnel down
on every eject, says "while in use". The outcome of an eject comes from
the transaction (section 6.2 step 7), not from this diff.

**Balloons.** Where `shell32.dll` is version 5.0 or later (2000, ME, XP
and later) the tray uses the version 2 `NOTIFYICONDATAA` size with
`NIF_INFO` balloons and `NIM_SETVERSION` 3; on 98 SE (4.72) the version 1
size and the tooltip. The structures are declared in `tr_core.h` for the
MSVC 6.0 build with size checks (88 and 488 bytes, ANSI, x86); the x64
build uses the WDK's header.

### 7.3 Command line

`XHCITR [/tray] [/eject id=<instance id>] [/report <file>]`. `/report`
writes the Save Report text and exits (for scripts and bug reports).
Exit codes follow XHCIINST's (record 23 section 10).

## 8. Install, upgrade and uninstall

- XHCIINST owns the tool's file (and its `.NEW` and `.DEL` forms) and the
  Run value, outside the per-package `XHCIINST.LST` lists; its removal of
  `%windir%\XHCI98\` counts them as its own.
- **Copy.** On an install or upgrade run, the tool for the machine's
  architecture goes to `%windir%\XHCI98\XHCITR.EXE` (`XHCITR64.EXE` on
  x64) when it is missing or its file version is older than the
  package's; an equal version is left alone. The Run value is written
  only after the file is in place. `/notray` writes no Run value and
  removes an existing one. There is no new question (owner: start at
  login).
- **Closing running copies.** Before replacing or deleting the file,
  XHCIINST sets the quit event (section 7.1), which every session's tray
  sees, and posts `WM_CLOSE` to the `Xhci98TrTray` window in its own
  session, then waits up to 15 s for the file to become replaceable, then
  clears the event. It never ends a process.
- **Still in use.** NT: the new file is copied beside it as `XHCITR.NEW`
  and moved over it at the next restart (`MoveFileEx`, replace, delay
  until reboot); 98 SE and ME: a `[rename]` line in `WININIT.INI` with
  8.3 paths, every existing line kept. XHCIINST says the new tool takes
  effect after a restart.
- **Uninstall.** Closes it as above, deletes the Run value and the file;
  a file still in use is renamed to `XHCITR.DEL` and deleted at the next
  restart on NT, or deleted by a `NUL=` line in `WININIT.INI` on 98 SE
  and ME. An install that finds its own pending `NUL=` line for the tool
  removes that line before copying.
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
- **T6** Plain USB devices get no eject in the tool (section 1.3).
- **T7** An eject waits for a connection-manager pass completed after it
  began (section 6.2), in place of record 24's ask and echo; the pass's
  age only marks the display current or not.
- **T8** The tray's balloons from polling are descriptive; only an eject
  transaction reports an eject's outcome (section 7.2).

## 13. Open

- Whether every target lets an unelevated user open `\\.\HCD<n>` and
  `\\.\XHCICM<n>` with no access rights (record 13 section 8.10 left this
  open for the hub names); T.1 reads it.
- 98 SE's and ME's `\\.\` resolution of `\DosDevices\XHCICM<n>` (xhci98's
  `HCD<n>` names resolve there, record 13 section 8.2), and what NTKERN
  returns to `DeviceIoControl` for the removed-device answer.
- Whether the ICM's firmware messages carry a link rate the client could
  keep; until read, "not reported by the firmware".
