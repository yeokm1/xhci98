# Real speeds on root ports: a virtual USB 2.0 hub for each slower device on a root port

Written 2026-09-07; revised 2026-09-25, when the owner chose the on-demand
shape over the permanent one this page first proposed (section 4). **Taken
up the same day as roadmap task 24.3**, whose first sub-task, 24.3.1, moves
this page to `docs/contributing/design/12-virtual-hub-on-root-ports.md`;
until that move it stays here and is still worded as an idea. Nothing in it
has been built and no boot has been taken for it. Where it says what a
batch would do, it is naming work, not reporting it. The evidence it rests
on is the tree as it stands, issue 6
(`docs/issues/06-full-speed-root-port-bugcheck.md`), design record 02
(`docs/contributing/design/02-hub-topology-route-string.md`) and the
root-hub contracts in `docs/usb-xhci-info/usbport-miniport-abi.md`
section 4.

Two rules frame everything below, the same two the SuperSpeed storage
proposal was written under:

1. The virtual hubs are gated behind a registry value and **off by
   default**.
2. With the value absent or 0, the driver behaves **exactly as it does
   today**. Not "equivalently": the same root-hub reports, the same
   override, the same counters, the same trace lines, the same bytes on
   the bus.

Section 3.1 says what the value is, how it is read, and what holds rule 2.

## 1. The problem it would solve

The driver reports every connected root port to `usbport.sys` as High Speed
whatever the device's decoded speed, and keeps the true speed for its own
Slot and Endpoint Contexts. Issue 6 says why: usbport applies the EHCI model,
in which a root port can only hold a High-Speed device, and when told a
root-port device is Full or Low Speed it looks up the transaction translator
that model guarantees. The root hub has none, `USBPORT_GetTt` turns the empty
list into a garbage pointer, and both shipping builds bugcheck. The override
in `XhciPortShadowReport` (`src/xhci_port.c`) is the one lever there is, and
it costs three things:

- **The interrupt interval.** usbport turns `bInterval` into the `Period` it
  hands the miniport using the speed it believes, and for a believed
  High-Speed device that is `2^(bInterval-1)` microframes capped at 32. A
  Full or Low Speed device on a root port is bucketed on those rules, the
  driver then floors it at the 1 ms the xHCI specification allows at those
  speeds, and the result is three bands: `bInterval` 1 to 4 gives 1 ms, 5
  gives 2 ms, 6 and above gives 4 ms. A stock mouse at `bInterval` 10 runs
  at 4 ms. The same mouse behind a Full-Speed hub arrives at its true speed
  and gets 8 ms, the value it asked for.
- **The isochronous cadence.** usbport stamps a believed High-Speed
  isochronous endpoint's packets one per microframe whatever the descriptor
  says, and the driver recovers the true cadence from a snooped
  `GET_DESCRIPTOR(Configuration)` (`src/xhci_desc.c`). A Full-Speed audio
  device on a root port works on Windows 98 and 2000, but on a derived
  cadence with a counter recording that usbport and the descriptor
  disagreed; from Windows XP on it plays nothing (issue 6 section 7).
- **Everything usbport shows and budgets.** Device Manager reports every
  root-port device as High Speed, and usbport budgets it against the
  High-Speed bus rather than a frame budget. The bandwidth half has no
  measurement either way.

Issue 6 section 8 lists a true-speed report as an opt-in and says why it
cannot be enabled under any shipping usbport: the fault is in usbport's list
handling, and SweetLow's rebuild has the same branch - read statically, and
on 2026-09-25 run: a truthful build took Windows 98 SE under his stack down
at NUSB's `NTKERN` address (roadmap 24.3, `runs/run-24.md`). The lever has two
positions and both have been taken. This page proposes a third: keep
reporting High Speed on every root port, and give usbport the hub its model
is looking for, only where a device needs one.

## 2. The idea

When a root-port reset on a managed USB 2.0 protocol port decodes a Full or
Low Speed device, the miniport presents, in that device's place, a USB 2.0
hub that does not exist on the bus: High Speed, one downstream port, one
transaction translator, `bDeviceProtocol` 1. The real device becomes that
hub's port 1. When the device is unplugged, the root port reports the
disconnect exactly as it does today, and usbhub removes the hub and the
device behind it the way it removes any hub. A High-Speed device on a root
port gets no hub and takes today's path unchanged, with the switch on or
off.

In usbport's view a slower device on a root port gains one tier; in the
xHC's view nothing changes, because the controller keeps driving the device
on its root port with a Route String of 0 and no TT fields, as it does today.

