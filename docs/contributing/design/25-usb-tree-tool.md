# Design Record 25 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Status: revision 1, 2026-10-16, draft for review. Written for the owner's
instructions of 2026-10-16: the command-line `INSTALL /devices` and
`INSTALL /eject` of design record 24 are not easy to use, so a separate
graphical tool shows every USB and Thunderbolt 3 / USB4 controller and
device with its details, ejects a tunnelled device as Windows' own Safely
Remove Hardware does, and sits in the notification area. It ships in
`3.0.0.0`, which the owner held for it, as roadmap Phase 41
(`roadmap-usb4.md`). Section 12 lists the decisions taken in writing it,
each an orchestrator decision for the owner to confirm unless it names the
owner; section 13 what is open.

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
  XHCIINST keeps `/devices` and `/eject` unchanged.
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
  eject and unsafe unplug on 2000 and later, the tooltip carries the last
  event on 98 SE and ME; it starts at login (owner).
- Runs as the logged-on user, with no administrator rights and no UAC
  prompt at start (owner); an action that needs rights asks for them then.
- A read-only query device in `xhcicm.sys` (section 4), so the tool reads a
  current status without administrator rights and gets the details the
  registry record lacks (link speed and width, liveness). `xhci98.sys` is
  not changed (decision T3).
- An application icon (owner): `xhcitr\xhcitr.ico`, drawn for this project
  (section 9).

### 1.3 Not in scope