What that buys follows from the EHCI model usbport applies. A Full or Low
Speed device behind a High-Speed hub is the case that model was written
for: `USBPORT_GetTt` walks up to the virtual hub, finds a TT record there,
and returns it. usbport then does the truthful thing at every layer:
`DeviceSpeed` is the decoded speed, `Period` is bucketed in frames (which
`XhciIntervalFromPeriod` already handles, since that is what a device
behind a real hub gets), isochronous packets are stamped per frame, the
device is budgeted against the hub's TT, and Device Manager says Full Speed.
The measurement issue 6 already took behind a Full-Speed hub is the
prediction for a device behind the virtual one: `bInterval` 10, 1 and 4
arriving as `Period` 8, 1 and 4. So is the real USB 2.0 hub measured on
32-bit Windows 7 on the E460 (issue 6 section 8): behind it the Low-Speed
mouse ran without the Vista/7 bugcheck and the Full-Speed audio device
played.

Nothing in the xHC has to know. The xHC handles Full and Low Speed on a
root port natively, Table 6-6 conditions the TT fields on "connected
through a High-speed hub", and the driver's own topology graph already
decides the TT fields from each hub's decoded speed rather than from what
usbport names (design record 02, step 3). The virtual hub is a device that
lives entirely in the miniport, answered in software, and the only thing it
translates is what usbport is told.

## 3. What the driver would have to do

### 3.1 The switch, and what off means