- Approving, rejecting or authorising devices; security levels; firmware.
- Changing any driver setting from the tool (the registry values stay
  `tuning.md`'s, set by hand or by XHCIINST).
- Forcing a removal Windows vetoes, or stopping a device that is not a
  unit (record 24 section 4.2), including plain USB devices: Windows'
  own applet stops those on 2000 to 7, and on 98 SE and ME a USB device
  has never needed one with this driver (the storage class driver flushes
  on removal, as with Windows' own stack).
- Writing `LowerFilters` to repair a missing child filter (Planned
  EJRB.1); the tool shows the condition (record 24 section 4.3) and says
  what to do.

## 2. Where it lives

- Source: `xhcitr\` (new): `xhcitr.c` (window, tray, OS calls),
  `tr_core.c` / `tr_core.h` (pure: record and query parsing, descriptor
  decoding, the event diff, text), `xhcitr.rc`, `xhcitr.manifest`,
  `xhcitr.ico`, `build.cmd`, `selftest.cmd`, `check-imports.ps1`,
  `README.TXT`. It links `xhciinst\inst_core.c` for record 24's pure
  rules (unit selection, protection, eject answers and texts) rather than
  copying them.
- Package: `tools\xhcitr\XHCITR.EXE`, `XHCITR64.EXE`, `readme.txt`.
- Installed copy: `%windir%\XHCI98\XHCITR.EXE` (or `XHCITR64.EXE`),
  started at login from
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`, value
  `xHCI98 USB Tree`, data `"<path>" /tray` (section 8).
- Host tests: `test\test_tr.c`, run by `test\run-host-tests.cmd`.

## 3. The window

### 3.1 Layout

- Title "xHCI98 USB Tree". A resizable top-level window: tree view (left),
  read-only multi-line edit with both scroll bars (right), a splitter
  between them, a status bar ("N USB devices, M Thunderbolt/USB4
  devices"). Size, splitter position and the View check boxes persist
  per user under `HKCU\Software\xHCI98\XHCITR`.
- Menus:
  - **File**: Refresh (F5), Save Report... (writes the whole tree with
    every node's details as text, for a bug report), Exit.
  - **Edit**: Copy (the details pane, or the selection), Select All.
  - **View** (check boxes): Thunderbolt and USB4; USB controllers;
    Windows' own USB controllers (not xhci98's); Empty ports;
    Descriptors; Devices behind each tunnel; Auto Refresh.
  - **Action**: Safely Remove... (enabled on a unit or a node inside
    one), Properties (opens Device Manager's properties of the selected
    devnode where the system offers it: `DeviceProperties_RunDLL` on 2000
    to 7; disabled on 98 SE and ME).
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
  their bridges, Device Manager status and problem code.
- **Router / device**: kind, vendor and model names, IDs, generation,
  link speed and width where known (USB4: Gen 2 / Gen 3 / Gen 4, x1 / x2;
  TB3 under the ICM: "not reported by the firmware"), route, approval,
  each tunnel's state, the unit's devnodes, whether Windows lists it in
  Safely Remove (record 24 section 4.3) and the advice when not.
- **USB controller**: description, PCI ID, driver, `HCD<n>` name, root
  port count, problem code.
- **Hub**: hub descriptor (ports, power switching, compound, over-current),
  self/bus power, speed.
- **USB device**: port, connection status, speed (Low, Full, High,
  SuperSpeed, SuperSpeedPlus; the sublink speeds from the BOS
  SuperSpeedPlus capability when present), address, VID:PID, class,
  product and manufacturer strings, driver and driver key, problem code,
  current configuration, MaxPower; with Descriptors on, every descriptor
  decoded field by field, then in hex.
- Every node: the devnode instance ID, status flags and problem code.

### 3.4 Refresh

- F5, a device-change notification (`WM_DEVICECHANGE`,
  `DBT_DEVNODES_CHANGED`, coalesced 500 ms), or Auto Refresh (every 2 s
  for the Thunderbolt branch only) rebuilds the tree. The selection and
  expanded nodes are kept by key (instance ID, or controller + port path,
  or NHI + route).
- A refresh never sends a request to a device that the driver does not
  already answer from cache, except strings and the BOS when Descriptors
  is on and the node is selected (record 13 section 8.11).

## 4. The query device in `xhcicm.sys`

### 4.1 Why

The registry record (record 24 section 3) is readable by every user, but
only a fresh one, confirmed by an echo of a value written under
`HKLM\...\Control\Class`, is trusted for an eject, and that write needs an
administrator on NT. It also lacks link speed and width, deeper routers'
IDs and the liveness state. A device the tool opens with no access rights
and asks with a buffered, any-access IOCTL needs no rights on any target,
answers from the connection manager's own state at once, and is the only
route that works the same on 98 SE and on 7.

### 4.2 The device

- One named control device per started NHI, created by `xhcicm.sys` at
  start and deleted at stop and remove: `\Device\XhciCm<n>`, linked as
  `\DosDevices\XHCICM<n>`, `n` the first free of 0..15. `n` is written as
  `XhciCmQueryName` (REG_SZ `XHCICM<n>`) in the NHI's hardware key beside
  `XhciCmStatus`, so the tool maps an NHI devnode to its device.
- Not part of the PnP stack (a control device object), so an open never
  reaches the NHI's lower drivers; `FILE_DEVICE_SECURE_OPEN` on NT.
- `IRP_MJ_CREATE` and `IRP_MJ_CLOSE` succeed; `IRP_MJ_DEVICE_CONTROL`
  answers one code; everything else fails with
  `STATUS_INVALID_DEVICE_REQUEST`.

### 4.3 `IOCTL_XHCICM_QUERY`

`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS)`,
no input; the output is `XHCICM_QUERY` (`src\cm\cm_query.h`, Windows-type
free, shared by the driver, the tool and the host tests):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `'XCMQ'` |
| 4 | 4 | version, 1 |
| 8 | 4 | size of the whole answer |
| 12 | 4 | connection-manager pass counter |
| 16 | 4 | milliseconds since the last completed pass |
| 20 | 4 | liveness state (`CM_LIVE_*`) |
| 24 | 4 | flags: bit 0 thread inert, bit 1 record truncated |
| 28 | 4 | offset of the status record |
| 32 | 4 | offset of the router extensions |
| 36 | 4 | router extension count and entry size (16 bits each) |
| 40 | 24 | reserved, 0 |
| 64 | 128 + 128 x n | the status record, byte for byte as `CmStatusBuild` builds it (record 24 section 3.1) |
| ... | 32 x n | one extension per router entry, same order |

Router extension (32 bytes): link rate (0 unknown, 10 / 20 / 40 Gbit/s
per lane), lane width (0 unknown, 1, 2), the adapter's link state, the
lane's negotiated generation code as read, the router's TB vendor and
device IDs and the DROM vendor/model where known for deeper routers, and
reserved bytes. The connection manager fills these when it reads them on
its own path (the USB4 CM reads the lane speed and width when it sets up
a USB3 tunnel's bandwidth, `tbt_u4cm.c`); the query copies a cache under
the connection manager's lock and never touches hardware. "Unknown" is a
valid answer and the tool prints it as such.

- **Freshness.** The answer is the state as of the last completed pass of
  the connection-manager thread (200 ms while active, 1 s while the
  transport is down) and says how old that pass is. The tool treats an
  answer whose pass is under 3 s old, or whose inert flag is set, as
  confirmed for record 24's rules (section 6); that replaces the ask and
  echo for this tool only.
- **Size.** The tool asks with a 4 KiB buffer; if the size field says
  more, it asks again with that size.
- **IRQL and locking.** Served at `PASSIVE_LEVEL` in the caller's thread;
  takes the connection manager's existing state lock for the copy only.

### 4.4 What does not change

The registry record (`XhciCmStatus`, version 1), the ask and echo, and
XHCIINST's use of them stay exactly as record 24 has them.

## 5. The USB branch

- Controllers: `\\.\HCD0` to `\\.\HCD63` on every target (usbview's own
  route reaches only HCD0 to HCD9, and xhci98 can take a higher number,
  record 13 section 8.1), opened with no access rights (all the IOCTLs
  below are `FILE_ANY_ACCESS`); on 2000 to 7 also every interface of
  `GUID_DEVINTERFACE_USB_HOST_CONTROLLER`, de-duplicated by driver key.
- `IOCTL_GET_HCD_DRIVERKEY_NAME` (the controller's devnode, so its
  description and whether xhci98 drives it), `IOCTL_USB_GET_ROOT_HUB_NAME`,
  then per hub `IOCTL_USB_GET_NODE_INFORMATION` and per port
  `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX`, `_EX_V2` (SuperSpeed
  and SuperSpeedPlus flags; xhci98 reports speed 2 in `_EX` on NT, record
  13 section 8.4), `_DRIVERKEY_NAME`, `_NAME` for hubs, and
  `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` for the configuration,
  strings and BOS.
- The structures the MSVC 6.0 headers lack (`_EX`, `_EX_V2`, the
  descriptor request) are declared in `tr_core.h` from their documented
  layouts.
- A controller that is not xhci98's is shown with whatever its driver
  answers; an IOCTL it refuses leaves the field "not reported".

## 6. Eject

- **What can be ejected:** exactly record 24's units (section 4.2 there):
  a devnode directly behind an active, verified tunnel port of a confirmed
  status, not protected (NHIs, their ancestors, and the USB controllers
  that share the NHI's grandparent). The rules are `inst_core.c`'s
  (`inst_tb_units`, `inst_tb_protect`, `inst_tb_unit_blocked`); the tool
  feeds them from the query (section 4) instead of the registry record.
- **The call:** record 24 section 7: `CM_Request_Device_EjectA` on NT
  (setupapi on 2000), with the not-removable fallback
  `CM_Query_And_Remove_SubTreeA`; on 98 SE and ME
  `CM_Query_Remove_SubTree` then, if accepted, always
  `CM_Remove_SubTree`, both `UI_NOT_OK`. Never forced, never `DIF_REMOVE`,
  one unit per action, revalidated (a fresh query and walk) immediately
  before the call, under the same named mutex XHCIINST takes
  (`XhciinstTbAsk`) so the two never interleave.
- **Confirmation:** a dialog like Windows' "Stop a Hardware device": the
  unit and the devnodes that will stop, OK / Cancel.
- **Answers:** record 24 section 5.4's texts, from `inst_core.c`
  (`inst_eject_text`, the unplug advice), in a message box from the
  window or a balloon (2000 and later) from the tray; on a veto, the
  vetoing device and the reason.
- **Rights:** on NT, `CM_Request_Device_EjectA` needs no administrator
  for a removable device (Windows' applet runs as the user). If it
  answers with a rights failure, the tool says so and offers to run the
  eject elevated: it starts itself with `runas` (Vista and 7) and
  `/eject id=<instance id>`, which revalidates and ejects that one unit
  and exits (section 7.3).

## 7. The tray icon

### 7.1 Behaviour

- `XHCITR /tray` starts with only the icon; `XHCITR` with no argument
  opens the window and also keeps the icon. One instance per user
  session (a named mutex `Local\Xhci98TrTray` on 2000 and later,
  `Xhci98TrTray` on 98 SE and ME); a second start hands its request to
  the first (a registered window message) and exits.
- **Left click**: a menu "Safely remove <unit>" per unit (grayed with the
  reason when it cannot be stopped now), a separator, "Open xHCI98 USB
  Tree". **Double click**: the window. **Right click**: Open, Refresh,
  About, Exit.
- **Tooltip**: "xHCI98 USB Tree: N Thunderbolt/USB4 devices" and, on 98
  SE and ME, the last event.
- The icon is re-added on `TaskbarCreated` (Explorer restart).

### 7.2 Events

The tray polls the query of every NHI every 1 s (one IOCTL each, no
device traffic) and diffs the router entries by NHI + route (pure,
`tr_core.c`):

| Event | Balloon (2000 and later) |
|---|---|
| a router entry appears | "<vendor> <model> connected (Thunderbolt 3 / USB4)." |
| approval becomes pending, unanswered or held | "<name> is waiting to be approved." |
| approval becomes failed | "<name> could not be approved." |
| a unit this tool ejected leaves | "You can now unplug <name>." (record 24 section 5.4's qualified text where it applies) |
| a router whose unit was started leaves without a stop | "<name> was unplugged without being stopped first." |
| the liveness state leaves ACTIVE | "The Thunderbolt/USB4 controller stopped answering." |

Balloons use `NOTIFYICONDATA` version 5 (`NIF_INFO`) when `shell32.dll`
reports version 5 or later (2000 and later); otherwise version 1 and the
tooltip. The structure is declared in `tr_core.h` for the MSVC 6.0 build.

### 7.3 Command line

`XHCITR [/tray] [/eject id=<instance id>] [/report <file>]`. `/report`
writes the Save Report text and exits (for scripts and bug reports).
Exit codes follow XHCIINST's (record 23 section 10).

## 8. Install, upgrade and uninstall

- XHCIINST copies the tool for the machine's architecture to
  `%windir%\XHCI98\XHCITR.EXE` (`XHCITR64.EXE` on x64) on every
  changing run and writes the Run value (section 2). `/notray` skips the
  Run value; there is no new question (owner: start at login).
- Before replacing or deleting it, XHCIINST closes a running copy: it
  finds the tray's hidden window by class (`Xhci98TrTray`), posts
  `WM_CLOSE`, and waits up to 5 s.
- Uninstall closes it, deletes the Run value and the file; a file that
  cannot be deleted on NT is scheduled with `MoveFileEx` delay-until-
  reboot, on 98 SE and ME through `WININIT.INI`, and reported.
- The packager carries both executables under `tools\xhcitr\` with
  XHCIINST's freshness and version checks; `test-package.ps1` checks the
  layout.

## 9. Build, imports, manifest and icon

- x86: MSVC 6.0, `/SUBSYSTEM:WINDOWS,4.0`; x64: WDK 7.1,
  `/SUBSYSTEM:WINDOWS,5.02 /MACHINE:X64`. ANSI C89 as XHCIINST.
- Static imports: KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, COMCTL32,
  COMDLG32 (all present on 98 SE); setupapi and cfgmgr32 resolved at run
  time as XHCIINST does. `xhcitr\check-imports.ps1` allows exactly those.
- Manifest: `asInvoker`, the Common Controls 6 dependency (XP and later
  draw themed controls; 98 SE to 2000 ignore the manifest).
- Icon `xhcitr.ico`, drawn for this project: a controller box with a
  tree line to two devices and an eject arrow, in the 16 VGA colours with
  no partial transparency, at 16, 32 and 48 pixels, 4 and 32 bits per
  pixel, so it reads the same on 98 SE's grey taskbar, XP's blue and 7's
  dark one.
- Version resource from `src\xhci_version.h`.

## 10. The pure half and its host tests

`tr_core.c` holds no Windows call: query and record parsing and checking,
the router extension decode, descriptor decoding to text (device,
configuration, interface, endpoint, SuperSpeed companion, BOS with USB 2.0
extension, SuperSpeed and SuperSpeedPlus capabilities, strings), speed
naming, the tray event diff, tree keys, report formatting. `test\test_tr.c`
covers each with golden vectors, including truncated and malformed
descriptors and a query whose size disagrees with its counts. The
driver's query builder is pure too (`cm_query.c`, host-tested with
`test_cm`).

## 11. Legs

| Leg | Systems | What it reads |
|---|---|---|
| T.1 | 98 SE, ME, 2000, XP, XP x64, Vista x86/x64, 7 x86/x64 | the tool starts as a normal user (no UAC prompt), the window shows the USB tree with speeds and descriptors |
| T.2 | the same, with the TB3 and USB4 emulator devices | the Thunderbolt branch, the NVMe unit behind its tunnel, link details |
| T.3 | 98 SE, 2000, XP, 7 x64 | Safely Remove from the window and from the tray; the answer text; the unit gone; re-plug |
| T.4 | 2000, XP, 7 | balloons on plug, approval, eject, unsafe unplug; 98 SE tooltip |
| T.5 | 98 SE, 7 x64 | install puts the tool and the Run value; login starts the tray; uninstall closes it and removes both |
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
  section 8) give everything the USB branch needs, with SuperSpeedPlus
  from `_EX_V2` and the sublink speeds from the BOS; a change to the HCD
  this close to the cut would reopen its whole regression for a detail
  (Gen 2x1 against Gen 1x2) the BOS already answers.
- **T4** No new install question; `/notray` opts out of the Run value.
- **T5** The tool links `inst_core.c` for record 24's rules and texts, and
  shares XHCIINST's mutex, rather than copying them.
- **T6** Plain USB devices get no eject in the tool (section 1.3).
- **T7** The query's freshness (a pass under 3 s old) replaces the ask and
  echo for this tool's eject.

## 13. Open

- Whether every target lets an unelevated user open `\\.\HCD<n>` and
  `\\.\XHCICM<n>` with no access rights (record 13 section 8.10 left this
  open for the hub names); T.1 reads it.
- Whether the ICM's firmware messages carry a link speed the client could
  keep; until read, "not reported by the firmware".
- 98 SE's `\\.\` resolution of the new `\DosDevices\XHCICM<n>` names
  (xhci98's `HCD<n>` names resolve there, record 13 section 8.2).