| | |
|---|---|
| Name | `XhciVirtualHSHub` (the owner's, 2026-09-25) |
| Type | `REG_DWORD` |
| Where | The controller's driver (software) key: the key a plain `AddReg` under an INF install section writes, and the key `XhciLogVerbosity` and the SuperSpeed storage proposal's `XhciSuperSpeed` live in |
| Values | `0` off, the default. `1` on, the on-demand shape of section 4. `2` is reserved for the permanent shape, section 4's fallback, and is refused like any other value unless that shape is built. A refused value is not clamped: the driver applies 0 and records that it refused, on the rule the INF states for `XhciLogVerbosity` |
| Absent | 0, and not an error |
| Set by | The user, by hand, in Registry Editor. The INF's `AddReg` writes the 0 so the value is visible where the user looks for it; `XHCISNAP` does not set it |

It is read the way the log values and the SuperSpeed storage proposal's
switch are read: through `UsbPortGetMiniportRegistryKeyValue` from
`StartController`, in the routine that already reads `XhciLogVerbosity`
(`xhciLogReadValues` in `src/xhci_dispatch.c`), because every constraint on
a registry read has been paid for there. PASSIVE_LEVEL only, since
`StartController` is the one callback this driver knows is PASSIVE. No new
import, since the `Zw*` names are denied by the import gate and usbport's
service is the only registry channel this project may use. Nothing in the
read may fail a start: the value missing, the read failing and a NULL
service pointer all leave it at 0 and the driver starts as today, with the
`MPSTATUS` kept beside the value so a reader can tell a 0 somebody set from
nothing found. usbport zeroes the miniport extension before every start, so
the value is re-read at every start and cached nowhere else; changing it
takes a disable and enable of the controller or a reboot. What was read,
what was applied and the status go into the snapshot header
(`docs/contributing/passthru-snapshot-instrument.md`), so a dump from a
stranger's machine states which mode the driver was in.

The virtual hub's vendor and product id are **user-settable, and the
binary carries no default** (the owner, 2026-09-25). They come from two more
values beside the switch, read in the same routine and at the same time,
and written by the INF:

| | `XhciVirtualHSHubVid` | `XhciVirtualHSHubPid` |
|---|---|---|
| Type | `REG_SZ`: a hexadecimal string, the form a user reads in Device Manager's `USB\VID_1209&PID_0001` and types into Registry Editor as a String Value | the same |
| Accepted | exactly four hexadecimal digits, either case, optionally prefixed `0x` or `0X`, and nothing else - no spaces, no sign, no fifth digit - with a vendor id of `0000` refused | the same, `0000` accepted |
| Written by the INF | `"1209"` | `"0001"` |
| Set by | the INF's `AddReg` (a plain string line, which both setup engines write as `REG_SZ`), in both INFs and on every install path, beside the switch's 0; the user may change it by hand in Registry Editor | the same |

**With the switch on, both ids must be present and valid, or the feature is
off.** A missing value, a failed read, a value that is not a string of the
accepted form, or a vendor id of `0000` makes the driver apply the switch as
0 for that start and record which value failed and why, beside the switch's
own read status in the snapshot header. A value a user created as a DWORD
instead of a String Value fails the same way, because its bytes are not
hexadecimal characters. The driver never invents an id: there is no id in
the binary to fall back to, so a machine where the INF's values have been
deleted runs today's driver, not a hub with a made-up identity. With the
switch off the two values are not consulted at all, so their state cannot
affect rule 2.

**Reading a string through usbport's service** needs three things the
DWORD values never did, all from what `UsbPortGetMiniportRegistryKeyValue`
is measured to do (ABI document, "`UsbPortGetMiniportRegistryKeyValue` -
read out of both binaries"). It returns neither the value's type nor its
length, and copies exactly the byte count asked for, whatever the value
holds; and anything but a clean `STATUS_SUCCESS` - a value too long for the
buffer included - comes back as the one failure code. So the driver asks
for a fixed buffer sized for the longest accepted form and its terminator
(`0x` plus four digits plus a NUL), treats a failure as "missing or too
long", and parses only up to the first NUL, refusing a buffer with none;
bytes after the terminator are whatever the service's scratch buffer held
and are never looked at. And the character encoding it hands back is
known on NT by construction (the NT registry stores `REG_SZ` as UTF-16)
and **not measured on Windows 98 or ME**, where the value comes
through NTKERN's registry layer. The accepted form makes that decidable
from the bytes alone: every accepted string is at least four characters of
ASCII, so as UTF-16 its second byte is 0 and as a single-byte string it is
not. The parser takes whichever the bytes show, host vectors run both, and
the 9x encoding is the first thing 24.3.4 reads on Windows 98 SE and ME.

The ids reach `GET_DESCRIPTOR(Device)` (3.3) and nothing else. Neither
decides which driver binds the hub: every hub INF this project has read
binds by class - `USB\HubClass` in NUSB's and SweetLow's `USB2.INF`,
`USB\CLASS_09` in NUSB 3.6's `USB.INF` - and 24.3.1 confirms the NT
targets' own. What an id does decide is the hub's hardware id,
`USB\VID_xxxx&PID_yyyy`, which Windows matches **before** the class, so an
id that some vendor INF on the machine names would bind that vendor's
driver to the virtual hub; the values exist so that a user who meets that
can move off it. Changing either one gives the hub a new hardware id, so
Windows treats it as a new device and installs the hub again on each port
it appears on. And since the INF writes all three values on every install,
as it does `XhciLogVerbosity`, a reinstall turns the feature off and puts
the ids back to the INF's.

**Why `1209:0001`, and what it is.** `0x1209` is pid.codes' open-source
vendor id, and `0x0001` is the first of its sixteen test product ids
(`0x0001`-`0x0010`), which pid.codes reserves "for use in private testing"
and says "MUST NOT be used on any device that will be redistributed, sold,
or manufactured", adding that software naming one should warn that it is
not unique and not for use outside testing. The owner chose it on
2026-09-25 because the feature is off by default and is documented as
**experimental, for private testing only** - pid.codes' own wording - and
because the id lives in the INF as a setting the user can change rather
than in the binary. Every
document that tells a user how to turn the feature on carries that warning
and says the id is pid.codes' shared test id, not one allocated to this
project. A project-allocated id from pid.codes (a pull request on its
repository, published under the project's name) remains the route if the
feature ever stops being experimental; that is the owner's to apply for.

The ids to avoid in the INF are other vendors'. Two look convenient and are
exactly that: the host controller's PCI vendor id (PCI-SIG and USB-IF
assign vendor ids separately, so the same number can belong to an unrelated
USB vendor, and Intel's `0x8086` and `0x8087` are USB vendor ids of real
hubs too), and `0x1D6B`, the Linux Foundation's id for Linux's own root
hubs.

Rule 2 is held at one divergence point, the decision of 3.2, and in the
on-demand shape that point is reached only after a root-port reset has
decoded a Full or Low Speed device. With the switch applied as 0 the
decision is never taken: no virtual record is ever created, the address map
never holds an address without a Slot ID, and the topology graph never sees
a hub it has to fold out. The only new code that runs in the off state is
the read and one test of the applied value at the decision point. The
root-hub report itself does not change in either state (3.2). Host vectors
run the affected paths in both states, and the device matrix runs with the
switch off in both the value-absent and the INF-installed setups, as the
SuperSpeed storage proposal's plan does, so that "today's driver" is a
measured reading rather than a claim.

The gate is there for two reasons. With the switch on, every slower device
plugged into a root port brings a "Generic USB Hub" devnode with it, and
one more enumeration before it works: a visible change a user who is happy
with the 4 ms mouse should not have to take. And the reading section 5
rests on, that each hub driver gives the virtual hub a TT record, has not
been taken; until it has on both primary targets, the default cannot move.

### 3.2 The root port, and the decision

The root hub keeps its descriptor, its port count and **every report it
makes today**, in both states of the switch. A connected root port already
reports connected and High Speed before any reset (the override in
`XhciPortShadowReport`), usbhub already resets it before it creates a
device there, and the reset is a physical one, as now. Nothing about the
root port tells usbport whether a virtual hub is coming.

The decision is taken when a root-port reset completes. The Port Speed
field is valid only once the port is enabled (Table 5-27, which is why the
shadow keeps the decoded speed from the reset), so this is the first moment
the driver knows what it has. With the switch on, a decoded Full or Low
Speed puts the port in **virtual-hub mode**; High Speed, or the switch off,
leaves it **direct**, which is today's path. The mode is recorded in the
same shadow update that latches `C_PORT_RESET`, and usbhub learns that the
reset is over only by reading that latch through `RH_GetPortStatus`, so no
request usbport sends to the new device can arrive before the mode is set.
On the timeout path the reset has not decoded a speed, and the port stays
direct.

In virtual-hub mode the reset arms the virtual hub's address-0 open rather
than a physical device's: the next `OpenEndpoint` for address 0 on that
port is served by the virtual record (3.3), and no Enable Slot is issued.
The rule that a root-port reset drops a pending hub-port claim
(`XhciTopoDropPending`) has to learn that difference.

The decision is taken again at **every** root-port reset, not only the
first: after an error usbhub cannot clear, on re-enumeration, and on
resume. A root-port reset while the port holds a virtual hub is a physical
reset, as today, which sends the device behind it back to the Default state,
exactly what resetting a real hub does to its children; usbhub then
re-creates the hub from address 0 and the hub re-creates its port. If a
re-taken decision changes the mode - a High-Speed device found after a
resume where a slower one was, or the reverse - the driver latches
`C_PORT_CONNECTION` on the root port so usbhub tears down what it believes
is there and enumerates the port again, and the next reset decides afresh.

`RH_ClearFeaturePortEnable` and `RH_ClearFeaturePortPower` on a port in
virtual-hub mode mean what they mean today: usbport has let go of the port.
The driver drops the virtual record and, through the existing disown path
(`XhciSlotPortDisowned` and the confirmed half), the device's slot. This is
the ordinary removal route in this shape, because an unplug is a root-port
disconnect: the Port Status Change Event path and the start/resume seed
keep every latch rule in `docs/contributing/implementation-invariants.md`,
"Root Hub Reporting", and announce through `UsbPortInvalidateRootHub` as
they do now.

### 3.3 The virtual hub as a device

usbport creates the virtual hub the way it creates any device: EP0 open at
address 0, `GET_DESCRIPTOR(Device)`, `SET_ADDRESS`, the descriptors again at
the new address, `SET_CONFIGURATION`, then the hub class. The driver
intercepts `SET_ADDRESS` today and issues Address Device; for the virtual
hub it records the address in the usbport-address map against a record
that has no Slot ID and no ring, and every later open and transfer keyed on
that address is served in software. Each request is a table row, and the
table is the whole device:

| Request | Answer |
|---|---|
| `GET_DESCRIPTOR(Device)` | `bcdUSB` 0x0200, class 9, subclass 0, `bDeviceProtocol` 1 (single TT), `bMaxPacketSize0` 64, the vendor and product id parsed from `XhciVirtualHSHubVid` / `XhciVirtualHSHubPid` (3.1; the hub does not exist without both), `bcdDevice` carrying the driver version, `iManufacturer` 0, `iProduct` 1, `iSerialNumber` 0 |
| `GET_DESCRIPTOR(Configuration)` | one configuration, one interface of class 9 with one interrupt IN endpoint (`bInterval` 12, `wMaxPacketSize` 1), `bmAttributes` self-powered, `bMaxPower` 0 |
| `GET_DESCRIPTOR(String)` | index 0: the language table, one LANGID, `0x0409` (English, United States). Index 1: the product string **"xhci98 virtual HS hub"** (the owner's, 2026-09-25), 21 characters, so `bLength` 44, UTF-16LE built from an ASCII literal, answered whatever LANGID `wIndex` names. Both truncated to `wLength`, as a device does when asked for the first two bytes. Any other index: a stall. There is deliberately no manufacturer string and **never a serial number**: with a serial, Windows keys the hub's instance on the serial rather than the port, so one hub instance would follow a device from port to port |
| `GET_DESCRIPTOR(Hub)` | `bNbrPorts` 1, `wHubCharacteristics` individual port power and over-current, `TTT` 0, `bPwrOn2PwrGood` from the root hub's own value (a smaller one would shorten every plug, since the physical port is already powered, but is only worth taking if every hub driver is measured to accept it), `bHubContrCurrent` 0, port 1 removable |
| `SET_CONFIGURATION`, `SET_INTERFACE` (alt 0), `GET_STATUS` (device, interface, endpoint), `CLEAR_FEATURE(ENDPOINT_HALT)` | success, no data |
| `GET_HUB_STATUS` | zero |
| `GET_PORT_STATUS(1)` | the real port's shadow reported truthfully: the connect, enable, suspend, over-current, reset and power bits as today, and the Low-Speed or High-Speed bit from the decoded speed, neither for Full Speed. `C_PORT_CONNECTION` is latched once when the hub is created, since the device was already there |
| `SET_PORT_FEATURE(1, PORT_RESET)` | a second physical reset through the existing path: PORTSC.PR, the asynchronous timeout, a reset generation, `C_PORT_RESET` on completion, reported through 3.4. It is physical, not synthesised, so usbhub's enumeration retries reach the device the way they would behind a real hub |
| `SET_PORT_FEATURE(1, PORT_POWER)` | success; the physical port is powered and stays so |
| `SET_PORT_FEATURE(1, PORT_SUSPEND)`, `CLEAR_PORT_FEATURE(1, PORT_SUSPEND)` | through 3.5's merge with the root port's own suspend state, then the existing `RH_SetFeature...` and resume bodies for the underlying port |
| `CLEAR_PORT_FEATURE(1, C_PORT_*)` | clear the virtual port's change bit, as `RH_ClearFeaturePortXChange` does for a root port |
| `CLEAR_PORT_FEATURE(1, PORT_ENABLE)`, `PORT_POWER` | the existing disown and disable bodies (`XhciSlotPortDisowned` and the confirmed half) for the device, which leaves the virtual hub in place with an empty port until usbhub resets it again or the root port goes |
| `CLEAR_TT_BUFFER`, `RESET_TT`, `GET_TT_STATE`, `STOP_TT` | success. The xHC has no translator on a root port to clear |
| anything else | a stall, counted |

Two rules from the tree apply to how the answers are delivered. A transfer
cannot be completed inside `SubmitTransfer`, because usbport writes to the
transfer record after that callback returns (design record 05, section 7:
the submit bracket is a lifetime rule, and `SubmitEpoch` is what defers a
completion out of it); a synthetic completion therefore threads onto the
same deferred-completion list a hardware completion uses and drains from
the same place, with the reply bytes written through the scatter/gather
list's `MappedSystemVa` before `UsbPortCompleteTransfer`, the one window in
which usbport keeps that mapping. And a refusal that can never stop being
true must fail the transfer rather than return nonzero, so an unknown
request is completed with a stall status, never left queued for retry.

The two EP0 snoops share this channel. The hub-topology graph
(`src/xhci_topo.c`) learns hubs from the hub-class traffic it sees at
placement and from the reply bytes it reads at completion; the descriptor
snoop (`src/xhci_desc.c`) reads configuration descriptors the same way. A
virtual hub's traffic must either go through both folds and be recognised
there, or be diverted before them. Going through is the smaller change and
the graph then sees the virtual hub as a hub, which 3.6 has to correct.

### 3.4 The status-change pipe

usbhub opens the hub's interrupt IN pipe and keeps one transfer pending on
it. The driver holds that transfer on the virtual device's queue and
completes it with a one-byte bitmap (bit 1 set) when the virtual port
latches a change; between changes the transfer stays pending, which is what
a real hub's pipe does. In this shape the pipe carries few changes: the
connect latched at creation, the completion of each port-1 reset, a suspend
change and an over-current. An unplug is **not** one of them; it is the
root port's connect change (3.2), and it removes the hub along with the
device. A change latched with no transfer pending is held until the next
transfer arrives and completed into it at once. The controller has still
acknowledged the PORTSC bit the moment it was read, so nothing here loosens
the "latch until asked" half of the rule. Because one PORTSC now feeds two
views, the root port's and the virtual port's, each view keeps its own
change bits, and acknowledging one does not clear the other.

The pipe's `Period` is whatever usbport buckets `bInterval` 12 into; it is
never programmed anywhere, since there is no endpoint. `AbortTransfer` on
this pipe must find the held transfer wherever it is, queue or completion
list, as design record 05 already requires, and the hub's teardown on an
unplug is the case that will exercise it most.

### 3.5 Suspend: one port, two views

usbhub can suspend the root port (which, to it, suspends the virtual hub)
and can suspend the virtual hub's port 1, and both are the same physical
port. The physical port is suspended while either view has it suspended,
and resumed only when both have asked; each view's resume completes with
its own `C_PORT_SUSPEND`, immediately if the physical port is already
running. A remote wake from the device resumes the physical port and
latches the change in both views. The permanent shape needs the same rule,
so it is not a cost of choosing on demand.

### 3.6 The topology graph

The virtual hub is a hub in usbport's view and must not be one in the
xHC's. Four rules in the graph and the device records change meaning:

- A device behind virtual hub port 1 on root port N is a root-port device:
  Route String 0, Root Hub Port Number N, `HubPort` 0, `RootPort` N, and
  no TT fields. The rule that a behind-hub record must not hold a root-hub
  port holds, because the virtual hub is folded out before the record is
  built.
- `SET_FEATURE(PORT_RESET)` on a virtual hub arms the root-port
  enumeration claim for the device, not a hub-port claim. The two claims
  stay mutually exclusive.
- `XhciTopoTtFor` must return "no TT" for a device whose nearest High-Speed
  ancestor is a virtual hub. usbport's `HubAddr`/`PortNumber` will name the
  virtual hub for every Full and Low Speed root-port device, so the
  comparison that feeds `TtPairsAgreed`/`TtPairsDisagreed` has to know the
  virtual address, or every such device counts as a disagreement.
- A device behind a real hub behind a virtual hub is one tier behind a real
  hub, and its TT fields come from that hub as they do today. A Full-Speed
  hub plugged into a root port decodes Full Speed, so it gets a virtual hub
  above it and, in usbport's view, a TT from it; the xHC needs none, which
  is already what the graph derives for a Full-Speed hub on a root port.
  This is the topology that bugchecks Vista and 7 today (issue 6 section
  6.2), and the virtual hub is what takes it off the NULL-TT path. A
  High-Speed hub on a root port decodes High Speed and stays direct.

Device removal keeps its route: an unplug is the root-port disconnect it is
today, and the virtual record goes in the same teardown. The hub-path
removal (the `GET_STATUS(port)` reply fold that tears down a behind-hub
device and its subtree) is not involved.

### 3.7 What stays as it is

Everything below the usbport-facing surface, and the root-hub report above
it. Port speed decoding, the Slot Context and EP0 programming from the
decoded speed, the interval floor at Table 6-12's minimum, the reset
generations, the disown and disable split, the failure counters, the log
channel, the PORTSC watchdog and the recovery latch are untouched. The
High-Speed override in `XhciPortShadowReport` stays where it is, and in
virtual-hub mode what it describes is the virtual hub. A High-Speed device
on a root port takes today's path, byte for byte, whatever the switch says.

## 4. Two shapes, and which to build

**On demand (the proposed shape, the owner's choice of 2026-09-25; value
1).** A virtual hub exists only while a Full or Low Speed device is on that
root port, created at the reset that decodes it and removed with it. What
decides it over the permanent shape is how much the new code touches: it
runs only for the devices that are broken today - the polling bands, the
silent audio from XP on, the Vista/7 bugcheck behind a USB 1.1 hub - and
never for a High-Speed device, which is most of what gets plugged in and
all of what works now. Three things follow:

- The root-hub report is unchanged in both states of the switch (3.2), so
  the divergence point is one decision after a reset, not a rewrite of the
  root port.
- An unplug is a root-port disconnect, the removal path that exists today;
  usbhub removes the hub and the device itself (3.6).
- No hub devnode, USB address or boot-time enumeration is spent on an empty
  port or a High-Speed device, and the idle-suspend and boot-time questions
  of section 5 arise only while a slower device is attached.

Its costs, each with a home above: the decision is taken again at every
root-port reset, and a mode change across a resume needs a forced connect
change (3.2); each plug takes two physical resets and one hub enumeration
before the device's own, so a slower device takes longer to become usable
than today; the two views of one port have to merge their suspend states
(3.5); and a "Generic USB Hub" devnode appears and disappears with every
plug and unplug. On Windows 98 its first appearance on each port is a hub
install, which may show the New Hardware wizard once per port; and the hub
removal path in every hub driver runs on every unplug, which is why section
5's churn reading decides between the shapes.

**Permanent (the fallback; value 2 if it is ever built).** Every managed
port carries a virtual hub from start to stop, plugged or not, and every
root-port device, High Speed included, sits behind one. It is one state
machine per port with no decision point, a device swap across a suspend is
just a device change behind a hub that already exists, a plug costs no hub
enumeration, and nothing appears or disappears in Device Manager. Its costs
are the reason it is not proposed: every root-port device reaches usbport
through the new path, so a defect in it reaches the USB stick as well as
the mouse, and the whole device matrix has to be re-read under it; one
"Generic USB Hub" devnode and one USB address per managed port; N hub
enumerations at every start and resume, on a Windows 98 machine that already
boots slowly; pending interrupt transfers on N hubs that may hold off
usbport's idle suspend permanently; a root port that must report connected,
enabled and High Speed whether or not anything is plugged in, with
synthetic resets; and removal that goes through the hub path instead of the
root port. Build it only if the churn reading of section 5 fails on a
target under the on-demand shape.

This page recommended the permanent shape until 2026-09-25, on the grounds
of one state machine and no decision point, and called on demand "the
smaller visible change and the larger state change". That undercounted what
on demand saves: its decision needs no change to what the root hub reports,
because a connected root port already says High Speed before the reset that
decides, and its removal is the path that already exists. The permanent
shape rewrites both.

## 5. What has to be measured before it is trusted

- **That usbhub creates a TT record for the virtual hub on every stack.**
  The whole idea rests on `USBPORT_GetTt` finding a non-empty list. Batch
  7b-V0 measured that a believed High-Speed hub with `bDeviceProtocol` 0
  got one (design record 02, open question 6), and a hub declaring protocol
  1 is the case the hub driver was written for, but why either holds is
  unconfirmed against the binaries. The probe is the one the tree already
  has: `HubAddr != 0xFFFF` on the first Full-Speed device's endpoint
  properties, read before the device is opened, on NUSB's build, SP4's,
  SweetLow's and XP's.
- **What a one-port hub does to each hub driver.** `bNbrPorts` 1 is legal
  and unusual. usbhub20, Windows 2000's `usbhub.sys`, Windows ME's and XP's
  each need one boot with the switch on.
- **Churn, the reading that decides between the shapes.** A Full-Speed
  device plugged and unplugged a few dozen times on a root port, on every
  target, the switch on: each cycle creates and removes a hub. A target
  whose hub driver or usbport does not survive it is the case for the
  permanent fallback. It is the test issue 6 section 6.2's Vista and 7
  bugcheck already needed, and the matrix's churn row is the vehicle.
- **Plug latency.** From plug to device usable, against today, with a
  mouse and an audio device; `bPwrOn2PwrGood` is the lever if it matters.
- **The first hub install on Windows 98 and ME.** Whether the hub's first
  appearance on a port runs the New Hardware wizard or asks for the CD,
  under NUSB's stack and SweetLow's.
- **Idle suspend.** usbport idle-suspends a quiet controller (issue 5).
  Whether a virtual hub's pending interrupt transfer holds that off while a
  slower device is attached, which changes what the package's
  `USB_MINIPORT_FLAGS_DISABLE_SS` is for.
- **Resume.** The decision re-taken at the resume reset: the same device
  back in virtual-hub mode, and a device swapped for a High-Speed one while
  suspended, which must force the connect change of 3.2.
- **The product string.** Which hub drivers ask for the language table and
  index 1 unprompted, that each accepts the answer, and where the name shows:
  USBView, and the bus-reported description on Vista and 7.
- **Boot time.** One extra enumeration per slower device attached at boot.
- **The interval.** The measurement issue 6 took with a `bInterval`
  override tool, repeated on a root port with the switch on: `bInterval`
  10 should arrive as `Period` 8 and program Interval 6.
- **The bandwidth half** stays unmeasured until a root port is loaded with
  enough Full-Speed periodic traffic to reach usbport's TT budget.

## 6. What QEMU can and cannot show

`qemu-xhci` with `usb-kbd,usb_version=1`, `usb-mouse,usb_version=1` and
`usb-audio` (all Full Speed) on root ports reproduces every reading in
section 5 except boot time and Low Speed, on every guest the project holds;
QEMU models no Low-Speed peripheral, so Low Speed takes the `usb-host`
passthrough of a real mouse that roadmap task 24.1 built. Churn is
scriptable from the monitor (`device_add` / `device_del`). The device
matrix's Full-Speed rows run unchanged with the switch on and are the
regression half, and its High-Speed rows must read exactly as with the
switch off. What QEMU cannot show is a real Windows 98 machine's hub driver
under this, and that is the E460's reading: the Low-Speed mouse and the
Full-Speed audio device from `docs/contributing/test-equipment.md` on root
ports, with the interval read from the snapshot instrument.

## 7. What it does not do

- High-Speed devices see no change, with the switch on or off.
- The 1 ms floor stays, since the xHCI specification allows nothing less at
  Full and Low Speed. What goes away is the bucketing above it.
- usbport still rounds a Full or Low Speed `bInterval` down to a power of
  two in frames, so `bInterval` 10 gives 8 ms, as it does behind a real hub.
- It does not remove the override. Under a usbport that guarded the empty
  list (issue 6 section 8) a truthful root port would be simpler than a
  virtual hub, but no such usbport exists for these targets.
- It does not interact with the SuperSpeed storage proposal except by
  composition: a SuperSpeed device reported as High-Speed takes the direct
  path, since High Speed is what the decision sees.

## 8. Batches (roadmap sub-tasks 24.3.2 to 24.3.5)

- `-0`: the request table of 3.3 as host vectors (`test/`), fed the
  setup packets both shipping hub drivers send, from the measurements in
  design record 02 (`wValue` 0 on `GET_DESCRIPTOR(Hub)`, `wLength` 71); the
  decision of 3.2 (mode by decoded speed, the timeout path, the re-taken
  decision and its forced connect change); the suspend merge of 3.5; the
  graph fold of 3.6 as vectors over the existing topology tests; the
  switch of 3.1, its refusal of unknown values (2 included until the
  permanent shape exists), its snapshot header fields, and the off-state
  vectors that hold rule 2.
- `-A`: the virtual device record, the decision point, the synthetic
  completion path, the status-change pipe, the suspend merge.
- `-V`: section 6 on every guest held - Windows 98 SE under NUSB and under
  SweetLow's stack, ME, 2000, XP in both architectures, Vista and 7 in both
  - switch off then on, the device matrix in both states, and the churn
  reading.
- `-E`: the E460 reading.

## 9. Decisions the owner would take

1. Whether it is wanted at all, against the cost in section 4: a hub
   devnode and a longer enumeration for each slower device, for the
   device's own interval (a stock mouse at its declared 8 ms rather than
   today's 4 ms band, and every rate hidusbf offers), Full-Speed audio from
   XP on, and the Vista/7 hub bugcheck. **Answered 2026-09-25: taken, as
   roadmap task 24.3, inside Phase 24, shipping in one cut with task
   24.1.** And on which targets: **all of them** with the switch on -
   Windows 98 SE under NUSB and SweetLow's stack, ME, 2000, XP, Vista and 7
   - rather than XP and later only.
2. Permanent or on demand. **Answered 2026-09-25: on demand, with the
   permanent shape kept as the fallback if the churn reading fails on a
   target.**
3. The virtual hub's vendor and product id and whether it carries strings.
   **Ids answered 2026-09-25:** user-settable through
   `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, hexadecimal strings
   (`REG_SZ`) the INF writes as pid.codes' test id, `"1209"` and `"0001"`,
   no id in the binary, and the feature off
   if either is missing or invalid (3.1); documented as experimental, for
   private testing only. **Strings answered the same day:** a product
   string only, "xhci98 virtual HS hub"; no manufacturer string, never a
   serial number (3.3).
4. The switch's name. **Answered 2026-09-25: `XhciVirtualHSHub`.**
5. Whether the switch defaults to on in a later release once section 5 is
   read on both primary targets, or stays an opt-in like the SuperSpeed
   storage proposal's. It is off in the Phase 24 cut; the later release is
   still open.

## Sources

- `docs/issues/06-full-speed-root-port-bugcheck.md`: the bugcheck, the
  override, the three bands, the behind-hub measurement, the real USB 2.0
  hub on the E460, and the opt-in that cannot be taken.
- `docs/contributing/runs/run-24.md`, 24.3: the truthful report run under
  SweetLow's stack.
- `docs/usb-xhci-info/usbport-miniport-abi.md` section 4 (the root-hub
  callbacks and their contracts), section 5 ("Periodic scheduling: what
  `Period` actually carries") and section 8 ("The transaction-translator
  lookup").
- `docs/usb-xhci-info/xhci-data-structures.md`: Table 5-27 (Port Speed valid
  once enabled) and Table 6-6 (the TT fields).
- `docs/contributing/design/02-hub-topology-route-string.md`: what the
  miniport sees of hub-class traffic, the graph, step 3's TT derivation,
  and open question 6.
- `docs/contributing/design/05-locking-model.md` section 7: the submit
  bracket and the deferred-completion list.
- `docs/contributing/implementation-invariants.md`: "Root Hub Reporting",
  "Device Addressing", "Hub Paths".
- `docs/issues/05-idle-suspend-and-disableselectivesuspend.md`: the idle
  suspend the virtual hubs may or may not hold off.
- `src/xhci_port.c` (`XhciPortShadowReport`), `src/xhci_topo.c`,
  `src/xhci_ctx.c` (`XhciIntervalFromPeriod`).
